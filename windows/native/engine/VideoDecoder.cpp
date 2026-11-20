#include "VideoDecoder.h"

#include <mfapi.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <codecapi.h>
#include <propvarutil.h>

#include <algorithm>
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

    return InitH264Mft();
}

void VideoDecoder::Shutdown() {
    if (h264Mft_) {
        h264Mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        h264Mft_.Reset();
    }
    wicFactory_.Reset();
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
    InitH264Mft();
}

bool VideoDecoder::InitH264Mft() {
    h264OutputReady_ = false;
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

    ComPtr<IMFMediaType> inType;
    if (FAILED(MFCreateMediaType(&inType))) return false;
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, 1280, 720);
    MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, 30, 1);

    hr = h264Mft_->SetInputType(0, inType.Get(), 0);
    if (FAILED(hr)) return false;

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
    if (!h264Mft_ && !InitH264Mft()) return false;

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
    inSample->SetSampleDuration(333333LL);

    HRESULT hr = h264Mft_->ProcessInput(0, inSample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
        // Drain pending output and retry once
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
    if (FAILED(hr)) return false;

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

bool VideoDecoder::ReadH264OutputSample(IMFSample* sample, DecodedFrame& outFrame) {
    ComPtr<IMFMediaBuffer> buf;
    if (FAILED(sample->ConvertToContiguousBuffer(&buf))) return false;

    const uint32_t outW = displayW_;
    const uint32_t outH = displayH_;
    if (outW < 16 || outH < 16) return false;

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
    outFrame.data.resize(static_cast<size_t>(outFrame.stride) * h);

    return SUCCEEDED(converter->CopyPixels(
        nullptr,
        outFrame.stride,
        static_cast<UINT>(outFrame.data.size()),
        outFrame.data.data()));
}

} // namespace phonecam
