#include "VideoDecoder.h"

#include <mfapi.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <codecapi.h>
#include <propvarutil.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace phonecam {

VideoDecoder::~VideoDecoder() {
    Shutdown();
}

bool VideoDecoder::Initialize() {
    Shutdown();
    MFStartup(MF_VERSION, MFSTARTUP_LITE);

    CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory_));

    InitDxvaDevice();
    if (!InitH264Mft(true)) {
        return InitH264Mft(false);
    }
    return true;
}

void VideoDecoder::Shutdown() {
    if (h264Mft_) {
        h264Mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        h264Mft_.Reset();
    }
    dxvaStagingTex_.Reset();
    dxvaStagingW_ = 0;
    dxvaStagingH_ = 0;
    dxvaStagingFmt_ = DXGI_FORMAT_UNKNOWN;
    dxgiManager_.Reset();
    dxvaCtx_.Reset();
    dxvaDevice_.Reset();
    wicFactory_.Reset();
    usingDxva_ = false;
    h264OutputReady_ = false;
    MFShutdown();
}

void VideoDecoder::ResetH264() {
    if (h264Mft_) {
        h264Mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        h264Mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    }
    h264Mft_.Reset();
    h264OutputReady_ = false;
    usingDxva_ = false;
    if (!InitH264Mft(true)) {
        InitH264Mft(false);
    }
}

bool VideoDecoder::InitDxvaDevice() {
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL chosen{};
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels,
        _countof(levels),
        D3D11_SDK_VERSION,
        &dxvaDevice_,
        &chosen,
        &dxvaCtx_);
    if (FAILED(hr) || !dxvaDevice_) {
        dxvaDevice_.Reset();
        dxvaCtx_.Reset();
        return false;
    }

    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(dxvaDevice_.As(&mt))) {
        mt->SetMultithreadProtected(TRUE);
    }

    hr = MFCreateDXGIDeviceManager(&dxgiResetToken_, &dxgiManager_);
    if (FAILED(hr) || !dxgiManager_) {
        dxvaCtx_.Reset();
        dxvaDevice_.Reset();
        return false;
    }

    hr = dxgiManager_->ResetDevice(dxvaDevice_.Get(), dxgiResetToken_);
    if (FAILED(hr)) {
        dxgiManager_.Reset();
        dxvaCtx_.Reset();
        dxvaDevice_.Reset();
        return false;
    }
    return true;
}

bool VideoDecoder::InitH264Mft(bool allowDxva) {
    h264OutputReady_ = false;
    usingDxva_ = false;

    HRESULT hr = CoCreateInstance(
        CLSID_MSH264DecoderMFT,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&h264Mft_));
    if (FAILED(hr) || !h264Mft_) return false;

    ComPtr<ICodecAPI> codecApi;
    if (SUCCEEDED(h264Mft_.As(&codecApi))) {
        VARIANT var{};
        var.vt = VT_BOOL;
        var.boolVal = VARIANT_TRUE;
        codecApi->SetValue(&CODECAPI_AVLowLatencyMode, &var);
    }

    if (allowDxva && dxgiManager_) {
        ComPtr<IMFAttributes> attrs;
        UINT32 d3d11Aware = 0;
        if (SUCCEEDED(h264Mft_->GetAttributes(&attrs)) &&
            SUCCEEDED(attrs->GetUINT32(MF_SA_D3D11_AWARE, &d3d11Aware)) &&
            d3d11Aware != 0) {
            if (SUCCEEDED(h264Mft_->ProcessMessage(
                    MFT_MESSAGE_SET_D3D_MANAGER,
                    reinterpret_cast<ULONG_PTR>(dxgiManager_.Get())))) {
                usingDxva_ = true;
            }
        }
    }

    ComPtr<IMFMediaType> inType;
    if (FAILED(MFCreateMediaType(&inType))) {
        h264Mft_.Reset();
        usingDxva_ = false;
        return false;
    }
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, 1280, 720);
    MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, 60, 1);

    hr = h264Mft_->SetInputType(0, inType.Get(), 0);
    if (FAILED(hr)) {
        h264Mft_.Reset();
        usingDxva_ = false;
        return false;
    }

    NegotiateH264OutputType();
    h264Mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    h264Mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    return true;
}

