#include "EngineApi.h"
#include "AdbHelper.h"
#include "AiEngine.h"
#include "BluetoothClient.h"
#include "GpuProcessor.h"
#include "NetworkClient.h"
#include "VideoDecoder.h"
#include "VirtualCamManager.h"
#include "../common/ColorConvert.h"
#include "../common/SharedFrameBuffer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace phonecam;

std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

int32_t CopyToOutBuffer(const std::string& s, char* outBuf, int32_t bufSize) {
    if (!outBuf || bufSize <= 0) return static_cast<int32_t>(s.size());
    const size_t n = std::min(s.size(), static_cast<size_t>(bufSize - 1));
    std::memcpy(outBuf, s.data(), n);
    outBuf[n] = '\0';
    return static_cast<int32_t>(n);
}

void FillAsymmetricTestPatternBgra(std::vector<uint8_t>& bgra, uint32_t w, uint32_t h, uint64_t tick) {
    bgra.resize(static_cast<size_t>(w) * h * 4u);
    const uint32_t barX = static_cast<uint32_t>((tick * 10) % w);
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t* row = bgra.data() + static_cast<size_t>(y) * w * 4u;
        const bool top = y < (h / 2);
        for (uint32_t x = 0; x < w; ++x) {
            const bool left = x < (w / 2);
            uint8_t r = 40, g = 45, b = 60;
            if (top && left) {
                r = 212;
                g = 158;
                b = 128;
            } else if (top && !left) {
                r = 45;
                g = 135;
                b = 235;
            } else if (!top && left) {
                r = 35;
                g = 185;
                b = 115;
            } else {
                r = static_cast<uint8_t>(120 + (x * 110) / w);
                g = static_cast<uint8_t>(90 + (y * 100) / h);
                b = 95;
            }

            // Asymmetric L-shape marker to make mirror/orientation clear
            if ((x >= 80 && x <= 105 && y >= 80 && y <= 240) ||
                (x >= 80 && x <= 185 && y >= 215 && y <= 240)) {
                r = 250; g = 250; b = 250;
            }

            if (std::abs(static_cast<int>(x) - static_cast<int>(barX)) < 4) {
                r = 255; g = 220; b = 60;
            }

            row[x * 4 + 0] = b;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = r;
            row[x * 4 + 3] = 255;
        }
    }
}

PC_VideoParams DefaultVideoParams() {
    PC_VideoParams p{};
    p.flipH = 0;
    p.flipV = 0;
    p.manualRotationDeg = 0;
    p.autoRotateFromPhone = 1;
    p.framingMode = 0;
    p.outputWidth = 1280;
    p.outputHeight = 720;
    p.outputFps = 60;
    p.previewMirrorLocal = 0;
    p.mirrorCheckOverlay = 0;
    p.bypassEffects = 0;
    p.testPatternMode = 0;
    p.bgMode = 0;
    p.arFilterMode = 0;
    p.styleFilterMode = 0;
    p.autoFrameEnabled = 0;

    p.zoom = 1.0f;
    p.panX = 0.0f;
    p.panY = 0.0f;
    p.skinSmooth = 0.0f;
    p.denoise = 0.0f;
    p.studioLight = 0.0f;
    p.exposure = 0.0f;
    p.brightness = 0.0f;
    p.contrast = 1.0f;
    p.highlights = 0.0f;
    p.shadows = 0.0f;
    p.gammaVal = 1.0f;
    p.temperature = 0.0f;
    p.tint = 0.0f;
    p.saturation = 1.0f;
    p.vibrance = 0.0f;
    p.sharpen = 0.0f;
    p.softGlow = 0.0f;
    p.vignette = 0.0f;
    p.autoWhiteBalance = 0.0f;
    p.splitComparePos = -1.0f;
    p.bgBlurStrength = 0.65f;
    p.faceSlim = 0.0f;
    p.eyeEnlarge = 0.0f;
    p.eyeBrighten = 0.0f;
    p.teethWhiten = 0.0f;
    p.styleIntensity = 0.75f;
    return p;
}

