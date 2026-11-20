#pragma once

#include <cstdint>
#include <vector>

#include <d3d11.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace phonecam {

struct DecodedFrame {
    bool isBgra = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    std::vector<uint8_t> data; // NV12 packed (width*height*3/2) or BGRA32 (width*height*4)
};

class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool Initialize();
    void Shutdown();

    bool IsUsingHardwareDecode() const { return usingDxva_; }

    bool DecodeH264(const uint8_t* annexB, size_t size, uint64_t timestampMs, DecodedFrame& outFrame);
    bool DecodeMjpeg(const uint8_t* jpegData, size_t size, DecodedFrame& outFrame);
    void ResetH264();

private:
    bool InitDxvaDevice();
    bool InitH264Mft(bool allowDxva = true);
    bool NegotiateH264OutputType();
    bool ReadH264OutputSample(IMFSample* sample, DecodedFrame& outFrame);
    bool CopyDxgiSurfaceToNv12(ID3D11Texture2D* tex, UINT subresourceIdx, DecodedFrame& outFrame);

    Microsoft::WRL::ComPtr<ID3D11Device> dxvaDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> dxvaCtx_;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> dxgiManager_;
    UINT dxgiResetToken_ = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> dxvaStagingTex_;
    uint32_t dxvaStagingW_ = 0;
    uint32_t dxvaStagingH_ = 0;
    DXGI_FORMAT dxvaStagingFmt_ = DXGI_FORMAT_UNKNOWN;

    Microsoft::WRL::ComPtr<IMFTransform> h264Mft_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory_;
    bool usingDxva_ = false;
    bool h264OutputReady_ = false;
    bool mftProvidesSamples_ = false;
    DWORD mftOutputBufferSize_ = 0;

    uint32_t codedW_ = 1280;
    uint32_t codedH_ = 720;
    uint32_t displayW_ = 1280;
    uint32_t displayH_ = 720;
    INT32 defaultStride_ = 1280;
};

} // namespace phonecam
