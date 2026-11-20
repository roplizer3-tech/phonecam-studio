#include "AiEngine.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

#if defined(PHONECAM_HAS_ONNX)
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#endif

namespace phonecam {

namespace {

bool DirContainsModels(const std::wstring& dir) {
    DWORD a1 = GetFileAttributesW((dir + L"\\selfie_segmentation.onnx").c_str());
    DWORD a2 = GetFileAttributesW((dir + L"\\face_detection_yunet_2023mar.onnx").c_str());
    return (a1 != INVALID_FILE_ATTRIBUTES) || (a2 != INVALID_FILE_ATTRIBUTES);
}

inline float Clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

inline float Ema(float prev, float curr, float alpha) {
    return prev * (1.0f - alpha) + curr * alpha;
}

} // namespace

struct AiEngine::Impl {
#if defined(PHONECAM_HAS_ONNX)
    Ort::Env env{ ORT_LOGGING_LEVEL_WARNING, "PhoneCamStudioAI" };
    std::unique_ptr<Ort::Session> segSession;
    std::unique_ptr<Ort::Session> faceSession;
    std::string segInputName = "pixel_values";
    std::string segOutputName = "alphas";
    std::string faceInputName = "input";
    std::vector<std::string> faceOutputNames;

    std::vector<float> segInputBuf;  // 3 * 256 * 256
    std::vector<float> faceInputBuf; // 3 * 640 * 640
    uint32_t frameCounter = 0;

    bool TryCreateSession(const std::wstring& modelPath, std::unique_ptr<Ort::Session>& outSess, bool& usedDml) {
        DWORD attr = GetFileAttributesW(modelPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) return false;

        // 1. Try DirectML Hardware GPU Execution Provider first
        try {
            Ort::SessionOptions dmlOpts;
            dmlOpts.SetExecutionMode(ORT_SEQUENTIAL);
            dmlOpts.DisableMemPattern();
            dmlOpts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            const OrtApi& ort = Ort::GetApi();
            const OrtDmlApi* dmlApi = nullptr;
            OrtStatus* apiSt = ort.GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dmlApi));
            if (!apiSt && dmlApi) {
                OrtStatus* epSt = dmlApi->SessionOptionsAppendExecutionProvider_DML(dmlOpts, 0);
                if (!epSt) {
                    outSess = std::make_unique<Ort::Session>(env, modelPath.c_str(), dmlOpts);
                    usedDml = true;
                    return true;
                }
                ort.ReleaseStatus(epSt);
            } else if (apiSt) {
                ort.ReleaseStatus(apiSt);
            }
        } catch (...) {
            outSess.reset();
        }

        // 2. Fallback to multi-threaded CPU Execution Provider
        try {
            Ort::SessionOptions cpuOpts;
            cpuOpts.SetIntraOpNumThreads(2);
            cpuOpts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            outSess = std::make_unique<Ort::Session>(env, modelPath.c_str(), cpuOpts);
            usedDml = false;
            return true;
        } catch (...) {
            outSess.reset();
            return false;
        }
    }
#endif
};

AiEngine::AiEngine() : impl_(new Impl()) {
    currentState_.mask256.assign(AiStateSnapshot::kMaskW * AiStateSnapshot::kMaskH, 255);
}

AiEngine::~AiEngine() {
    Shutdown();
    delete impl_;
    impl_ = nullptr;
}

std::wstring AiEngine::FindModelsDirectory() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring base(exePath);
    size_t pos = base.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        std::wstring d = base.substr(0, pos) + L"\\models";
        if (DirContainsModels(d)) return d;
    }

    HMODULE hm = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&AiEngine::FindModelsDirectory),
            &hm) && hm) {
        wchar_t dllPath[MAX_PATH]{};
        GetModuleFileNameW(hm, dllPath, MAX_PATH);
        std::wstring db(dllPath);
        size_t dp = db.find_last_of(L"\\/");
        if (dp != std::wstring::npos) {
            std::wstring d = db.substr(0, dp) + L"\\models";
            if (DirContainsModels(d)) return d;
        }
    }
    return L"models";
}

