#pragma once

#include "AiEngine.h"
#include "EngineApi.h"
#include "../common/SharedFrameBuffer.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace phonecam {

enum class InputPixelFormat {
    NV12 = 0,
    BGRA32 = 1
};

struct RawFrameInput {
    InputPixelFormat format = InputPixelFormat::NV12;
    const uint8_t* data = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t strideY = 0;   // Used for NV12 or BGRA row stride
    uint32_t strideUV = 0;
    const uint8_t* planeUV = nullptr; // Optional separate UV pointer
    int sensorRotationDeg = 0;
};

struct ProcessedFrameOutput {
    std::vector<uint8_t> nv12;
    std::vector<uint8_t> previewBgra;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t previewWidth = 0;
    uint32_t previewHeight = 0;
};

class GpuProcessor {
public:
    GpuProcessor() = default;
    ~GpuProcessor();

    GpuProcessor(const GpuProcessor&) = delete;
    GpuProcessor& operator=(const GpuProcessor&) = delete;

    bool Initialize(bool forceWarp = false);
    void Shutdown();

    bool IsInitialized() const { return device_ != nullptr; }
    bool IsWarp() const { return isWarp_; }
    const std::string& LastError() const { return lastError_; }

    void SetAiEngine(AiEngine* aiEngine);
    bool SetCustomBackgroundPath(const std::string& utf8Path);

    bool ProcessFrame(
        const RawFrameInput& input,
        const PC_VideoParams& params,
        bool producePreview,
        ProcessedFrameOutput& output,
        const AiStateSnapshot* overrideAi = nullptr);

private:
    bool CompileShaders();
    bool EnsureInputTextures(uint32_t w, uint32_t h);
    bool EnsureOutputTextures(uint32_t w, uint32_t h);
    bool EnsureAiResources();
    void UploadMaskIfNeeded(const AiStateSnapshot& ai, int bgMode);
    void SubmitAsyncAiExtractIfNeeded(const PC_VideoParams& params, bool needAwbSample);
    void UpdateCenterStage(const PC_VideoParams& params, const AiStateSnapshot& ai);
    void UpdateAutoWhiteBalance(const uint8_t* bgra, uint32_t w, uint32_t h);
    void DrawPass(
        ID3D11PixelShader* ps,
        ID3D11RenderTargetView* const* rtvs,
        uint32_t numRtvs,
        uint32_t vpW,
        uint32_t vpH,
        ID3D11ShaderResourceView* const* srvs,
        uint32_t numSrvs,
        ID3D11Buffer* cb);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerLinear_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> fullscreenVS_;

    Microsoft::WRL::ComPtr<ID3D11PixelShader> psGeom_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psBeauty_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psGrade_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psToY_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psToUV_;

    Microsoft::WRL::ComPtr<ID3D11Buffer> cbGeom_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cbBeauty_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cbGrade_;

    // Source textures
    uint32_t srcTexW_ = 0;
    uint32_t srcTexH_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> srcYTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srcYSrv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> srcUVTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srcUVSrv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> srcBgraTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srcBgraSrv_;

    // Output-sized intermediate & 2-slot alternating staging textures
    uint32_t outTexW_ = 0;
    uint32_t outTexH_ = 0;
    uint32_t previewW_ = 0;
    uint32_t previewH_ = 0;
    int stagingIdx_ = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> rtGeomTex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtGeomRtv_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> rtGeomSrv_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> rtBeautyTex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtBeautyRtv_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> rtBeautySrv_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> rtGradeOutTex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtGradeOutRtv_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> rtGradeOutSrv_;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> rtPreviewTex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtPreviewRtv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingPreviewTex_[2];

    Microsoft::WRL::ComPtr<ID3D11Texture2D> rtYTex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtYRtv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingYTex_[2];

    Microsoft::WRL::ComPtr<ID3D11Texture2D> rtUVTex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtUVRtv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingUVTex_[2];

    // AI mask (256x256 R8), custom background (BGRA), & async 480x270 readback ring
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texMask_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texMaskSrv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texCustomBg_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texCustomBgSrv_;
    bool hasCustomBg_ = false;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> rtAiTex_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtAiRtv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingAiTex_[2];
    int aiStagingIdx_ = 0;
    bool aiRingWarmed_ = false;
    std::vector<uint8_t> aiExtractBuf_;

    AiEngine* aiEngine_ = nullptr;
    float autoZoom_ = 1.0f;
    float autoPanX_ = 0.0f;
    float autoPanY_ = 0.0f;

    bool isWarp_ = false;
    std::string lastError_;
    float awbGainR_ = 1.0f;
    float awbGainG_ = 1.0f;
    float awbGainB_ = 1.0f;
    std::chrono::steady_clock::time_point startTime_;
    std::mutex mutex_;
};

} // namespace phonecam
