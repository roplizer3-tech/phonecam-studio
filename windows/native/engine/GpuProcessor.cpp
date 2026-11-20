#include "GpuProcessor.h"
#include "GeneratedShaders.h"

#include <d3dcompiler.h>
#include <objbase.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace phonecam {

namespace {

constexpr uint32_t kAiExtractW = 480;
constexpr uint32_t kAiExtractH = 270;

struct alignas(16) GeomCBData {
    float srcWidth;
    float srcHeight;
    float outWidth;
    float outHeight;
    int32_t inputMode;
    int32_t rotationDeg;
    int32_t flipH;
    int32_t flipV;
    int32_t framingMode;
    float zoom;
    float panX;
    float panY;
};

struct alignas(16) BeautyCBData {
    float texWidth;
    float texHeight;
    float skinSmooth;
    float denoise;
    float studioLight;
    float faceSlim;
    float eyeEnlarge;
    float faceDetected;
    float faceCenterX;
    float faceCenterY;
    float faceScale;
    float pad0;
    float rightEyeX;
    float rightEyeY;
    float leftEyeX;
    float leftEyeY;
};

struct alignas(16) GradeCBData {
    float texWidth;
    float texHeight;
    float exposure;
    float brightness;

    float contrast;
    float highlights;
    float shadows;
    float gammaVal;

    float temperature;
    float tint;
    float saturation;
    float vibrance;

    float sharpen;
    float softGlow;
    float vignette;
    float awbStrength;

    float awbGainR;
    float awbGainG;
    float awbGainB;
    float mirrorCheck;

    float isPreview;
    float splitPos;
    float bypassAll;
    float previewFlipH;

    int32_t bgMode;
    int32_t arFilterMode;
    int32_t styleFilterMode;
    float bgBlurStrength;

    float eyeBrighten;
    float teethWhiten;
    float styleIntensity;
    float timeSec;

    float hasCustomBg;
    float hasAiMask;
    float faceDetected;
    float faceConf;

    float rightEyeX;
    float rightEyeY;
    float leftEyeX;
    float leftEyeY;

    float noseX;
    float noseY;
    float mouthCenterX;
    float mouthCenterY;

    float faceBoxX;
    float faceBoxY;
    float faceBoxW;
    float faceBoxH;
};

static_assert(sizeof(GeomCBData) == 48, "GeomCBData size mismatch");
static_assert(sizeof(BeautyCBData) == 64, "BeautyCBData size mismatch");
static_assert(sizeof(GradeCBData) == 192, "GradeCBData size mismatch");

bool CompileShaderFromMemory(
    const char* source,
    const char* name,
    const char* entryPoint,
    const char* target,
    ComPtr<ID3DBlob>& outBlob,
    std::string& outErr) {
    ComPtr<ID3DBlob> errBlob;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    HRESULT hr = D3DCompile(
        source,
        std::strlen(source),
        name,
        nullptr,
        nullptr,
        entryPoint,
        target,
        flags,
        0,
        &outBlob,
        &errBlob);
    if (FAILED(hr)) {
        if (errBlob) {
            outErr = std::string(static_cast<const char*>(errBlob->GetBufferPointer()), errBlob->GetBufferSize());
        } else {
            outErr = std::string("D3DCompile failed for ") + name;
        }
        return false;
    }
    return true;
}

bool CreateConstBuffer(ID3D11Device* dev, UINT byteWidth, ComPtr<ID3D11Buffer>& outBuf) {
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = byteWidth;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &outBuf));
}

bool CreateRenderTargetTex(
    ID3D11Device* dev,
    UINT w,
    UINT h,
    DXGI_FORMAT fmt,
    ComPtr<ID3D11Texture2D>& outTex,
    ComPtr<ID3D11RenderTargetView>& outRtv,
    ComPtr<ID3D11ShaderResourceView>* outSrv) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | (outSrv ? D3D11_BIND_SHADER_RESOURCE : 0u);
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &outTex))) return false;
    if (FAILED(dev->CreateRenderTargetView(outTex.Get(), nullptr, &outRtv))) return false;
    if (outSrv) {
        if (FAILED(dev->CreateShaderResourceView(outTex.Get(), nullptr, outSrv->GetAddressOf()))) return false;
    }
    return true;
}

bool CreateStagingTex(ID3D11Device* dev, UINT w, UINT h, DXGI_FORMAT fmt, ComPtr<ID3D11Texture2D>& outTex) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    return SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &outTex));
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (needed <= 1) return {};
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), needed);
    return out;
}

} // namespace

GpuProcessor::~GpuProcessor() {
    Shutdown();
}

void GpuProcessor::SetAiEngine(AiEngine* aiEngine) {
    std::lock_guard<std::mutex> lock(mutex_);
    aiEngine_ = aiEngine;
}

bool GpuProcessor::Initialize(bool forceWarp) {
    std::lock_guard<std::mutex> lock(mutex_);
    Shutdown();

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL chosen{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    HRESULT hr = E_FAIL;
    if (!forceWarp) {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            levels,
            _countof(levels),
            D3D11_SDK_VERSION,
            &device_,
            &chosen,
            &ctx_);
        isWarp_ = false;
    }
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            flags,
            levels,
            _countof(levels),
            D3D11_SDK_VERSION,
            &device_,
            &chosen,
            &ctx_);
        isWarp_ = true;
    }
    if (FAILED(hr)) {
        lastError_ = "Failed to initialize Direct3D 11 device";
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&sd, &samplerLinear_))) {
        lastError_ = "Failed creating sampler state";
        Shutdown();
        return false;
    }

    if (!CreateConstBuffer(device_.Get(), sizeof(GeomCBData), cbGeom_) ||
        !CreateConstBuffer(device_.Get(), sizeof(BeautyCBData), cbBeauty_) ||
        !CreateConstBuffer(device_.Get(), sizeof(GradeCBData), cbGrade_)) {
        lastError_ = "Failed creating constant buffers";
        Shutdown();
        return false;
    }

    if (!CompileShaders() || !EnsureAiResources()) {
        Shutdown();
        return false;
    }

    startTime_ = std::chrono::steady_clock::now();
    return true;
}