class StudioEngine {
public:
    StudioEngine() : params_(DefaultVideoParams()) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        shmWriter_.Open();
        ai_.Initialize();
        gpu_.Initialize(false);
        gpu_.SetAiEngine(&ai_);
        decoder_.Initialize();
        pumpRunning_.store(true);
        pumpThread_ = std::thread(&StudioEngine::PumpLoop, this);
    }

    ~StudioEngine() {
        pumpRunning_.store(false);
        if (pumpThread_.joinable()) pumpThread_.join();
        net_.Disconnect();
        net_.StopDiscovery();
        vcam_.Stop();
        gpu_.SetAiEngine(nullptr);
        ai_.Shutdown();
        decoder_.Shutdown();
        gpu_.Shutdown();
        shmWriter_.Close();
        CoUninitialize();
    }

    bool StartDiscovery() {
        return net_.StartDiscovery();
    }

    std::string GetDiscoveredJson() {
        auto devs = net_.GetDiscoveredDevices();
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < devs.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{"
                << "\"ip\":\"" << EscapeJson(devs[i].ip) << "\","
                << "\"deviceName\":\"" << EscapeJson(devs[i].deviceName) << "\","
                << "\"tcpPort\":" << devs[i].tcpPort << ","
                << "\"pinRequired\":" << (devs[i].pinRequired ? "true" : "false") << ","
                << "\"facing\":\"" << EscapeJson(devs[i].facing) << "\","
                << "\"streaming\":" << (devs[i].streaming ? "true" : "false")
                << "}";
        }
        oss << "]";
        return oss.str();
    }

    bool Connect(const char* ip, int32_t port, const char* pin, bool isUsb) {
        if (!ip || !*ip) return false;
        std::string pinStr = pin ? pin : "";
        {
            std::lock_guard<std::mutex> lk(decoderMutex_);
            decoder_.ResetH264();
        }
        {
            std::lock_guard<std::mutex> lk(errMutex_);
            customError_.clear();
        }
        uint16_t p = (port > 0 && port <= 65535) ? static_cast<uint16_t>(port) : kTcpPort;
        return net_.Connect(ip, p, pinStr, isUsb, [this](PacketType type, uint8_t flags, const uint8_t* data, size_t size, uint32_t tsMs) {
            OnVideoPacket(type, flags, data, size, tsMs);
        });
    }

    bool ConnectUsb(const char* pin) {
        AdbResult adb = AdbHelper::ForwardStreamPort(kTcpPort, kTcpPort);
        if (!adb.ok) {
            std::lock_guard<std::mutex> lk(errMutex_);
            customError_ = adb.message;
            return false;
        }
        return Connect("127.0.0.1", kTcpPort, pin, true);
    }

    std::string GetUsbStatusJson() {
        UsbStatusSnapshot st = AdbHelper::GetUsbStatus();
        bool devConn = !st.devices.empty();
        bool auth = false;
        std::string serial;
        std::string msg;

        for (const auto& d : st.devices) {
            if (d.state == "device") {
                auth = true;
                serial = d.serial;
                msg = "USB Authorized (" + serial + ")";
                break;
            }
        }
        if (!auth && !st.devices.empty()) {
            serial = st.devices[0].serial;
            msg = (st.devices[0].state == "unauthorized")
                ? "USB Unauthorized: accept RSA USB Debugging popup on phone"
                : ("USB Device state: " + st.devices[0].state);
        } else if (!st.adbFound) {
            msg = "adb.exe not found";
        } else if (st.devices.empty()) {
            msg = "No USB Android device detected";
        }

        std::ostringstream oss;
        oss << "{"
            << "\"adbFound\":" << (st.adbFound ? "true" : "false") << ","
            << "\"deviceConnected\":" << (devConn ? "true" : "false") << ","
            << "\"authorized\":" << (auth ? "true" : "false") << ","
            << "\"deviceCount\":" << st.devices.size() << ","
            << "\"serial\":\"" << EscapeJson(serial) << "\","
            << "\"message\":\"" << EscapeJson(msg) << "\""
            << "}";
        return oss.str();
    }

    std::string GetBluetoothDevicesJson() {
        auto devs = BluetoothHelper::EnumeratePairedDevices();
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < devs.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{"
                << "\"address\":\"" << EscapeJson(devs[i].address) << "\","
                << "\"name\":\"" << EscapeJson(devs[i].name) << "\","
                << "\"connected\":" << (devs[i].connected ? "true" : "false") << ","
                << "\"authenticated\":" << (devs[i].authenticated ? "true" : "false")
                << "}";
        }
        oss << "]";
        return oss.str();
    }

    bool ConnectBluetooth(const char* btAddr, const char* pin) {
        if (!btAddr || !*btAddr) return false;
        std::string pinStr = pin ? pin : "";
        {
            std::lock_guard<std::mutex> lk(decoderMutex_);
            decoder_.ResetH264();
        }
        {
            std::lock_guard<std::mutex> lk(errMutex_);
            customError_.clear();
        }
        return net_.ConnectBluetooth(btAddr, pinStr, [this](PacketType type, uint8_t flags, const uint8_t* data, size_t size, uint32_t tsMs) {
            OnVideoPacket(type, flags, data, size, tsMs);
        });
    }

    void Disconnect() {
        net_.Disconnect();
    }

    bool SendControlJson(const char* json) {
        if (!json) return false;
        return net_.SendControlJson(json);
    }

    void SetVideoParams(const PC_VideoParams* p) {
        if (!p) return;
        std::lock_guard<std::mutex> lk(paramsMutex_);
        params_ = *p;
    }

    bool SetCustomBackgroundPath(const char* utf8Path) {
        return gpu_.SetCustomBackgroundPath(utf8Path ? utf8Path : "");
    }

    void SetPreviewCallback(PC_PreviewFrameCallback cb, void* userData) {
        std::lock_guard<std::mutex> lk(cbMutex_);
        previewCb_ = cb;
        previewUser_ = userData;
    }

    bool StartVirtualCam() {
        if (!shmWriter_.IsOpen()) shmWriter_.Open();
        return vcam_.Start();
    }

    void StopVirtualCam() {
        vcam_.Stop();
    }

    std::string GetStatusJson() {
        auto st = net_.GetStatus();
        std::string extraErr;
        {
            std::lock_guard<std::mutex> lk(errMutex_);
            extraErr = customError_;
        }
        std::string err = st.lastError.empty() ? extraErr : st.lastError;
        std::ostringstream oss;
        oss << "{"
            << "\"connected\":" << (st.connected ? "true" : "false") << ","
            << "\"connecting\":" << (st.connecting ? "true" : "false") << ","
            << "\"authenticated\":" << (st.authenticated ? "true" : "false") << ","
            << "\"isUsb\":" << (st.isUsb ? "true" : "false") << ","
            << "\"isBluetooth\":" << (st.isBluetooth ? "true" : "false") << ","
            << "\"hwDecode\":" << (decoder_.IsUsingHardwareDecode() ? "true" : "false") << ","
            << "\"aiModelsLoaded\":" << (ai_.IsModelsLoaded() ? "true" : "false") << ","
            << "\"aiDirectML\":" << (ai_.IsUsingDirectML() ? "true" : "false") << ","
            << "\"remoteIp\":\"" << EscapeJson(st.remoteIp) << "\","
            << "\"deviceName\":\"" << EscapeJson(st.deviceName) << "\","
            << "\"lastError\":\"" << EscapeJson(err) << "\","
            << "\"latencyMs\":" << st.latencyMs << ","
            << "\"rxFps\":" << st.rxFps << ","
            << "\"frontCameraActive\":" << (frontActive_.load() ? "true" : "false") << ","
            << "\"cameras\":" << (st.camerasJson.empty() ? "[]" : st.camerasJson) << ","
            << "\"telemetry\":" << (st.telemetryJson.empty() ? "{}" : st.telemetryJson)
            << "}";
        return oss.str();
    }

    std::string GetVCamDiagnosticsJson() {
        auto d = vcam_.GetDiagnostics();
        std::ostringstream oss;
        oss << "{"
            << "\"isWindows11OrNewer\":" << (d.isWindows11OrNewer ? "true" : "false") << ","
            << "\"mfApiAvailable\":" << (d.mfApiAvailable ? "true" : "false") << ","
            << "\"mfComRegistered\":" << (d.mfComRegistered ? "true" : "false") << ","
            << "\"dsComRegistered\":" << (d.dsComRegistered ? "true" : "false") << ","
            << "\"active\":" << (d.active ? "true" : "false") << ","
            << "\"gpuWarpFallback\":" << (gpu_.IsWarp() ? "true" : "false") << ","
            << "\"shmOpen\":" << (shmWriter_.IsOpen() ? "true" : "false") << ","
            << "\"activeBackend\":\"" << EscapeJson(d.activeBackend) << "\","
            << "\"lastError\":\"" << EscapeJson(d.lastError) << "\""
            << "}";
        return oss.str();
    }

    bool PushSyntheticTestFrame() {
        PC_VideoParams p = GetParams();
        std::vector<uint8_t> bgra;
        const uint32_t w = 1280, h = 720;
        FillAsymmetricTestPatternBgra(bgra, w, h, ++syntheticTick_);
        RawFrameInput in;
        in.format = InputPixelFormat::BGRA32;
        in.data = bgra.data();
        in.width = w;
        in.height = h;
        in.strideY = w * 4u;
        in.sensorRotationDeg = 0;

        return RunGpuAndPublish(in, p, true);
    }

