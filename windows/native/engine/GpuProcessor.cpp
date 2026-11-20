#include "GpuProcessor.h"
#include "GeneratedShaders.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace phonecam {

namespace {

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
    float pad0;
    float pad1;
    float pad2;
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
};

static_assert(sizeof(GeomCBData) == 48, "GeomCBData size mismatch");
static_assert(sizeof(BeautyCBData) == 32, "BeautyCBData size mismatch");
static_assert(sizeof(GradeCBData) == 96, "GradeCBData size mismatch");

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

} // namespace

GpuProcessor::~GpuProcessor() {
    Shutdown();
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

    if (!CompileShaders()) {
        Shutdown();
        return false;
    }

    return true;
}

void GpuProcessor::Shutdown() {
    srcTexW_ = srcTexH_ = 0;
    outTexW_ = outTexH_ = 0;
    srcYSrv_.Reset(); srcYTex_.Reset();
    srcUVSrv_.Reset(); srcUVTex_.Reset();
    srcBgraSrv_.Reset(); srcBgraTex_.Reset();
    rtGeomSrv_.Reset(); rtGeomRtv_.Reset(); rtGeomTex_.Reset();
    rtBeautySrv_.Reset(); rtBeautyRtv_.Reset(); rtBeautyTex_.Reset();
    rtGradeOutSrv_.Reset(); rtGradeOutRtv_.Reset(); rtGradeOutTex_.Reset();
    rtPreviewRtv_.Reset(); rtPreviewTex_.Reset(); stagingPreviewTex_.Reset();
    rtYRtv_.Reset(); rtYTex_.Reset(); stagingYTex_.Reset();
    rtUVRtv_.Reset(); rtUVTex_.Reset(); stagingUVTex_.Reset();
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

bool GpuProcessor::EnsureInputTextures(InputPixelFormat, uint32_t w, uint32_t h) {
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
    rtPreviewRtv_.Reset(); rtPreviewTex_.Reset(); stagingPreviewTex_.Reset();
    rtYRtv_.Reset(); rtYTex_.Reset(); stagingYTex_.Reset();
    rtUVRtv_.Reset(); rtUVTex_.Reset(); stagingUVTex_.Reset();

    if (!CreateRenderTargetTex(device_.Get(), w, h, DXGI_FORMAT_B8G8R8A8_UNORM, rtGeomTex_, rtGeomRtv_, &rtGeomSrv_)) return false;
    if (!CreateRenderTargetTex(device_.Get(), w, h, DXGI_FORMAT_B8G8R8A8_UNORM, rtBeautyTex_, rtBeautyRtv_, &rtBeautySrv_)) return false;
    if (!CreateRenderTargetTex(device_.Get(), w, h, DXGI_FORMAT_B8G8R8A8_UNORM, rtGradeOutTex_, rtGradeOutRtv_, &rtGradeOutSrv_)) return false;
    if (!CreateRenderTargetTex(device_.Get(), w, h, DXGI_FORMAT_B8G8R8A8_UNORM, rtPreviewTex_, rtPreviewRtv_, nullptr)) return false;
    if (!CreateStagingTex(device_.Get(), w, h, DXGI_FORMAT_B8G8R8A8_UNORM, stagingPreviewTex_)) return false;

    if (!CreateRenderTargetTex(device_.Get(), w, h, DXGI_FORMAT_R8_UNORM, rtYTex_, rtYRtv_, nullptr)) return false;
    if (!CreateStagingTex(device_.Get(), w, h, DXGI_FORMAT_R8_UNORM, stagingYTex_)) return false;
    if (!CreateRenderTargetTex(device_.Get(), w / 2, h / 2, DXGI_FORMAT_R8G8_UNORM, rtUVTex_, rtUVRtv_, nullptr)) return false;
    if (!CreateStagingTex(device_.Get(), w / 2, h / 2, DXGI_FORMAT_R8G8_UNORM, stagingUVTex_)) return false;

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

void GpuProcessor::UpdateAutoWhiteBalance(const uint8_t* bgra, uint32_t w, uint32_t h) {
    if (!bgra || w < 16 || h < 16) return;
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    uint32_t count = 0;
    for (uint32_t y = h / 8; y < (h * 7) / 8; y += 16) {
        const uint8_t* row = bgra + static_cast<size_t>(y) * w * 4;
        for (uint32_t x = w / 8; x < (w * 7) / 8; x += 16) {
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
    ProcessedFrameOutput& output) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!device_ || !input.data || input.width < 16 || input.height < 16) return false;

    const uint32_t outW = (params.outputWidth >= 640 && params.outputWidth <= 1920)
        ? (static_cast<uint32_t>(params.outputWidth) & ~1u) : 1280u;
    const uint32_t outH = (params.outputHeight >= 360 && params.outputHeight <= 1080)
        ? (static_cast<uint32_t>(params.outputHeight) & ~1u) : 720u;

    if (!EnsureInputTextures(input.format, input.width, input.height) ||
        !EnsureOutputTextures(outW, outH)) {
        lastError_ = "Failed creating GPU textures";
        return false;
    }

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

    // Pass 1: Geometry, Flip, Rotation, Smart Framing
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
    geomCb.zoom = std::clamp(params.zoom, 1.0f, 3.0f);
    geomCb.panX = std::clamp(params.panX, -0.5f, 0.5f);
    geomCb.panY = std::clamp(params.panY, -0.5f, 0.5f);
    uploadCB(cbGeom_.Get(), &geomCb, sizeof(geomCb));

    ID3D11ShaderResourceView* geomSrvs[3] = { srcYSrv_.Get(), srcUVSrv_.Get(), srcBgraSrv_.Get() };
    DrawPass(psGeom_.Get(), rtGeomRtv_.GetAddressOf(), 1, outW, outH, geomSrvs, 3, cbGeom_.Get());

    // Pass 2: Beauty & Studio Lighting
    BeautyCBData beautyCb{};
    beautyCb.texWidth = static_cast<float>(outW);
    beautyCb.texHeight = static_cast<float>(outH);
    beautyCb.skinSmooth = params.bypassEffects ? 0.0f : std::clamp(params.skinSmooth, 0.0f, 1.0f);
    beautyCb.denoise = params.bypassEffects ? 0.0f : std::clamp(params.denoise, 0.0f, 1.0f);
    beautyCb.studioLight = params.bypassEffects ? 0.0f : std::clamp(params.studioLight, 0.0f, 1.0f);
    uploadCB(cbBeauty_.Get(), &beautyCb, sizeof(beautyCb));

    DrawPass(psBeauty_.Get(), rtBeautyRtv_.GetAddressOf(), 1, outW, outH, rtGeomSrv_.GetAddressOf(), 1, cbBeauty_.Get());

    // Pass 3A: Virtual Camera Output Grading (NO preview split, NO selfie-mirror)
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
    uploadCB(cbGrade_.Get(), &gradeCb, sizeof(gradeCb));

    ID3D11ShaderResourceView* gradeSrvs[2] = { rtBeautySrv_.Get(), rtGeomSrv_.Get() };
    DrawPass(psGrade_.Get(), rtGradeOutRtv_.GetAddressOf(), 1, outW, outH, gradeSrvs, 2, cbGrade_.Get());

    // Pass 4: Convert Graded RGBA -> NV12 Y & UV planes
    DrawPass(psToY_.Get(), rtYRtv_.GetAddressOf(), 1, outW, outH, rtGradeOutSrv_.GetAddressOf(), 1, nullptr);
    DrawPass(psToUV_.Get(), rtUVRtv_.GetAddressOf(), 1, outW / 2, outH / 2, rtGradeOutSrv_.GetAddressOf(), 1, nullptr);

    ctx_->CopyResource(stagingYTex_.Get(), rtYTex_.Get());
    ctx_->CopyResource(stagingUVTex_.Get(), rtUVTex_.Get());

    const size_t yBytes = static_cast<size_t>(outW) * outH;
    const size_t uvBytes = yBytes / 2;
    output.nv12.resize(yBytes + uvBytes);
    output.width = outW;
    output.height = outH;

    if (SUCCEEDED(ctx_->Map(stagingYTex_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        for (uint32_t r = 0; r < outH; ++r) {
            std::memcpy(output.nv12.data() + static_cast<size_t>(r) * outW,
                        static_cast<const uint8_t*>(mapped.pData) + r * mapped.RowPitch,
                        outW);
        }
        ctx_->Unmap(stagingYTex_.Get(), 0);
    }

    if (SUCCEEDED(ctx_->Map(stagingUVTex_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        uint8_t* dstUV = output.nv12.data() + yBytes;
        for (uint32_t r = 0; r < outH / 2; ++r) {
            std::memcpy(dstUV + static_cast<size_t>(r) * outW,
                        static_cast<const uint8_t*>(mapped.pData) + r * mapped.RowPitch,
                        outW);
        }
        ctx_->Unmap(stagingUVTex_.Get(), 0);
    }

    // Optional Preview pass (handles Selfie preview flip & Before/After split compare)
    if (producePreview || params.autoWhiteBalance > 0.01f) {
        const bool needCustomPreview = (params.previewMirrorLocal != 0) || (params.splitComparePos >= 0.0f);
        ID3D11Texture2D* previewSrc = rtGradeOutTex_.Get();
        if (needCustomPreview) {
            gradeCb.isPreview = 1.0f;
            gradeCb.splitPos = params.splitComparePos;
            gradeCb.previewFlipH = params.previewMirrorLocal ? 1.0f : 0.0f;
            uploadCB(cbGrade_.Get(), &gradeCb, sizeof(gradeCb));
            DrawPass(psGrade_.Get(), rtPreviewRtv_.GetAddressOf(), 1, outW, outH, gradeSrvs, 2, cbGrade_.Get());
            previewSrc = rtPreviewTex_.Get();
        }
        ctx_->CopyResource(stagingPreviewTex_.Get(), previewSrc);
        if (SUCCEEDED(ctx_->Map(stagingPreviewTex_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            output.previewBgra.resize(static_cast<size_t>(outW) * outH * 4u);
            for (uint32_t r = 0; r < outH; ++r) {
                std::memcpy(output.previewBgra.data() + static_cast<size_t>(r) * outW * 4u,
                            static_cast<const uint8_t*>(mapped.pData) + r * mapped.RowPitch,
                            static_cast<size_t>(outW) * 4u);
            }
            ctx_->Unmap(stagingPreviewTex_.Get(), 0);

            if (params.autoWhiteBalance > 0.01f) {
                UpdateAutoWhiteBalance(output.previewBgra.data(), outW, outH);
            }
        }
    }

    return true;
}

} // namespace phonecam