void GpuProcessor::Shutdown() {
    srcTexW_ = srcTexH_ = 0;
    outTexW_ = outTexH_ = 0;
    previewW_ = previewH_ = 0;
    stagingIdx_ = 0;
    aiStagingIdx_ = 0;
    aiRingWarmed_ = false;
    hasCustomBg_ = false;
    autoZoom_ = 1.0f;
    autoPanX_ = 0.0f;
    autoPanY_ = 0.0f;

    srcYSrv_.Reset(); srcYTex_.Reset();
    srcUVSrv_.Reset(); srcUVTex_.Reset();
    srcBgraSrv_.Reset(); srcBgraTex_.Reset();
    rtGeomSrv_.Reset(); rtGeomRtv_.Reset(); rtGeomTex_.Reset();
    rtBeautySrv_.Reset(); rtBeautyRtv_.Reset(); rtBeautyTex_.Reset();
    rtGradeOutSrv_.Reset(); rtGradeOutRtv_.Reset(); rtGradeOutTex_.Reset();
    rtPreviewRtv_.Reset(); rtPreviewTex_.Reset();
    stagingPreviewTex_[0].Reset(); stagingPreviewTex_[1].Reset();
    rtYRtv_.Reset(); rtYTex_.Reset();
    stagingYTex_[0].Reset(); stagingYTex_[1].Reset();
    rtUVRtv_.Reset(); rtUVTex_.Reset();
    stagingUVTex_[0].Reset(); stagingUVTex_[1].Reset();
    texMaskSrv_.Reset(); texMask_.Reset();
    texCustomBgSrv_.Reset(); texCustomBg_.Reset();
    rtAiRtv_.Reset(); rtAiTex_.Reset();
    stagingAiTex_[0].Reset(); stagingAiTex_[1].Reset();

    cbGeom_.Reset(); cbBeauty_.Reset(); cbGrade_.Reset();
    psGeom_.Reset(); psBeauty_.Reset(); psGrade_.Reset(); psToY_.Reset(); psToUV_.Reset();
    fullscreenVS_.Reset();
    samplerLinear_.Reset();
    ctx_.Reset();
    device_.Reset();
}

bool GpuProcessor::CompileShaders() {
    ComPtr<ID3DBlob> blob;
    if (!CompileShaderFromMemory(shaders::kFullscreenVS, "fullscreen_vs.hlsl", "main", "vs_5_0", blob, lastError_) ||
        FAILED(device_->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &fullscreenVS_))) {
        return false;
    }

    if (!CompileShaderFromMemory(shaders::kPass1GeomPS, "pass1_geom_ps.hlsl", "main", "ps_5_0", blob, lastError_) ||
        FAILED(device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &psGeom_))) {
        return false;
    }

    if (!CompileShaderFromMemory(shaders::kPass2BeautyPS, "pass2_beauty_ps.hlsl", "main", "ps_5_0", blob, lastError_) ||
        FAILED(device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &psBeauty_))) {
        return false;
    }

    if (!CompileShaderFromMemory(shaders::kPass3GradePS, "pass3_grade_ps.hlsl", "main", "ps_5_0", blob, lastError_) ||
        FAILED(device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &psGrade_))) {
        return false;
    }

    if (!CompileShaderFromMemory(shaders::kPass4Nv12PS, "pass4_nv12_ps.hlsl", "PS_ToY", "ps_5_0", blob, lastError_) ||
        FAILED(device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &psToY_))) {
        return false;
    }

    if (!CompileShaderFromMemory(shaders::kPass4Nv12PS, "pass4_nv12_ps.hlsl", "PS_ToUV", "ps_5_0", blob, lastError_) ||
        FAILED(device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &psToUV_))) {
        return false;
    }

    return true;
}