bool VideoDecoder::NegotiateH264OutputType() {
    if (!h264Mft_) return false;
    for (DWORD i = 0;; ++i) {
        ComPtr<IMFMediaType> outType;
        HRESULT hr = h264Mft_->GetOutputAvailableType(0, i, &outType);
        if (FAILED(hr)) break;

        GUID subtype{};
        if (SUCCEEDED(outType->GetGUID(MF_MT_SUBTYPE, &subtype)) && subtype == MFVideoFormat_NV12) {
            if (SUCCEEDED(h264Mft_->SetOutputType(0, outType.Get(), 0))) {
                UINT32 w = 1280, h = 720;
                MFGetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, &w, &h);
                codedW_ = w;
                codedH_ = h;
                displayW_ = w;
                displayH_ = h;

                MFVideoArea area{};
                UINT32 areaSize = 0;
                if (SUCCEEDED(outType->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), &areaSize)) &&
                    areaSize == sizeof(area) && area.Area.cx > 0 && area.Area.cy > 0) {
                    displayW_ = static_cast<uint32_t>(area.Area.cx) & ~1u;
                    displayH_ = static_cast<uint32_t>(area.Area.cy) & ~1u;
                }

                UINT32 stride = w;
                if (FAILED(outType->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) || stride < w) {
                    stride = w;
                }
                defaultStride_ = static_cast<INT32>(stride);

                MFT_OUTPUT_STREAM_INFO osi{};
                if (SUCCEEDED(h264Mft_->GetOutputStreamInfo(0, &osi))) {
                    mftProvidesSamples_ = (osi.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
                    mftOutputBufferSize_ = std::max<DWORD>(osi.cbSize, codedW_ * codedH_ * 2u);
                } else {
                    mftProvidesSamples_ = false;
                    mftOutputBufferSize_ = codedW_ * codedH_ * 2u;
                }
                h264OutputReady_ = true;
                return true;
            }
        }
    }
    return false;
}

bool VideoDecoder::DecodeH264(const uint8_t* annexB, size_t size, uint64_t timestampMs, DecodedFrame& outFrame) {
    if (!annexB || size == 0) return false;
    if (!h264Mft_ && !InitH264Mft(true) && !InitH264Mft(false)) return false;

    ComPtr<IMFSample> inSample;
    ComPtr<IMFMediaBuffer> inBuf;
    if (FAILED(MFCreateSample(&inSample)) ||
        FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(size), &inBuf))) {
        return false;
    }
    BYTE* dst = nullptr;
    if (FAILED(inBuf->Lock(&dst, nullptr, nullptr))) return false;
    std::memcpy(dst, annexB, size);
    inBuf->Unlock();
    inBuf->SetCurrentLength(static_cast<DWORD>(size));
    inSample->AddBuffer(inBuf.Get());
    inSample->SetSampleTime(static_cast<LONGLONG>(timestampMs) * 10000LL);
    inSample->SetSampleDuration(166667LL);

    HRESULT hr = h264Mft_->ProcessInput(0, inSample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
        MFT_OUTPUT_DATA_BUFFER outData{};
        DWORD status = 0;
        ComPtr<IMFSample> tmpSample;
        if (!mftProvidesSamples_ && SUCCEEDED(MFCreateSample(&tmpSample))) {
            ComPtr<IMFMediaBuffer> tmpBuf;
            if (SUCCEEDED(MFCreateMemoryBuffer(mftOutputBufferSize_, &tmpBuf))) {
                tmpSample->AddBuffer(tmpBuf.Get());
                outData.pSample = tmpSample.Get();
            }
        }
        h264Mft_->ProcessOutput(0, 1, &outData, &status);
        if (mftProvidesSamples_ && outData.pSample) outData.pSample->Release();
        if (outData.pEvents) outData.pEvents->Release();
        hr = h264Mft_->ProcessInput(0, inSample.Get(), 0);
    }
    if (FAILED(hr)) {
        if (usingDxva_) {
            h264Mft_.Reset();
            usingDxva_ = false;
            if (InitH264Mft(false)) {
                hr = h264Mft_->ProcessInput(0, inSample.Get(), 0);
            }
        }
        if (FAILED(hr)) return false;
    }

    bool gotFrame = false;
    for (int iter = 0; iter < 8; ++iter) {
        MFT_OUTPUT_DATA_BUFFER outData{};
        ComPtr<IMFSample> outSample;
        if (!mftProvidesSamples_) {
            if (FAILED(MFCreateSample(&outSample))) break;
            ComPtr<IMFMediaBuffer> outBuf;
            if (FAILED(MFCreateMemoryBuffer(mftOutputBufferSize_, &outBuf))) break;
            outSample->AddBuffer(outBuf.Get());
            outData.pSample = outSample.Get();
        }

        DWORD status = 0;
        hr = h264Mft_->ProcessOutput(0, 1, &outData, &status);
        if (outData.pEvents) {
            outData.pEvents->Release();
            outData.pEvents = nullptr;
        }

        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            if (mftProvidesSamples_ && outData.pSample) outData.pSample->Release();
            NegotiateH264OutputType();
            continue;
        }
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT || FAILED(hr)) {
            if (mftProvidesSamples_ && outData.pSample) outData.pSample->Release();
            break;
        }

        IMFSample* readySample = mftProvidesSamples_ ? outData.pSample : outSample.Get();
        if (readySample) {
            if (ReadH264OutputSample(readySample, outFrame)) {
                gotFrame = true;
            }
            if (mftProvidesSamples_) {
                readySample->Release();
            }
        }
    }
    return gotFrame;
}