bool AiEngine::Initialize(const std::wstring& modelsDirOverride) {
    Shutdown();

#if !defined(PHONECAM_HAS_ONNX)
    (void)modelsDirOverride;
    statusSummary_ = "ONNX Runtime not linked";
    return false;
#else
    const std::wstring dir = modelsDirOverride.empty() ? FindModelsDirectory() : modelsDirOverride;
    bool segDml = false, faceDml = false;
    bool segOk = impl_->TryCreateSession(dir + L"\\selfie_segmentation.onnx", impl_->segSession, segDml);
    bool faceOk = impl_->TryCreateSession(dir + L"\\face_detection_yunet_2023mar.onnx", impl_->faceSession, faceDml);

    Ort::AllocatorWithDefaultOptions alloc;
    if (segOk && impl_->segSession) {
        impl_->segInputName = impl_->segSession->GetInputNameAllocated(0, alloc).get();
        impl_->segOutputName = impl_->segSession->GetOutputNameAllocated(0, alloc).get();
        impl_->segInputBuf.resize(3u * 256u * 256u);
    }
    if (faceOk && impl_->faceSession) {
        impl_->faceInputName = impl_->faceSession->GetInputNameAllocated(0, alloc).get();
        const size_t outCount = impl_->faceSession->GetOutputCount();
        impl_->faceOutputNames.clear();
        for (size_t i = 0; i < outCount; ++i) {
            impl_->faceOutputNames.emplace_back(impl_->faceSession->GetOutputNameAllocated(i, alloc).get());
        }
        impl_->faceInputBuf.resize(3u * 640u * 640u);
    }

    modelsLoaded_ = (segOk || faceOk);
    usingDirectML_ = (segDml || faceDml);
    statusSummary_ = modelsLoaded_
        ? (std::string(usingDirectML_ ? "DirectML GPU" : "CPU") +
           " (Seg:" + (segOk ? "OK" : "Missing") +
           ", Face:" + (faceOk ? "OK" : "Missing") + ")")
        : "ONNX models not found";

    if (modelsLoaded_) {
        running_.store(true);
        workerThread_ = std::thread(&AiEngine::WorkerLoop, this);
    }
    return modelsLoaded_;
#endif
}

void AiEngine::Shutdown() {
    running_.store(false);
    jobCv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
#if defined(PHONECAM_HAS_ONNX)
    if (impl_) {
        impl_->segSession.reset();
        impl_->faceSession.reset();
    }
#endif
    modelsLoaded_ = false;
    usingDirectML_ = false;
}

void AiEngine::SubmitFrameAsync(const uint8_t* bgra, uint32_t w, uint32_t h, bool needSeg, bool needFace) {
    if (!modelsLoaded_ || !bgra || w < 32 || h < 32 || (!needSeg && !needFace)) return;

    std::lock_guard<std::mutex> lk(jobMutex_);
    const size_t bytes = static_cast<size_t>(w) * h * 4u;
    pendingBgra_.resize(bytes);
    std::memcpy(pendingBgra_.data(), bgra, bytes);
    pendingW_ = w;
    pendingH_ = h;
    pendingNeedSeg_ = needSeg;
    pendingNeedFace_ = needFace;
    hasPendingJob_ = true;
    jobCv_.notify_one();
}

void AiEngine::GetLatestState(AiStateSnapshot& outSnapshot) {
    std::lock_guard<std::mutex> lk(stateMutex_);
    outSnapshot = currentState_;
}

void AiEngine::WorkerLoop() {
    std::vector<uint8_t> localBgra;
    while (running_.load()) {
        uint32_t w = 0, h = 0;
        bool needSeg = false, needFace = false;
        {
            std::unique_lock<std::mutex> lk(jobMutex_);
            jobCv_.wait(lk, [&]() { return !running_.load() || hasPendingJob_; });
            if (!running_.load()) break;
            localBgra.swap(pendingBgra_);
            w = pendingW_;
            h = pendingH_;
            needSeg = pendingNeedSeg_;
            needFace = pendingNeedFace_;
            hasPendingJob_ = false;
        }
        if (!localBgra.empty() && w >= 32 && h >= 32) {
            ProcessFrameSync(localBgra.data(), w, h, needSeg, needFace);
        }
    }
}