bool GpuProcessor::EnsureAiResources() {
    if (texMask_ && texCustomBg_ && rtAiTex_) return true;

    // 256x256 dynamic R8 AI segmentation mask
    D3D11_TEXTURE2D_DESC md{};
    md.Width = 256;
    md.Height = 256;
    md.MipLevels = 1;
    md.ArraySize = 1;
    md.Format = DXGI_FORMAT_R8_UNORM;
    md.SampleDesc.Count = 1;
    md.Usage = D3D11_USAGE_DYNAMIC;
    md.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    md.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    std::vector<uint8_t> initMask(256u * 256u, 255u);
    D3D11_SUBRESOURCE_DATA initData{ initMask.data(), 256u, 0u };
    if (FAILED(device_->CreateTexture2D(&md, &initData, &texMask_)) ||
        FAILED(device_->CreateShaderResourceView(texMask_.Get(), nullptr, &texMaskSrv_))) {
        return false;
    }

    // 1x1 fallback custom background texture so SRV t3 is always valid
    D3D11_TEXTURE2D_DESC bd{};
    bd.Width = 1;
    bd.Height = 1;
    bd.MipLevels = 1;
    bd.ArraySize = 1;
    bd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bd.SampleDesc.Count = 1;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const uint8_t blackPx[4] = { 24, 20, 18, 255 };
    D3D11_SUBRESOURCE_DATA bgData{ blackPx, 4u, 0u };
    if (FAILED(device_->CreateTexture2D(&bd, &bgData, &texCustomBg_)) ||
        FAILED(device_->CreateShaderResourceView(texCustomBg_.Get(), nullptr, &texCustomBgSrv_))) {
        return false;
    }

    if (!CreateRenderTargetTex(device_.Get(), kAiExtractW, kAiExtractH, DXGI_FORMAT_B8G8R8A8_UNORM, rtAiTex_, rtAiRtv_, nullptr)) {
        return false;
    }
    if (!CreateStagingTex(device_.Get(), kAiExtractW, kAiExtractH, DXGI_FORMAT_B8G8R8A8_UNORM, stagingAiTex_[0]) ||
        !CreateStagingTex(device_.Get(), kAiExtractW, kAiExtractH, DXGI_FORMAT_B8G8R8A8_UNORM, stagingAiTex_[1])) {
        return false;
    }

    aiExtractBuf_.resize(static_cast<size_t>(kAiExtractW) * kAiExtractH * 4u);
    return true;
}

bool GpuProcessor::SetCustomBackgroundPath(const std::string& utf8Path) {
    if (utf8Path.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        hasCustomBg_ = false;
        return true;
    }

    const std::wstring widePath = Utf8ToWide(utf8Path);
    if (widePath.empty()) return false;

    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool needUninit = (hrCo == S_OK || hrCo == S_FALSE);

    uint32_t imgW = 0, imgH = 0;
    std::vector<uint8_t> bgra;

    {
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;

        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame))) {
            UINT w = 0, h = 0;
            if (SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0) {
                IWICBitmapSource* source = frame.Get();
                ComPtr<IWICBitmapScaler> scaler;
                if (w > 1920 || h > 1080) {
                    const double scale = std::min(1920.0 / w, 1080.0 / h);
                    UINT nw = std::max<UINT>(1u, static_cast<UINT>(std::lround(w * scale)));
                    UINT nh = std::max<UINT>(1u, static_cast<UINT>(std::lround(h * scale)));
                    if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
                        SUCCEEDED(scaler->Initialize(frame.Get(), nw, nh, WICBitmapInterpolationModeFant))) {
                        w = nw;
                        h = nh;
                        source = scaler.Get();
                    }
                }
                if (SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                    SUCCEEDED(converter->Initialize(source, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                    imgW = w;
                    imgH = h;
                    bgra.resize(static_cast<size_t>(imgW) * imgH * 4u);
                    if (FAILED(converter->CopyPixels(nullptr, imgW * 4u, static_cast<UINT>(bgra.size()), bgra.data()))) {
                        bgra.clear();
                    }
                }
            }
        }
    }

    if (needUninit) CoUninitialize();
    if (bgra.empty() || imgW == 0 || imgH == 0) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!device_) return false;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = imgW;
    td.Height = imgH;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initData{ bgra.data(), imgW * 4u, 0u };

    ComPtr<ID3D11Texture2D> newTex;
    ComPtr<ID3D11ShaderResourceView> newSrv;
    if (FAILED(device_->CreateTexture2D(&td, &initData, &newTex)) ||
        FAILED(device_->CreateShaderResourceView(newTex.Get(), nullptr, &newSrv))) {
        return false;
    }

    texCustomBg_ = newTex;
    texCustomBgSrv_ = newSrv;
    hasCustomBg_ = true;
    return true;
}

bool GpuProcessor::EnsureInputTextures(uint32_t w, uint32_t h) {
    if (srcTexW_ == w && srcTexH_ == h && srcYTex_ && srcUVTex_ && srcBgraTex_) {
        return true;
    }
    srcYSrv_.Reset(); srcYTex_.Reset();
    srcUVSrv_.Reset(); srcUVTex_.Reset();
    srcBgraSrv_.Reset(); srcBgraTex_.Reset();

    auto makeDynamic = [&](UINT tw, UINT th, DXGI_FORMAT fmt, ComPtr<ID3D11Texture2D>& tex, ComPtr<ID3D11ShaderResourceView>& srv) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = tw;
        td.Height = th;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = fmt;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &tex))) return false;
        return SUCCEEDED(device_->CreateShaderResourceView(tex.Get(), nullptr, &srv));
    };

    if (!makeDynamic(w, h, DXGI_FORMAT_R8_UNORM, srcYTex_, srcYSrv_)) return false;
    if (!makeDynamic(w / 2, h / 2, DXGI_FORMAT_R8G8_UNORM, srcUVTex_, srcUVSrv_)) return false;
    if (!makeDynamic(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, srcBgraTex_, srcBgraSrv_)) return false;

    srcTexW_ = w;
    srcTexH_ = h;
    return true;
}