bool VideoDecoder::CopyDxgiSurfaceToNv12(ID3D11Texture2D* tex, UINT subresourceIdx, DecodedFrame& outFrame) {
    if (!tex || !dxvaDevice_ || !dxvaCtx_) return false;
    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);
    if (td.Format != DXGI_FORMAT_NV12 || td.Width < displayW_ || td.Height < displayH_) {
        return false;
    }

    if (!dxvaStagingTex_ || dxvaStagingW_ != td.Width || dxvaStagingH_ != td.Height || dxvaStagingFmt_ != td.Format) {
        dxvaStagingTex_.Reset();
        D3D11_TEXTURE2D_DESC sd = td;
        sd.MipLevels = 1;
        sd.ArraySize = 1;
        sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.MiscFlags = 0;
        if (FAILED(dxvaDevice_->CreateTexture2D(&sd, nullptr, &dxvaStagingTex_))) {
            return false;
        }
        dxvaStagingW_ = td.Width;
        dxvaStagingH_ = td.Height;
        dxvaStagingFmt_ = td.Format;
    }

    dxvaCtx_->CopySubresourceRegion(dxvaStagingTex_.Get(), 0, 0, 0, 0, tex, subresourceIdx, nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(dxvaCtx_->Map(dxvaStagingTex_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }

    const uint32_t outW = displayW_;
    const uint32_t outH = displayH_;
    outFrame.isBgra = false;
    outFrame.width = outW;
    outFrame.height = outH;
    outFrame.stride = outW;
    outFrame.data.resize(static_cast<size_t>(outW) * outH * 3 / 2);

    const uint8_t* srcY = static_cast<const uint8_t*>(mapped.pData);
    const uint8_t* srcUV = srcY + static_cast<size_t>(mapped.RowPitch) * td.Height;
    for (uint32_t r = 0; r < outH; ++r) {
        std::memcpy(outFrame.data.data() + static_cast<size_t>(r) * outW, srcY + static_cast<size_t>(r) * mapped.RowPitch, outW);
    }
    uint8_t* dstUV = outFrame.data.data() + static_cast<size_t>(outW) * outH;
    for (uint32_t r = 0; r < outH / 2; ++r) {
        std::memcpy(dstUV + static_cast<size_t>(r) * outW, srcUV + static_cast<size_t>(r) * mapped.RowPitch, outW);
    }

    dxvaCtx_->Unmap(dxvaStagingTex_.Get(), 0);
    return true;
}

bool VideoDecoder::ReadH264OutputSample(IMFSample* sample, DecodedFrame& outFrame) {
    const uint32_t outW = displayW_;
    const uint32_t outH = displayH_;
    if (outW < 16 || outH < 16) return false;

    if (usingDxva_) {
        ComPtr<IMFMediaBuffer> rawBuf;
        ComPtr<IMFDXGIBuffer> dxgiBuf;
        if (SUCCEEDED(sample->GetBufferByIndex(0, &rawBuf)) && SUCCEEDED(rawBuf.As(&dxgiBuf))) {
            ComPtr<ID3D11Texture2D> surfTex;
            UINT subIdx = 0;
            if (SUCCEEDED(dxgiBuf->GetResource(IID_PPV_ARGS(&surfTex))) &&
                SUCCEEDED(dxgiBuf->GetSubresourceIndex(&subIdx)) &&
                CopyDxgiSurfaceToNv12(surfTex.Get(), subIdx, outFrame)) {
                return true;
            }
        }
    }

    ComPtr<IMFMediaBuffer> buf;
    if (FAILED(sample->ConvertToContiguousBuffer(&buf))) return false;

    outFrame.isBgra = false;
    outFrame.width = outW;
    outFrame.height = outH;
    outFrame.stride = outW;
    outFrame.data.resize(static_cast<size_t>(outW) * outH * 3 / 2);

    ComPtr<IMF2DBuffer> buf2d;
    BYTE* scan0 = nullptr;
    LONG pitch = 0;
    if (SUCCEEDED(buf.As(&buf2d)) && SUCCEEDED(buf2d->Lock2D(&scan0, &pitch))) {
        const uint32_t absPitch = static_cast<uint32_t>(std::abs(pitch));
        const BYTE* srcY = scan0;
        const BYTE* srcUV = scan0 + static_cast<size_t>(absPitch) * codedH_;
        for (uint32_t r = 0; r < outH; ++r) {
            std::memcpy(outFrame.data.data() + static_cast<size_t>(r) * outW, srcY + static_cast<size_t>(r) * absPitch, outW);
        }
        uint8_t* dstUV = outFrame.data.data() + static_cast<size_t>(outW) * outH;
        for (uint32_t r = 0; r < outH / 2; ++r) {
            std::memcpy(dstUV + static_cast<size_t>(r) * outW, srcUV + static_cast<size_t>(r) * absPitch, outW);
        }
        buf2d->Unlock2D();
        return true;
    }

    BYTE* raw = nullptr;
    DWORD rawLen = 0;
    if (FAILED(buf->Lock(&raw, nullptr, &rawLen))) return false;
    const uint32_t srcStride = std::max<uint32_t>(static_cast<uint32_t>(std::abs(defaultStride_)), codedW_);
    if (rawLen >= static_cast<size_t>(srcStride) * (codedH_ + outH / 2)) {
        const BYTE* srcY = raw;
        const BYTE* srcUV = raw + static_cast<size_t>(srcStride) * codedH_;
        for (uint32_t r = 0; r < outH; ++r) {
            std::memcpy(outFrame.data.data() + static_cast<size_t>(r) * outW, srcY + static_cast<size_t>(r) * srcStride, outW);
        }
        uint8_t* dstUV = outFrame.data.data() + static_cast<size_t>(outW) * outH;
        for (uint32_t r = 0; r < outH / 2; ++r) {
            std::memcpy(dstUV + static_cast<size_t>(r) * outW, srcUV + static_cast<size_t>(r) * srcStride, outW);
        }
        buf->Unlock();
        return true;
    }
    buf->Unlock();
    return false;
}

bool VideoDecoder::DecodeMjpeg(const uint8_t* jpegData, size_t size, DecodedFrame& outFrame) {
    if (!wicFactory_ || !jpegData || size == 0) return false;

    ComPtr<IWICStream> stream;
    if (FAILED(wicFactory_->CreateStream(&stream))) return false;
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(jpegData), static_cast<DWORD>(size)))) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wicFactory_->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) {
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory_->CreateFormatConverter(&converter))) return false;
    if (FAILED(converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom))) {
        return false;
    }

    outFrame.isBgra = true;
    outFrame.width = w;
    outFrame.height = h;
    outFrame.stride = w * 4u;
    outFrame.data.resize(static_cast<size_t>(w) * h * 4u);

    if (FAILED(converter->CopyPixels(nullptr, outFrame.stride, static_cast<UINT>(outFrame.data.size()), outFrame.data.data()))) {
        return false;
    }
    return true;
}

} // namespace phonecam