bool AiEngine::ProcessFrameSync(const uint8_t* bgra, uint32_t w, uint32_t h, bool runSeg, bool runFace) {
#if !defined(PHONECAM_HAS_ONNX)
    (void)bgra; (void)w; (void)h; (void)runSeg; (void)runFace;
    return false;
#else
    if (!modelsLoaded_ || !bgra || w < 16 || h < 16) return false;
    bool anyRan = false;
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    ++impl_->frameCounter;

    // 1. Selfie Segmentation (256x256 RGB [0..1])
    if (runSeg && impl_->segSession) {
        constexpr uint32_t MW = 256, MH = 256;
        float* rPlane = impl_->segInputBuf.data();
        float* gPlane = rPlane + MW * MH;
        float* bPlane = gPlane + MW * MH;
        constexpr float inv255 = 1.0f / 255.0f;

        for (uint32_t y = 0; y < MH; ++y) {
            const uint32_t sy = (y * h) / MH;
            const uint8_t* row = bgra + static_cast<size_t>(sy) * w * 4u;
            for (uint32_t x = 0; x < MW; ++x) {
                const uint32_t sx = (x * w) / MW;
                const size_t idx = static_cast<size_t>(y) * MW + x;
                bPlane[idx] = static_cast<float>(row[sx * 4u + 0]) * inv255;
                gPlane[idx] = static_cast<float>(row[sx * 4u + 1]) * inv255;
                rPlane[idx] = static_cast<float>(row[sx * 4u + 2]) * inv255;
            }
        }

        const std::array<int64_t, 4> inShape = { 1, 3, MH, MW };
        Ort::Value inTensor = Ort::Value::CreateTensor<float>(
            memInfo, impl_->segInputBuf.data(), impl_->segInputBuf.size(), inShape.data(), inShape.size());
        const char* inNames[1] = { impl_->segInputName.c_str() };
        const char* outNames[1] = { impl_->segOutputName.c_str() };

        try {
            auto outs = impl_->segSession->Run(Ort::RunOptions{ nullptr }, inNames, &inTensor, 1, outNames, 1);
            if (!outs.empty() && outs[0].IsTensor()) {
                const float* alphaPtr = outs[0].GetTensorData<float>();
                const size_t count = outs[0].GetTensorTypeAndShapeInfo().GetElementCount();
                if (count >= MW * MH && alphaPtr) {
                    std::lock_guard<std::mutex> lk(stateMutex_);
                    if (currentState_.mask256.size() != MW * MH) {
                        currentState_.mask256.assign(MW * MH, 255);
                    }
                    const float smoothAlpha = currentState_.hasValidMask ? 0.58f : 1.0f;
                    for (size_t i = 0; i < MW * MH; ++i) {
                        const float v = Clamp01(alphaPtr[i]);
                        const float prev = static_cast<float>(currentState_.mask256[i]) * inv255;
                        const float blended = Ema(prev, v, smoothAlpha);
                        currentState_.mask256[i] = static_cast<uint8_t>(std::clamp(blended * 255.0f, 0.0f, 255.0f));
                    }
                    currentState_.hasValidMask = true;
                    currentState_.usingDirectML = usingDirectML_;
                    currentState_.modelsLoaded = modelsLoaded_;
                    anyRan = true;
                }
            }
        } catch (...) {
        }
    }

    // 2. YuNet Face & 5-Landmark Detection (640x640 BGR [0..255])
    if (runFace && impl_->faceSession && impl_->faceOutputNames.size() == 12) {
        constexpr uint32_t FW = 640, FH = 640;
        float* bPlane = impl_->faceInputBuf.data();
        float* gPlane = bPlane + FW * FH;
        float* rPlane = gPlane + FW * FH;

        for (uint32_t y = 0; y < FH; ++y) {
            const uint32_t sy = (y * h) / FH;
            const uint8_t* row = bgra + static_cast<size_t>(sy) * w * 4u;
            for (uint32_t x = 0; x < FW; ++x) {
                const uint32_t sx = (x * w) / FW;
                const size_t idx = static_cast<size_t>(y) * FW + x;
                bPlane[idx] = static_cast<float>(row[sx * 4u + 0]);
                gPlane[idx] = static_cast<float>(row[sx * 4u + 1]);
                rPlane[idx] = static_cast<float>(row[sx * 4u + 2]);
            }
        }

        const std::array<int64_t, 4> inShape = { 1, 3, FH, FW };
        Ort::Value inTensor = Ort::Value::CreateTensor<float>(
            memInfo, impl_->faceInputBuf.data(), impl_->faceInputBuf.size(), inShape.data(), inShape.size());

        const char* inNames[1] = { impl_->faceInputName.c_str() };
        std::array<const char*, 12> outNames{};
        for (size_t i = 0; i < 12; ++i) outNames[i] = impl_->faceOutputNames[i].c_str();

        try {
            auto outs = impl_->faceSession->Run(Ort::RunOptions{ nullptr }, inNames, &inTensor, 1, outNames.data(), 12);
            if (outs.size() == 12) {
                constexpr int kStrides[3] = { 8, 16, 32 };
                AiFaceResult best{};
                float bestScore = 0.55f;

                for (int si = 0; si < 3; ++si) {
                    const int stride = kStrides[si];
                    const int cols = 640 / stride;
                    const int rows = 640 / stride;
                    const float* clsV = outs[si].GetTensorData<float>();
                    const float* objV = outs[si + 3].GetTensorData<float>();
                    const float* bboxV = outs[si + 6].GetTensorData<float>();
                    const float* kpsV = outs[si + 9].GetTensorData<float>();

                    for (int r = 0; r < rows; ++r) {
                        for (int c = 0; c < cols; ++c) {
                            const size_t idx = static_cast<size_t>(r) * cols + c;
                            const float cs = Clamp01(clsV[idx]);
                            const float os = Clamp01(objV[idx]);
                            const float score = std::sqrt(cs * os);
                            if (score > bestScore) {
                                bestScore = score;
                                const float cx = (c + bboxV[idx * 4 + 0]) * stride / 640.0f;
                                const float cy = (r + bboxV[idx * 4 + 1]) * stride / 640.0f;
                                const float bw = std::exp(std::clamp(bboxV[idx * 4 + 2], -4.0f, 4.0f)) * stride / 640.0f;
                                const float bh = std::exp(std::clamp(bboxV[idx * 4 + 3], -4.0f, 4.0f)) * stride / 640.0f;

                                best.detected = true;
                                best.confidence = score;
                                best.boxX = Clamp01(cx - bw * 0.5f);
                                best.boxY = Clamp01(cy - bh * 0.5f);
                                best.boxW = Clamp01(bw);
                                best.boxH = Clamp01(bh);
                                best.rightEyeX = Clamp01((kpsV[idx * 10 + 0] + c) * stride / 640.0f);
                                best.rightEyeY = Clamp01((kpsV[idx * 10 + 1] + r) * stride / 640.0f);
                                best.leftEyeX = Clamp01((kpsV[idx * 10 + 2] + c) * stride / 640.0f);
                                best.leftEyeY = Clamp01((kpsV[idx * 10 + 3] + r) * stride / 640.0f);
                                best.noseX = Clamp01((kpsV[idx * 10 + 4] + c) * stride / 640.0f);
                                best.noseY = Clamp01((kpsV[idx * 10 + 5] + r) * stride / 640.0f);
                                best.rightMouthX = Clamp01((kpsV[idx * 10 + 6] + c) * stride / 640.0f);
                                best.rightMouthY = Clamp01((kpsV[idx * 10 + 7] + r) * stride / 640.0f);
                                best.leftMouthX = Clamp01((kpsV[idx * 10 + 8] + c) * stride / 640.0f);
                                best.leftMouthY = Clamp01((kpsV[idx * 10 + 9] + r) * stride / 640.0f);
                                best.framesSinceSeen = 0;
                            }
                        }
                    }
                }

                std::lock_guard<std::mutex> lk(stateMutex_);
                if (best.detected) {
                    const float a = currentState_.face.detected ? 0.42f : 1.0f;
                    auto& f = currentState_.face;
                    f.detected = true;
                    f.confidence = best.confidence;
                    f.framesSinceSeen = 0;
                    f.boxX = Ema(f.boxX, best.boxX, a);
                    f.boxY = Ema(f.boxY, best.boxY, a);
                    f.boxW = Ema(f.boxW, best.boxW, a);
                    f.boxH = Ema(f.boxH, best.boxH, a);
                    f.rightEyeX = Ema(f.rightEyeX, best.rightEyeX, a);
                    f.rightEyeY = Ema(f.rightEyeY, best.rightEyeY, a);
                    f.leftEyeX = Ema(f.leftEyeX, best.leftEyeX, a);
                    f.leftEyeY = Ema(f.leftEyeY, best.leftEyeY, a);
                    f.noseX = Ema(f.noseX, best.noseX, a);
                    f.noseY = Ema(f.noseY, best.noseY, a);
                    f.rightMouthX = Ema(f.rightMouthX, best.rightMouthX, a);
                    f.rightMouthY = Ema(f.rightMouthY, best.rightMouthY, a);
                    f.leftMouthX = Ema(f.leftMouthX, best.leftMouthX, a);
                    f.leftMouthY = Ema(f.leftMouthY, best.leftMouthY, a);
                } else {
                    currentState_.face.framesSinceSeen++;
                    if (currentState_.face.framesSinceSeen > 15) {
                        currentState_.face.detected = false;
                    }
                }
                currentState_.usingDirectML = usingDirectML_;
                currentState_.modelsLoaded = modelsLoaded_;
                anyRan = true;
            }
        } catch (...) {
        }
    }

    return anyRan;
#endif
}

} // namespace phonecam