bool GpuProcessor::EnsureOutputTextures(uint32_t w, uint32_t h) {
    if (outTexW_ == w && outTexH_ == h && rtGeomTex_ && rtYTex_) {
        return true;
    }
    rtGeomSrv_.Reset(); rtGeomRtv_.Reset(); rtGeomTex_.Reset();
    rtBeautySrv_.Reset(); rtBeautyRtv_.Reset(); rtBeautyTex_.Reset();
    rtGradeOutSrv_.Reset(); rtGradeOutRtv_.Reset(); rtGradeOutTex_.Reset();
    rtPreviewRtv_.Reset(); rtPreviewTex_.Reset();
    stagingPreviewTex_[0].Reset(); stagingPreviewTex_[1].Reset();
    rtYRtv_.Reset(); rtYTex_.Reset();
    stagingYTex_[0].Reset(); stagingYTex_[1].Reset();
    rtUVRtv_.Reset(); rtUVTex_.Reset();
    stagingUVTex_[0].Reset(); stagingUVTex_[1].Reset();

    previewW_ = std::min(w, 640u);
    previewH_ = std::max(2u, ((h * previewW_) / std::max(w, 1u)) & ~1u);

    if (!CreateRenderTargetTex(device_.Get(), w, h, DXGI_FORMAT_B8G8R8A8_UNORM, rtGeomTex_, rtGeomRtv_, &rtGeomSrv_)) return false;
    if (!CreateRenderTargetTex(device_.Get(), w, h, DXGI_FORMAT_B8G8R8A8_UNORM, rtBeautyTex_, rtBeautyRtv_, &rtBeautySrv_)) return false;
    if (!CreateRenderTargetTex(device_.Get(), w, h, DXGI_FORMAT_B8G8R8A8_UNORM, rtGradeOutTex_, rtGradeOutRtv_, &rtGradeOutSrv_)) return false;
    if (!CreateRenderTargetTex(device_.Get(), previewW_, previewH_, DXGI_FORMAT_B8G8R8A8_UNORM, rtPreviewTex_, rtPreviewRtv_, nullptr)) return false;

    if (!CreateStagingTex(device_.Get(), previewW_, previewH_, DXGI_FORMAT_B8G8R8A8_UNORM, stagingPreviewTex_[0]) ||
        !CreateStagingTex(device_.Get(), previewW_, previewH_, DXGI_FORMAT_B8G8R8A8_UNORM, stagingPreviewTex_[1])) {
        return false;
    }

    if (!CreateRenderTargetTex(device_.Get(), w, h, DXGI_FORMAT_R8_UNORM, rtYTex_, rtYRtv_, nullptr)) return false;
    if (!CreateStagingTex(device_.Get(), w, h, DXGI_FORMAT_R8_UNORM, stagingYTex_[0]) ||
        !CreateStagingTex(device_.Get(), w, h, DXGI_FORMAT_R8_UNORM, stagingYTex_[1])) {
        return false;
    }

    if (!CreateRenderTargetTex(device_.Get(), w / 2, h / 2, DXGI_FORMAT_R8G8_UNORM, rtUVTex_, rtUVRtv_, nullptr)) return false;
    if (!CreateStagingTex(device_.Get(), w / 2, h / 2, DXGI_FORMAT_R8G8_UNORM, stagingUVTex_[0]) ||
        !CreateStagingTex(device_.Get(), w / 2, h / 2, DXGI_FORMAT_R8G8_UNORM, stagingUVTex_[1])) {
        return false;
    }

    outTexW_ = w;
    outTexH_ = h;
    return true;
}

void GpuProcessor::DrawPass(
    ID3D11PixelShader* ps,
    ID3D11RenderTargetView* const* rtvs,
    uint32_t numRtvs,
    uint32_t vpW,
    uint32_t vpH,
    ID3D11ShaderResourceView* const* srvs,
    uint32_t numSrvs,
    ID3D11Buffer* cb) {
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(vpW);
    vp.Height = static_cast<float>(vpH);
    vp.MaxDepth = 1.0f;

    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(fullscreenVS_.Get(), nullptr, 0);
    ctx_->PSSetShader(ps, nullptr, 0);
    ctx_->PSSetSamplers(0, 1, samplerLinear_.GetAddressOf());
    ctx_->PSSetConstantBuffers(0, cb ? 1u : 0u, cb ? &cb : nullptr);
    ctx_->PSSetShaderResources(0, numSrvs, srvs);
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(numRtvs, rtvs, nullptr);
    ctx_->Draw(3, 0);

    ID3D11ShaderResourceView* nulls[4]{};
    ctx_->PSSetShaderResources(0, numSrvs, nulls);
    ID3D11RenderTargetView* nullRtv = nullptr;
    ctx_->OMSetRenderTargets(1, &nullRtv, nullptr);
}