private:
    PC_VideoParams GetParams() {
        std::lock_guard<std::mutex> lk(paramsMutex_);
        return params_;
    }

    bool RunGpuAndPublish(const RawFrameInput& in, const PC_VideoParams& p, bool forcePreview) {
        PC_PreviewFrameCallback cb = nullptr;
        void* user = nullptr;
        {
            std::lock_guard<std::mutex> lk(cbMutex_);
            cb = previewCb_;
            user = previewUser_;
        }

        const uint64_t nowMs = GetTickCount64();
        const bool wantPreview = (cb != nullptr) && (forcePreview || (nowMs - lastPreviewCallbackMs_.load() >= 30u));

        ProcessedFrameOutput out;
        if (!gpu_.ProcessFrame(in, p, wantPreview, out)) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lk(shmMutex_);
            if (!shmWriter_.IsOpen()) {
                shmWriter_.Open();
            }
            if (shmWriter_.IsOpen() && !out.nv12.empty()) {
                const uint32_t fps = (p.outputFps >= 45) ? 60u : 30u;
                shmWriter_.WriteFrameNV12(out.nv12.data(), out.width, out.height, fps, 1u);
            }
        }

        if (cb && !out.previewBgra.empty() && out.previewWidth > 0 && out.previewHeight > 0) {
            lastPreviewCallbackMs_.store(nowMs);
            cb(out.previewBgra.data(),
               static_cast<int32_t>(out.previewWidth),
               static_cast<int32_t>(out.previewHeight),
               static_cast<int32_t>(out.previewWidth * 4u),
               user);
        }
        return true;
    }

    void OnVideoPacket(PacketType type, uint8_t flags, const uint8_t* data, size_t size, uint32_t tsMs) {
        if (!comInitOnNetThread_) {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            comInitOnNetThread_ = true;
        }
        frontActive_.store(IsFrontCameraFlag(flags));

        DecodedFrame decoded;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(decoderMutex_);
            if (type == PacketType::VideoH264) {
                ok = decoder_.DecodeH264(data, size, tsMs, decoded);
            } else if (type == PacketType::VideoMjpeg) {
                ok = decoder_.DecodeMjpeg(data, size, decoded);
            }
        }
        if (!ok || decoded.data.empty()) return;

        lastLiveVideoTickMs_.store(GetTickCount64());

        RawFrameInput in;
        in.format = decoded.isBgra ? InputPixelFormat::BGRA32 : InputPixelFormat::NV12;
        in.data = decoded.data.data();
        in.width = decoded.width;
        in.height = decoded.height;
        in.strideY = decoded.stride;
        in.strideUV = decoded.stride;
        in.sensorRotationDeg = ExtractRotationDeg(flags);

        RunGpuAndPublish(in, GetParams(), false);
    }

    void PumpLoop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::vector<uint8_t> standbyNv12;
        std::vector<uint8_t> standbyBgra;
        while (pumpRunning_.load()) {
            const uint64_t now = GetTickCount64();
            const uint64_t lastLive = lastLiveVideoTickMs_.load();
            if (lastLive == 0 || (now > lastLive && (now - lastLive) > 450u)) {
                PC_VideoParams p = GetParams();
                if (p.testPatternMode != 0) {
                    PushSyntheticTestFrame();
                } else {
                    const uint32_t outW = (p.outputWidth >= 640 && p.outputWidth <= 1920)
                        ? (static_cast<uint32_t>(p.outputWidth) & ~1u) : 1280u;
                    const uint32_t outH = (p.outputHeight >= 360 && p.outputHeight <= 1080)
                        ? (static_cast<uint32_t>(p.outputHeight) & ~1u) : 720u;
                    RenderStandbyNV12(standbyNv12, outW, outH, ++syntheticTick_);
                    {
                        std::lock_guard<std::mutex> lk(shmMutex_);
                        if (!shmWriter_.IsOpen()) shmWriter_.Open();
                        if (shmWriter_.IsOpen()) {
                            shmWriter_.WriteFrameNV12(standbyNv12.data(), outW, outH, 30u, 1u);
                        }
                    }
                    PC_PreviewFrameCallback cb = nullptr;
                    void* user = nullptr;
                    {
                        std::lock_guard<std::mutex> lk(cbMutex_);
                        cb = previewCb_;
                        user = previewUser_;
                    }
                    if (cb) {
                        standbyBgra.resize(static_cast<size_t>(outW) * outH * 4u);
                        NV12ToBGRA32(standbyNv12.data(), outW, outH, standbyBgra.data(), outW * 4u);
                        cb(standbyBgra.data(), static_cast<int32_t>(outW), static_cast<int32_t>(outH), static_cast<int32_t>(outW * 4u), user);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
        CoUninitialize();
    }

    SharedFrameWriter shmWriter_;
    AiEngine ai_;
    GpuProcessor gpu_;
    VideoDecoder decoder_;
    NetworkClient net_;
    VirtualCamManager vcam_;

    std::mutex paramsMutex_;
    PC_VideoParams params_{};

    std::mutex shmMutex_;
    std::mutex decoderMutex_;
    std::mutex cbMutex_;
    PC_PreviewFrameCallback previewCb_ = nullptr;
    void* previewUser_ = nullptr;

    std::mutex errMutex_;
    std::string customError_;

    std::atomic<bool> pumpRunning_{ false };
    std::thread pumpThread_;
    std::atomic<uint64_t> lastLiveVideoTickMs_{ 0 };
    std::atomic<uint64_t> lastPreviewCallbackMs_{ 0 };
    std::atomic<uint64_t> syntheticTick_{ 0 };
    std::atomic<bool> frontActive_{ true };
    thread_local static bool comInitOnNetThread_;
};

thread_local bool StudioEngine::comInitOnNetThread_ = false;

} // namespace

extern "C" {

PC_API void* __stdcall pc_create() {
    return new StudioEngine();
}

PC_API void __stdcall pc_destroy(void* handle) {
    delete static_cast<StudioEngine*>(handle);
}

PC_API int32_t __stdcall pc_start_discovery(void* handle) {
    auto* e = static_cast<StudioEngine*>(handle);
    return (e && e->StartDiscovery()) ? 1 : 0;
}

PC_API int32_t __stdcall pc_get_discovered_devices_json(void* handle, char* outBuf, int32_t bufSize) {
    auto* e = static_cast<StudioEngine*>(handle);
    if (!e) return 0;
    return CopyToOutBuffer(e->GetDiscoveredJson(), outBuf, bufSize);
}

PC_API int32_t __stdcall pc_connect(void* handle, const char* ipUtf8, int32_t port, const char* pinUtf8) {
    auto* e = static_cast<StudioEngine*>(handle);
    return (e && e->Connect(ipUtf8, port, pinUtf8, false)) ? 1 : 0;
}

PC_API int32_t __stdcall pc_connect_usb(void* handle, const char* pinUtf8) {
    auto* e = static_cast<StudioEngine*>(handle);
    return (e && e->ConnectUsb(pinUtf8)) ? 1 : 0;
}

PC_API int32_t __stdcall pc_get_usb_status_json(void* handle, char* outBuf, int32_t bufSize) {
    auto* e = static_cast<StudioEngine*>(handle);
    if (!e) return 0;
    return CopyToOutBuffer(e->GetUsbStatusJson(), outBuf, bufSize);
}

PC_API int32_t __stdcall pc_get_bluetooth_devices_json(void* handle, char* outBuf, int32_t bufSize) {
    auto* e = static_cast<StudioEngine*>(handle);
    if (!e) return 0;
    return CopyToOutBuffer(e->GetBluetoothDevicesJson(), outBuf, bufSize);
}

PC_API int32_t __stdcall pc_connect_bluetooth(void* handle, const char* btAddrUtf8, const char* pinUtf8) {
    auto* e = static_cast<StudioEngine*>(handle);
    return (e && e->ConnectBluetooth(btAddrUtf8, pinUtf8)) ? 1 : 0;
}

PC_API void __stdcall pc_disconnect(void* handle) {
    if (auto* e = static_cast<StudioEngine*>(handle)) e->Disconnect();
}

PC_API int32_t __stdcall pc_get_status_json(void* handle, char* outBuf, int32_t bufSize) {
    auto* e = static_cast<StudioEngine*>(handle);
    if (!e) return 0;
    return CopyToOutBuffer(e->GetStatusJson(), outBuf, bufSize);
}

PC_API int32_t __stdcall pc_send_control_json(void* handle, const char* jsonUtf8) {
    auto* e = static_cast<StudioEngine*>(handle);
    return (e && e->SendControlJson(jsonUtf8)) ? 1 : 0;
}

PC_API void __stdcall pc_set_video_params(void* handle, const PC_VideoParams* params) {
    if (auto* e = static_cast<StudioEngine*>(handle)) e->SetVideoParams(params);
}

PC_API int32_t __stdcall pc_set_custom_background_path(void* handle, const char* utf8Path) {
    auto* e = static_cast<StudioEngine*>(handle);
    return (e && e->SetCustomBackgroundPath(utf8Path)) ? 1 : 0;
}

PC_API int32_t __stdcall pc_start_virtual_cam(void* handle) {
    auto* e = static_cast<StudioEngine*>(handle);
    return (e && e->StartVirtualCam()) ? 1 : 0;
}

PC_API void __stdcall pc_stop_virtual_cam(void* handle) {
    if (auto* e = static_cast<StudioEngine*>(handle)) e->StopVirtualCam();
}

PC_API int32_t __stdcall pc_get_vcam_diagnostics_json(void* handle, char* outBuf, int32_t bufSize) {
    auto* e = static_cast<StudioEngine*>(handle);
    if (!e) return 0;
    return CopyToOutBuffer(e->GetVCamDiagnosticsJson(), outBuf, bufSize);
}

PC_API void __stdcall pc_set_preview_callback(void* handle, PC_PreviewFrameCallback cb, void* userData) {
    if (auto* e = static_cast<StudioEngine*>(handle)) e->SetPreviewCallback(cb, userData);
}

PC_API int32_t __stdcall pc_push_test_frame(void* handle) {
    auto* e = static_cast<StudioEngine*>(handle);
    return (e && e->PushSyntheticTestFrame()) ? 1 : 0;
}

} // extern "C"