void GpuProcessor::UploadMaskIfNeeded(const AiStateSnapshot& ai, int bgMode) {
    if (bgMode <= 0 || !ai.hasValidMask || ai.mask256.size() != 256u * 256u) {
        return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(ctx_->Map(texMask_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        const uint8_t* src = ai.mask256.data();
        uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
        for (uint32_t r = 0; r < 256u; ++r) {
            std::memcpy(dst + static_cast<size_t>(r) * mapped.RowPitch, src + static_cast<size_t>(r) * 256u, 256u);
        }
        ctx_->Unmap(texMask_.Get(), 0);
    }
}

void GpuProcessor::SubmitAsyncAiExtractIfNeeded(const PC_VideoParams& params, bool needAwbSample) {
    const bool needSeg = (!params.bypassEffects && params.bgMode > 0);
    const bool needFace = (!params.bypassEffects && (
        params.arFilterMode > 0 ||
        params.autoFrameEnabled != 0 ||
        params.faceSlim > 0.01f ||
        params.eyeEnlarge > 0.01f ||
        params.eyeBrighten > 0.01f ||
        params.teethWhiten > 0.01f));

    if (!(aiEngine_ && (needSeg || needFace)) && !needAwbSample) {
        aiRingWarmed_ = false;
        return;
    }

    // Read previous frame's 480x270 extract non-blockingly so GPU pipeline never stalls
    if (aiRingWarmed_) {
        const int readSlot = aiStagingIdx_ ^ 1;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx_->Map(stagingAiTex_[readSlot].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
            for (uint32_t r = 0; r < kAiExtractH; ++r) {
                std::memcpy(aiExtractBuf_.data() + static_cast<size_t>(r) * kAiExtractW * 4u,
                            static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(r) * mapped.RowPitch,
                            static_cast<size_t>(kAiExtractW) * 4u);
            }
            ctx_->Unmap(stagingAiTex_[readSlot].Get(), 0);

            if (needAwbSample) {
                UpdateAutoWhiteBalance(aiExtractBuf_.data(), kAiExtractW, kAiExtractH);
            }
            if (aiEngine_ && (needSeg || needFace)) {
                aiEngine_->SubmitFrameAsync(aiExtractBuf_.data(), kAiExtractW, kAiExtractH, needSeg, needFace);
            }
        }
    }

    // Downscale rtGeomTex_ (already oriented) -> rtAiTex_ (480x270) via psGeom_ inputMode=1
    GeomCBData copyCb{};
    copyCb.srcWidth = 1.0f;
    copyCb.srcHeight = 1.0f;
    copyCb.outWidth = 1.0f;
    copyCb.outHeight = 1.0f;
    copyCb.inputMode = 1;
    copyCb.zoom = 1.0f;

    D3D11_MAPPED_SUBRESOURCE ms{};
    if (SUCCEEDED(ctx_->Map(cbGeom_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        std::memcpy(ms.pData, &copyCb, sizeof(copyCb));
        ctx_->Unmap(cbGeom_.Get(), 0);
    }

    ID3D11ShaderResourceView* srvs[3] = { nullptr, nullptr, rtGeomSrv_.Get() };
    DrawPass(psGeom_.Get(), rtAiRtv_.GetAddressOf(), 1, kAiExtractW, kAiExtractH, srvs, 3, cbGeom_.Get());
    ctx_->CopyResource(stagingAiTex_[aiStagingIdx_].Get(), rtAiTex_.Get());

    aiStagingIdx_ ^= 1;
    aiRingWarmed_ = true;
}

void GpuProcessor::UpdateCenterStage(const PC_VideoParams& params, const AiStateSnapshot& ai) {
    if (!params.bypassEffects && params.autoFrameEnabled != 0 && ai.face.detected) {
        const float targetZoom = std::clamp(0.42f / std::max(ai.face.boxH, 0.14f), 1.0f, 1.85f);
        if (std::abs(targetZoom - autoZoom_) > 0.04f) {
            autoZoom_ = autoZoom_ * 0.94f + targetZoom * 0.06f;
        }
        const float faceCx = ai.face.boxX + ai.face.boxW * 0.5f;
        const float faceCy = ai.face.boxY + ai.face.boxH * 0.45f;
        const float dx = faceCx - 0.5f;
        const float dy = faceCy - 0.44f;
        if (std::abs(dx) > 0.03f) {
            const float dirX = params.flipH ? -dx : dx;
            autoPanX_ = std::clamp(autoPanX_ + (dirX / std::max(autoZoom_, 1.0f)) * 0.07f, -0.35f, 0.35f);
        }
        if (std::abs(dy) > 0.03f) {
            const float dirY = params.flipV ? -dy : dy;
            autoPanY_ = std::clamp(autoPanY_ + (dirY / std::max(autoZoom_, 1.0f)) * 0.07f, -0.35f, 0.35f);
        }
    } else {
        autoZoom_ = autoZoom_ * 0.88f + 1.0f * 0.12f;
        autoPanX_ *= 0.88f;
        autoPanY_ *= 0.88f;
        if (std::abs(autoZoom_ - 1.0f) < 0.005f) autoZoom_ = 1.0f;
        if (std::abs(autoPanX_) < 0.002f) autoPanX_ = 0.0f;
        if (std::abs(autoPanY_) < 0.002f) autoPanY_ = 0.0f;
    }
}

void GpuProcessor::UpdateAutoWhiteBalance(const uint8_t* bgra, uint32_t w, uint32_t h) {
    if (!bgra || w < 16 || h < 16) return;
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    uint32_t count = 0;
    for (uint32_t y = h / 8; y < (h * 7) / 8; y += 8) {
        const uint8_t* row = bgra + static_cast<size_t>(y) * w * 4;
        for (uint32_t x = w / 8; x < (w * 7) / 8; x += 8) {
            const float b = row[x * 4 + 0];
            const float g = row[x * 4 + 1];
            const float r = row[x * 4 + 2];
            const float lum = (r + g + b) / 3.0f;
            if (lum > 18.0f && lum < 238.0f) {
                sumB += b;
                sumG += g;
                sumR += r;
                ++count;
            }
        }
    }
    if (count < 16) return;
    const float avgR = static_cast<float>(sumR / count);
    const float avgG = static_cast<float>(sumG / count);
    const float avgB = static_cast<float>(sumB / count);
    const float gray = (avgR + avgG + avgB) / 3.0f;
    if (gray < 10.0f) return;

    const float tgtR = std::clamp(gray / std::max(avgR, 1.0f), 0.78f, 1.28f);
    const float tgtG = std::clamp(gray / std::max(avgG, 1.0f), 0.85f, 1.18f);
    const float tgtB = std::clamp(gray / std::max(avgB, 1.0f), 0.78f, 1.28f);
    awbGainR_ = awbGainR_ * 0.90f + tgtR * 0.10f;
    awbGainG_ = awbGainG_ * 0.90f + tgtG * 0.10f;
    awbGainB_ = awbGainB_ * 0.90f + tgtB * 0.10f;
}

bool GpuProcessor::ProcessFrame(
    const RawFrameInput& input,
    const PC_VideoParams& params,
    bool producePreview,
    ProcessedFrameOutput& output,
    const AiStateSnapshot* overrideAi) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!device_ || !input.data || input.width < 16 || input.height < 16) return false;

    const uint32_t outW = (params.outputWidth >= 640 && params.outputWidth <= 1920)
        ? (static_cast<uint32_t>(params.outputWidth) & ~1u) : 1280u;
    const uint32_t outH = (params.outputHeight >= 360 && params.outputHeight <= 1080)
        ? (static_cast<uint32_t>(params.outputHeight) & ~1u) : 720u;

    if (!EnsureInputTextures(input.width, input.height) ||
        !EnsureOutputTextures(outW, outH) ||
        !EnsureAiResources()) {
        lastError_ = "Failed creating GPU textures";
        return false;
    }

    AiStateSnapshot aiState;
    if (overrideAi) {
        aiState = *overrideAi;
    } else if (aiEngine_) {
        aiEngine_->GetLatestState(aiState);
    }

    // Update Auto-Framing (Center Stage) before Pass 1 geometry
    UpdateCenterStage(params, aiState);

    // Upload source pixels
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (input.format == InputPixelFormat::NV12) {
        const uint32_t strideY = input.strideY ? input.strideY : input.width;
        const uint32_t strideUV = input.strideUV ? input.strideUV : input.width;
        const uint8_t* srcUV = input.planeUV
            ? input.planeUV
            : (input.data + static_cast<size_t>(strideY) * input.height);

        if (FAILED(ctx_->Map(srcYTex_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
        for (uint32_t r = 0; r < input.height; ++r) {
            std::memcpy(static_cast<uint8_t*>(mapped.pData) + r * mapped.RowPitch,
                        input.data + static_cast<size_t>(r) * strideY,
                        input.width);
        }
        ctx_->Unmap(srcYTex_.Get(), 0);

        if (FAILED(ctx_->Map(srcUVTex_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
        for (uint32_t r = 0; r < input.height / 2; ++r) {
            std::memcpy(static_cast<uint8_t*>(mapped.pData) + r * mapped.RowPitch,
                        srcUV + static_cast<size_t>(r) * strideUV,
                        input.width);
        }
        ctx_->Unmap(srcUVTex_.Get(), 0);
    } else {
        const uint32_t stride = input.strideY ? input.strideY : (input.width * 4u);
        if (FAILED(ctx_->Map(srcBgraTex_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
        for (uint32_t r = 0; r < input.height; ++r) {
            std::memcpy(static_cast<uint8_t*>(mapped.pData) + r * mapped.RowPitch,
                        input.data + static_cast<size_t>(r) * stride,
                        static_cast<size_t>(input.width) * 4u);
        }
        ctx_->Unmap(srcBgraTex_.Get(), 0);
    }

    auto uploadCB = [&](ID3D11Buffer* buf, const void* data, size_t bytes) {
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (SUCCEEDED(ctx_->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            std::memcpy(ms.pData, data, bytes);
            ctx_->Unmap(buf, 0);
        }
    };

    // Pass 1: Geometry, Flip, Rotation, Smart Framing, Center Stage
    const int baseRot = params.autoRotateFromPhone ? input.sensorRotationDeg : 0;
    const int totalRot = ((baseRot + params.manualRotationDeg) % 360 + 360) % 360;
    GeomCBData geomCb{};
    geomCb.srcWidth = static_cast<float>(input.width);
    geomCb.srcHeight = static_cast<float>(input.height);
    geomCb.outWidth = static_cast<float>(outW);
    geomCb.outHeight = static_cast<float>(outH);
    geomCb.inputMode = (input.format == InputPixelFormat::BGRA32) ? 1 : 0;
    geomCb.rotationDeg = totalRot;
    geomCb.flipH = params.flipH ? 1 : 0;
    geomCb.flipV = params.flipV ? 1 : 0;
    geomCb.framingMode = params.framingMode;
    geomCb.zoom = std::clamp(params.zoom * autoZoom_, 1.0f, 3.0f);
    geomCb.panX = std::clamp(params.panX + autoPanX_, -0.5f, 0.5f);
    geomCb.panY = std::clamp(params.panY + autoPanY_, -0.5f, 0.5f);
    uploadCB(cbGeom_.Get(), &geomCb, sizeof(geomCb));

    ID3D11ShaderResourceView* geomSrvs[3] = { srcYSrv_.Get(), srcUVSrv_.Get(), srcBgraSrv_.Get() };
    DrawPass(psGeom_.Get(), rtGeomRtv_.GetAddressOf(), 1, outW, outH, geomSrvs, 3, cbGeom_.Get());

    // Extract 480x270 oriented BGRA asynchronously for ONNX AI + Auto White Balance
    if (!overrideAi) {
        const bool needAwb = (!params.bypassEffects && params.autoWhiteBalance > 0.01f);
        SubmitAsyncAiExtractIfNeeded(params, needAwb);
    }

    // Upload AI segmentation mask if active
    UploadMaskIfNeeded(aiState, params.bypassEffects ? 0 : params.bgMode);

    // Pass 2: AI-Guided Face Warp (Slim + Eye Enlarge) + Skin Smooth + Studio Lighting
    BeautyCBData beautyCb{};
    beautyCb.texWidth = static_cast<float>(outW);
    beautyCb.texHeight = static_cast<float>(outH);
    beautyCb.skinSmooth = params.bypassEffects ? 0.0f : std::clamp(params.skinSmooth, 0.0f, 1.0f);
    beautyCb.denoise = params.bypassEffects ? 0.0f : std::clamp(params.denoise, 0.0f, 1.0f);
    beautyCb.studioLight = params.bypassEffects ? 0.0f : std::clamp(params.studioLight, 0.0f, 1.0f);
    beautyCb.faceSlim = params.bypassEffects ? 0.0f : std::clamp(params.faceSlim, 0.0f, 1.0f);
    beautyCb.eyeEnlarge = params.bypassEffects ? 0.0f : std::clamp(params.eyeEnlarge, 0.0f, 1.0f);
    beautyCb.faceDetected = aiState.face.detected ? 1.0f : 0.0f;
    if (aiState.face.detected) {
        beautyCb.faceCenterX = aiState.face.boxX + aiState.face.boxW * 0.5f;
        beautyCb.faceCenterY = aiState.face.boxY + aiState.face.boxH * 0.5f;
        beautyCb.faceScale = std::max(aiState.face.boxW, aiState.face.boxH);
        beautyCb.rightEyeX = aiState.face.rightEyeX;
        beautyCb.rightEyeY = aiState.face.rightEyeY;
        beautyCb.leftEyeX = aiState.face.leftEyeX;
        beautyCb.leftEyeY = aiState.face.leftEyeY;
    }
    uploadCB(cbBeauty_.Get(), &beautyCb, sizeof(beautyCb));

    DrawPass(psBeauty_.Get(), rtBeautyRtv_.GetAddressOf(), 1, outW, outH, rtGeomSrv_.GetAddressOf(), 1, cbBeauty_.Get());

    // Pass 3A: Virtual Camera Output Grading + AI Background + AR Filters + Stylized Shaders
    const float elapsedSec = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime_).count();

    GradeCBData gradeCb{};
    gradeCb.texWidth = static_cast<float>(outW);
    gradeCb.texHeight = static_cast<float>(outH);
    gradeCb.exposure = std::clamp(params.exposure, -2.0f, 2.0f);
    gradeCb.brightness = std::clamp(params.brightness, -0.5f, 0.5f);
    gradeCb.contrast = std::clamp(params.contrast, 0.5f, 1.8f);
    gradeCb.highlights = std::clamp(params.highlights, -1.0f, 1.0f);
    gradeCb.shadows = std::clamp(params.shadows, -1.0f, 1.0f);
    gradeCb.gammaVal = std::clamp(params.gammaVal, 0.6f, 1.6f);
    gradeCb.temperature = std::clamp(params.temperature, -1.0f, 1.0f);
    gradeCb.tint = std::clamp(params.tint, -1.0f, 1.0f);
    gradeCb.saturation = std::clamp(params.saturation, 0.0f, 2.0f);
    gradeCb.vibrance = std::clamp(params.vibrance, -1.0f, 1.0f);
    gradeCb.sharpen = std::clamp(params.sharpen, 0.0f, 1.0f);
    gradeCb.softGlow = std::clamp(params.softGlow, 0.0f, 1.0f);
    gradeCb.vignette = std::clamp(params.vignette, 0.0f, 1.0f);
    gradeCb.awbStrength = std::clamp(params.autoWhiteBalance, 0.0f, 1.0f);
    gradeCb.awbGainR = awbGainR_;
    gradeCb.awbGainG = awbGainG_;
    gradeCb.awbGainB = awbGainB_;
    gradeCb.mirrorCheck = params.mirrorCheckOverlay ? 1.0f : 0.0f;
    gradeCb.isPreview = 0.0f;
    gradeCb.splitPos = -1.0f;
    gradeCb.bypassAll = params.bypassEffects ? 1.0f : 0.0f;
    gradeCb.previewFlipH = 0.0f;

    gradeCb.bgMode = params.bypassEffects ? 0 : params.bgMode;
    gradeCb.arFilterMode = params.bypassEffects ? 0 : params.arFilterMode;
    gradeCb.styleFilterMode = params.bypassEffects ? 0 : params.styleFilterMode;
    gradeCb.bgBlurStrength = std::clamp(params.bgBlurStrength, 0.0f, 1.0f);
    gradeCb.eyeBrighten = std::clamp(params.eyeBrighten, 0.0f, 1.0f);
    gradeCb.teethWhiten = std::clamp(params.teethWhiten, 0.0f, 1.0f);
    gradeCb.styleIntensity = std::clamp(params.styleIntensity, 0.0f, 1.0f);
    gradeCb.timeSec = elapsedSec;

    gradeCb.hasCustomBg = hasCustomBg_ ? 1.0f : 0.0f;
    gradeCb.hasAiMask = aiState.hasValidMask ? 1.0f : 0.0f;
    gradeCb.faceDetected = aiState.face.detected ? 1.0f : 0.0f;
    gradeCb.faceConf = aiState.face.confidence;
    if (aiState.face.detected) {
        gradeCb.rightEyeX = aiState.face.rightEyeX;
        gradeCb.rightEyeY = aiState.face.rightEyeY;
        gradeCb.leftEyeX = aiState.face.leftEyeX;
        gradeCb.leftEyeY = aiState.face.leftEyeY;
        gradeCb.noseX = aiState.face.noseX;
        gradeCb.noseY = aiState.face.noseY;
        if (aiState.face.rightMouthX > 0.001f || aiState.face.leftMouthX > 0.001f) {
            gradeCb.mouthCenterX = 0.5f * (aiState.face.rightMouthX + aiState.face.leftMouthX);
            gradeCb.mouthCenterY = 0.5f * (aiState.face.rightMouthY + aiState.face.leftMouthY);
        } else {
            gradeCb.mouthCenterX = aiState.face.noseX;
            gradeCb.mouthCenterY = aiState.face.boxY + aiState.face.boxH * 0.72f;
        }
        gradeCb.faceBoxX = aiState.face.boxX;
        gradeCb.faceBoxY = aiState.face.boxY;
        gradeCb.faceBoxW = aiState.face.boxW;
        gradeCb.faceBoxH = aiState.face.boxH;
    }
    uploadCB(cbGrade_.Get(), &gradeCb, sizeof(gradeCb));

    ID3D11ShaderResourceView* gradeSrvs[4] = {
        rtBeautySrv_.Get(),
        rtGeomSrv_.Get(),
        texMaskSrv_.Get(),
        texCustomBgSrv_.Get()
    };
    DrawPass(psGrade_.Get(), rtGradeOutRtv_.GetAddressOf(), 1, outW, outH, gradeSrvs, 4, cbGrade_.Get());

    // Pass 4: Convert Graded RGBA -> NV12 Y & UV planes
    DrawPass(psToY_.Get(), rtYRtv_.GetAddressOf(), 1, outW, outH, rtGradeOutSrv_.GetAddressOf(), 1, nullptr);
    DrawPass(psToUV_.Get(), rtUVRtv_.GetAddressOf(), 1, outW / 2, outH / 2, rtGradeOutSrv_.GetAddressOf(), 1, nullptr);

    const int slot = stagingIdx_;
    stagingIdx_ ^= 1;

    ctx_->CopyResource(stagingYTex_[slot].Get(), rtYTex_.Get());
    ctx_->CopyResource(stagingUVTex_[slot].Get(), rtUVTex_.Get());

    const size_t yBytes = static_cast<size_t>(outW) * outH;
    const size_t uvBytes = yBytes / 2;
    output.nv12.resize(yBytes + uvBytes);
    output.width = outW;
    output.height = outH;

    if (SUCCEEDED(ctx_->Map(stagingYTex_[slot].Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        for (uint32_t r = 0; r < outH; ++r) {
            std::memcpy(output.nv12.data() + static_cast<size_t>(r) * outW,
                        static_cast<const uint8_t*>(mapped.pData) + r * mapped.RowPitch,
                        outW);
        }
        ctx_->Unmap(stagingYTex_[slot].Get(), 0);
    }

    if (SUCCEEDED(ctx_->Map(stagingUVTex_[slot].Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        uint8_t* dstUV = output.nv12.data() + yBytes;
        for (uint32_t r = 0; r < outH / 2; ++r) {
            std::memcpy(dstUV + static_cast<size_t>(r) * outW,
                        static_cast<const uint8_t*>(mapped.pData) + r * mapped.RowPitch,
                        outW);
        }
        ctx_->Unmap(stagingUVTex_[slot].Get(), 0);
    }

    // Optional lightweight Preview pass (<= 640x360 BGRA, handles Selfie mirror & Split compare)
    if (producePreview) {
        const bool needCustomPreview =
            (params.previewMirrorLocal != 0) ||
            (params.splitComparePos >= 0.0f) ||
            (previewW_ != outW) ||
            (previewH_ != outH);

        ID3D11Texture2D* previewSrc = rtGradeOutTex_.Get();
        if (needCustomPreview) {
            gradeCb.texWidth = static_cast<float>(previewW_);
            gradeCb.texHeight = static_cast<float>(previewH_);
            gradeCb.isPreview = 1.0f;
            gradeCb.splitPos = params.splitComparePos;
            gradeCb.previewFlipH = params.previewMirrorLocal ? 1.0f : 0.0f;
            uploadCB(cbGrade_.Get(), &gradeCb, sizeof(gradeCb));
            DrawPass(psGrade_.Get(), rtPreviewRtv_.GetAddressOf(), 1, previewW_, previewH_, gradeSrvs, 4, cbGrade_.Get());
            previewSrc = rtPreviewTex_.Get();
        }

        ctx_->CopyResource(stagingPreviewTex_[slot].Get(), previewSrc);
        if (SUCCEEDED(ctx_->Map(stagingPreviewTex_[slot].Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            output.previewWidth = previewW_;
            output.previewHeight = previewH_;
            output.previewBgra.resize(static_cast<size_t>(previewW_) * previewH_ * 4u);
            for (uint32_t r = 0; r < previewH_; ++r) {
                std::memcpy(output.previewBgra.data() + static_cast<size_t>(r) * previewW_ * 4u,
                            static_cast<const uint8_t*>(mapped.pData) + r * mapped.RowPitch,
                            static_cast<size_t>(previewW_) * 4u);
            }
            ctx_->Unmap(stagingPreviewTex_[slot].Get(), 0);
        }
    } else {
        output.previewWidth = 0;
        output.previewHeight = 0;
        output.previewBgra.clear();
    }

    return true;
}

} // namespace phonecam
