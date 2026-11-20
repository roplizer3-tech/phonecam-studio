#include "MFSource.h"
#include "../common/ColorConvert.h"

#include <algorithm>
#include <array>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace phonecam::mf {

namespace {

HRESULT CreateVideoMediaType(
    const GUID& subtype,
    uint32_t width,
    uint32_t height,
    uint32_t fpsNum,
    uint32_t fpsDen,
    IMFMediaType** ppType) {
    ComPtr<IMFMediaType> mt;
    HRESULT hr = MFCreateMediaType(&mt);
    if (FAILED(hr)) return hr;

    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mt->SetGUID(MF_MT_SUBTYPE, subtype);
    MFSetAttributeSize(mt.Get(), MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(mt.Get(), MF_MT_FRAME_RATE, fpsNum, fpsDen);
    MFSetAttributeRatio(mt.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    mt->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    mt->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    mt->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);

    UINT32 stride = width;
    UINT32 sampleSize = width * height * 3u / 2u;
    if (subtype == MFVideoFormat_RGB32) {
        stride = width * 4u;
        sampleSize = stride * height;
    }
    mt->SetUINT32(MF_MT_DEFAULT_STRIDE, stride);
    mt->SetUINT32(MF_MT_SAMPLE_SIZE, sampleSize);

    *ppType = mt.Detach();
    return S_OK;
}

} // namespace

// ============================================================================
// PhoneCamMediaStream
// ============================================================================

PhoneCamMediaStream::PhoneCamMediaStream(PhoneCamMediaSource* parent, IMFStreamDescriptor* sd)
    : parent_(parent), streamDesc_(sd) {
    IncrementModuleRef();
    MFCreateEventQueue(&eventQueue_);
    MFCreateAttributes(&streamAttrs_, 6);
    if (streamAttrs_) {
        streamAttrs_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
        streamAttrs_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
        streamAttrs_->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
        streamAttrs_->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);
    }
    QueryPerformanceFrequency(&qpcFreq_);
    QueryPerformanceCounter(&nextFrameQpc_);
    shmReader_.Open();
}

PhoneCamMediaStream::~PhoneCamMediaStream() {
    shmReader_.Close();
    DecrementModuleRef();
}

IFACEMETHODIMP PhoneCamMediaStream::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator ||
        riid == IID_IMFMediaStream || riid == IID_IMFMediaStream2) {
        *ppv = static_cast<IMFMediaStream2*>(this);
    } else if (riid == __uuidof(IKsControl)) {
        *ppv = static_cast<IKsControl*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) PhoneCamMediaStream::AddRef() {
    return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

IFACEMETHODIMP_(ULONG) PhoneCamMediaStream::Release() {
    const ULONG c = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (c == 0) delete this;
    return c;
}

IFACEMETHODIMP PhoneCamMediaStream::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !eventQueue_) return MF_E_SHUTDOWN;
    return eventQueue_->GetEvent(dwFlags, ppEvent);
}

IFACEMETHODIMP PhoneCamMediaStream::BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !eventQueue_) return MF_E_SHUTDOWN;
    return eventQueue_->BeginGetEvent(pCallback, punkState);
}

IFACEMETHODIMP PhoneCamMediaStream::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !eventQueue_) return MF_E_SHUTDOWN;
    return eventQueue_->EndGetEvent(pResult, ppEvent);
}

IFACEMETHODIMP PhoneCamMediaStream::QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !eventQueue_) return MF_E_SHUTDOWN;
    return eventQueue_->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue);
}

IFACEMETHODIMP PhoneCamMediaStream::GetMediaSource(IMFMediaSource** ppMediaSource) {
    if (!ppMediaSource) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !parent_) return MF_E_SHUTDOWN;
    return parent_->QueryInterface(IID_PPV_ARGS(ppMediaSource));
}

IFACEMETHODIMP PhoneCamMediaStream::GetStreamDescriptor(IMFStreamDescriptor** ppStreamDescriptor) {
    if (!ppStreamDescriptor) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !streamDesc_) return MF_E_SHUTDOWN;
    *ppStreamDescriptor = streamDesc_.Get();
    (*ppStreamDescriptor)->AddRef();
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaStream::SetStreamState(MF_STREAM_STATE value) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_) return MF_E_SHUTDOWN;
    state_ = value;
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaStream::GetStreamState(MF_STREAM_STATE* value) {
    if (!value) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_) return MF_E_SHUTDOWN;
    *value = state_;
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaStream::KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG* BytesReturned) {
    if (BytesReturned) *BytesReturned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP PhoneCamMediaStream::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG* BytesReturned) {
    if (BytesReturned) *BytesReturned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP PhoneCamMediaStream::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG* BytesReturned) {
    if (BytesReturned) *BytesReturned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

HRESULT PhoneCamMediaStream::Start(IMFMediaType* mediaType, const PROPVARIANT* varStartPosition) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_) return MF_E_SHUTDOWN;

    if (mediaType) {
        GUID sub{};
        if (SUCCEEDED(mediaType->GetGUID(MF_MT_SUBTYPE, &sub))) {
            activeSubtype_ = sub;
        }
        UINT32 w = 1280, h = 720, fn = 30, fd = 1;
        if (SUCCEEDED(MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, &w, &h)) && w > 0 && h > 0) {
            outW_ = w;
            outH_ = h;
        }
        if (SUCCEEDED(MFGetAttributeRatio(mediaType, MF_MT_FRAME_RATE, &fn, &fd)) && fn > 0 && fd > 0) {
            fpsNum_ = fn;
            fpsDen_ = fd;
        }
    }

    state_ = MF_STREAM_STATE_RUNNING;
    QueryPerformanceCounter(&nextFrameQpc_);
    if (eventQueue_) {
        eventQueue_->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, varStartPosition);
    }
    return S_OK;
}

HRESULT PhoneCamMediaStream::Stop() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_) return MF_E_SHUTDOWN;
    state_ = MF_STREAM_STATE_STOPPED;
    if (eventQueue_) {
        eventQueue_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
    }
    return S_OK;
}

HRESULT PhoneCamMediaStream::Pause() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_) return MF_E_SHUTDOWN;
    state_ = MF_STREAM_STATE_PAUSED;
    if (eventQueue_) {
        eventQueue_->QueueEventParamVar(MEStreamPaused, GUID_NULL, S_OK, nullptr);
    }
    return S_OK;
}

HRESULT PhoneCamMediaStream::Shutdown() {
    std::lock_guard<std::mutex> lk(mutex_);
    shutdown_ = true;
    parent_ = nullptr;
    if (eventQueue_) {
        eventQueue_->Shutdown();
        eventQueue_.Reset();
    }
    streamDesc_.Reset();
    streamAttrs_.Reset();
    shmReader_.Close();
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaStream::RequestSample(IUnknown* pToken) {
    uint32_t outW = 1280, outH = 720, fpsNum = 30, fpsDen = 1;
    GUID subtype = MFVideoFormat_NV12;
    LARGE_INTEGER targetQpc{};
    LARGE_INTEGER freq{};
    LONGLONG sampleTime = 0;
    LONGLONG sampleDur = 333333LL;

    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (shutdown_) return MF_E_SHUTDOWN;
        if (state_ != MF_STREAM_STATE_RUNNING) return MF_E_INVALIDREQUEST;

        outW = outW_;
        outH = outH_;
        fpsNum = fpsNum_;
        fpsDen = fpsDen_;
        subtype = activeSubtype_;
        freq = qpcFreq_;
        sampleDur = static_cast<LONGLONG>((10000000ULL * fpsDen) / std::max(1u, fpsNum));
        sampleTime = sampleTime100ns_;
        sampleTime100ns_ += sampleDur;

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const LONGLONG stepTicks = (freq.QuadPart * fpsDen) / std::max(1u, fpsNum);
        if (nextFrameQpc_.QuadPart < now.QuadPart - stepTicks * 2) {
            nextFrameQpc_.QuadPart = now.QuadPart;
        }
        targetQpc = nextFrameQpc_;
        nextFrameQpc_.QuadPart += stepTicks;
    }

    // Pace sample emission cleanly without holding mutex
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (targetQpc.QuadPart > now.QuadPart && freq.QuadPart > 0) {
        const LONGLONG waitMs = ((targetQpc.QuadPart - now.QuadPart) * 1000LL) / freq.QuadPart;
        if (waitMs > 0 && waitMs <= 50) {
            Sleep(static_cast<DWORD>(waitMs));
        }
    }

    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || state_ != MF_STREAM_STATE_RUNNING || !eventQueue_) {
        return MF_E_SHUTDOWN;
    }

    uint32_t srcW = 0, srcH = 0, srcFn = 30, srcFd = 1;
    uint64_t seq = 0;
    const bool hasLive = shmReader_.ReadLatestFrameNV12(rawNv12_, srcW, srcH, srcFn, srcFd, seq);
    if (!hasLive) {
        srcW = outW;
        srcH = outH;
        RenderStandbyNV12(rawNv12_, srcW, srcH, ++frameTick_);
    }

    const uint8_t* nv12Ready = rawNv12_.data();
    if (srcW != outW || srcH != outH) {
        scaledNv12_.resize(static_cast<size_t>(outW) * outH * 3u / 2u);
        ResizeNV12(rawNv12_.data(), srcW, srcH, scaledNv12_.data(), outW, outH);
        nv12Ready = scaledNv12_.data();
    }

    const bool isRgb32 = (subtype == MFVideoFormat_RGB32);
    const DWORD bufSize = isRgb32
        ? (outW * outH * 4u)
        : (outW * outH * 3u / 2u);

    ComPtr<IMFMediaBuffer> buf;
    HRESULT hr = MFCreateMemoryBuffer(bufSize, &buf);
    if (FAILED(hr)) return hr;

    BYTE* dst = nullptr;
    if (FAILED(hr = buf->Lock(&dst, nullptr, nullptr))) return hr;
    if (isRgb32) {
        NV12ToBGRA32(nv12Ready, outW, outH, dst, outW * 4u);
    } else {
        std::memcpy(dst, nv12Ready, bufSize);
    }
    buf->Unlock();
    buf->SetCurrentLength(bufSize);

    ComPtr<IMFSample> sample;
    if (FAILED(hr = MFCreateSample(&sample))) return hr;
    sample->AddBuffer(buf.Get());
    sample->SetSampleTime(sampleTime);
    sample->SetSampleDuration(sampleDur);
    sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
    if (pToken) {
        sample->SetUnknown(MFSampleExtension_Token, pToken);
    }

    PROPVARIANT var{};
    var.vt = VT_UNKNOWN;
    var.punkVal = sample.Get();
    return eventQueue_->QueueEventParamVar(MEMediaSample, GUID_NULL, S_OK, &var);
}

// ============================================================================
// PhoneCamMediaSource
// ============================================================================

HRESULT PhoneCamMediaSource::CreateInstance(PhoneCamMediaSource** ppSource) {
    if (!ppSource) return E_POINTER;
    *ppSource = nullptr;
    auto* src = new PhoneCamMediaSource();
    HRESULT hr = src->Initialize();
    if (FAILED(hr)) {
        src->Release();
        return hr;
    }
    *ppSource = src;
    return S_OK;
}

PhoneCamMediaSource::~PhoneCamMediaSource() {
    Shutdown();
    DecrementModuleRef();
}

HRESULT PhoneCamMediaSource::Initialize() {
    IncrementModuleRef();
    HRESULT hr = MFCreateEventQueue(&eventQueue_);
    if (FAILED(hr)) return hr;

    hr = MFCreateAttributes(&sourceAttrs_, 4);
    if (FAILED(hr)) return hr;
    sourceAttrs_->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    sourceAttrs_->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, L"PhoneCam Studio");

    struct MediaSpec {
        GUID subtype;
        uint32_t w, h, fps;
    };
    static const std::array<MediaSpec, 5> kSpecs = {{
        { MFVideoFormat_NV12, 1280, 720, 30 },
        { MFVideoFormat_NV12, 1920, 1080, 30 },
        { MFVideoFormat_NV12, 1280, 720, 60 },
        { MFVideoFormat_RGB32, 1280, 720, 30 },
        { MFVideoFormat_RGB32, 1920, 1080, 30 }
    }};

    std::array<IMFMediaType*, kSpecs.size()> types{};
    for (size_t i = 0; i < kSpecs.size(); ++i) {
        hr = CreateVideoMediaType(kSpecs[i].subtype, kSpecs[i].w, kSpecs[i].h, kSpecs[i].fps, 1, &types[i]);
        if (FAILED(hr)) {
            for (size_t j = 0; j < i; ++j) types[j]->Release();
            return hr;
        }
    }

    ComPtr<IMFStreamDescriptor> sd;
    hr = MFCreateStreamDescriptor(0, static_cast<DWORD>(types.size()), types.data(), &sd);
    ComPtr<IMFMediaTypeHandler> handler;
    if (SUCCEEDED(hr) && SUCCEEDED(sd->GetMediaTypeHandler(&handler))) {
        handler->SetCurrentMediaType(types[0]);
    }
    for (IMFMediaType* t : types) t->Release();
    if (FAILED(hr)) return hr;

    sd->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
    sd->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
    sd->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
    sd->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);

    IMFStreamDescriptor* sdArr[1] = { sd.Get() };
    hr = MFCreatePresentationDescriptor(1, sdArr, &presDesc_);
    if (FAILED(hr)) return hr;
    presDesc_->SelectStream(0);

    stream_.Attach(new PhoneCamMediaStream(this, sd.Get()));
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaSource::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator ||
        riid == IID_IMFMediaSource || riid == IID_IMFMediaSourceEx) {
        *ppv = static_cast<IMFMediaSourceEx*>(this);
    } else if (riid == IID_IMFGetService) {
        *ppv = static_cast<IMFGetService*>(this);
    } else if (riid == __uuidof(IKsControl)) {
        *ppv = static_cast<IKsControl*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) PhoneCamMediaSource::AddRef() {
    return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

IFACEMETHODIMP_(ULONG) PhoneCamMediaSource::Release() {
    const ULONG c = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (c == 0) delete this;
    return c;
}

IFACEMETHODIMP PhoneCamMediaSource::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !eventQueue_) return MF_E_SHUTDOWN;
    return eventQueue_->GetEvent(dwFlags, ppEvent);
}

IFACEMETHODIMP PhoneCamMediaSource::BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !eventQueue_) return MF_E_SHUTDOWN;
    return eventQueue_->BeginGetEvent(pCallback, punkState);
}

IFACEMETHODIMP PhoneCamMediaSource::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !eventQueue_) return MF_E_SHUTDOWN;
    return eventQueue_->EndGetEvent(pResult, ppEvent);
}

IFACEMETHODIMP PhoneCamMediaSource::QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !eventQueue_) return MF_E_SHUTDOWN;
    return eventQueue_->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue);
}

IFACEMETHODIMP PhoneCamMediaSource::GetCharacteristics(DWORD* pdwCharacteristics) {
    if (!pdwCharacteristics) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_) return MF_E_SHUTDOWN;
    *pdwCharacteristics = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** ppPresentationDescriptor) {
    if (!ppPresentationDescriptor) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !presDesc_) return MF_E_SHUTDOWN;
    return presDesc_->Clone(ppPresentationDescriptor);
}

IFACEMETHODIMP PhoneCamMediaSource::Start(
    IMFPresentationDescriptor* pPresentationDescriptor,
    const GUID* pguidTimeFormat,
    const PROPVARIANT* pvarStartPosition) {
    if (pguidTimeFormat && *pguidTimeFormat != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;

    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !eventQueue_ || !stream_) return MF_E_SHUTDOWN;

    ComPtr<IMFMediaType> selectedType;
    if (pPresentationDescriptor) {
        BOOL selected = FALSE;
        ComPtr<IMFStreamDescriptor> sd;
        if (SUCCEEDED(pPresentationDescriptor->GetStreamDescriptorByIndex(0, &selected, &sd)) && selected) {
            ComPtr<IMFMediaTypeHandler> handler;
            if (SUCCEEDED(sd->GetMediaTypeHandler(&handler))) {
                handler->GetCurrentMediaType(&selectedType);
            }
        }
    }

    PROPVARIANT var{};
    PropVariantInit(&var);
    var.vt = VT_UNKNOWN;
    var.punkVal = stream_.Get();
    eventQueue_->QueueEventParamVar(streamAnnounced_ ? MEUpdatedStream : MENewStream, GUID_NULL, S_OK, &var);
    streamAnnounced_ = true;

    HRESULT hr = stream_->Start(selectedType.Get(), pvarStartPosition);
    if (FAILED(hr)) return hr;

    return eventQueue_->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, pvarStartPosition);
}

IFACEMETHODIMP PhoneCamMediaSource::Stop() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_) return MF_E_SHUTDOWN;
    if (stream_) stream_->Stop();
    if (eventQueue_) eventQueue_->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaSource::Pause() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_) return MF_E_SHUTDOWN;
    if (stream_) stream_->Pause();
    if (eventQueue_) eventQueue_->QueueEventParamVar(MESourcePaused, GUID_NULL, S_OK, nullptr);
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaSource::Shutdown() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_) return S_OK;
    shutdown_ = true;
    if (stream_) {
        stream_->Shutdown();
        stream_.Reset();
    }
    if (eventQueue_) {
        eventQueue_->Shutdown();
        eventQueue_.Reset();
    }
    presDesc_.Reset();
    sourceAttrs_.Reset();
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaSource::GetSourceAttributes(IMFAttributes** ppAttributes) {
    if (!ppAttributes) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !sourceAttrs_) return MF_E_SHUTDOWN;
    *ppAttributes = sourceAttrs_.Get();
    (*ppAttributes)->AddRef();
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaSource::GetStreamAttributes(DWORD dwStreamIdentifier, IMFAttributes** ppAttributes) {
    if (!ppAttributes) return E_POINTER;
    if (dwStreamIdentifier != 0) return MF_E_INVALIDSTREAMNUMBER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !stream_) return MF_E_SHUTDOWN;
    IMFAttributes* attrs = stream_->Attributes();
    if (!attrs) return E_FAIL;
    *ppAttributes = attrs;
    attrs->AddRef();
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaSource::SetD3DManager(IUnknown*) {
    // Intentionally CPU memory sample path for universal WhatsApp/Teams/browser compatibility
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaSource::GetService(REFGUID, REFIID riid, LPVOID* ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;
    if (riid == __uuidof(IKsControl)) {
        return QueryInterface(riid, ppvObject);
    }
    return MF_E_UNSUPPORTED_SERVICE;
}

IFACEMETHODIMP PhoneCamMediaSource::KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG* BytesReturned) {
    if (BytesReturned) *BytesReturned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP PhoneCamMediaSource::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG* BytesReturned) {
    if (BytesReturned) *BytesReturned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

IFACEMETHODIMP PhoneCamMediaSource::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG* BytesReturned) {
    if (BytesReturned) *BytesReturned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

// ============================================================================
// PhoneCamActivator
// ============================================================================

HRESULT PhoneCamActivator::CreateInstance(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    auto* act = new PhoneCamActivator();
    HRESULT hr = act->QueryInterface(riid, ppv);
    act->Release();
    return hr;
}

PhoneCamActivator::PhoneCamActivator() {
    IncrementModuleRef();
    MFCreateAttributes(&attrs_, 4);
}

PhoneCamActivator::~PhoneCamActivator() {
    DecrementModuleRef();
}

IFACEMETHODIMP PhoneCamActivator::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IMFAttributes || riid == IID_IMFActivate) {
        *ppv = static_cast<IMFActivate*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) PhoneCamActivator::AddRef() {
    return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

IFACEMETHODIMP_(ULONG) PhoneCamActivator::Release() {
    const ULONG c = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (c == 0) delete this;
    return c;
}

IFACEMETHODIMP PhoneCamActivator::GetItem(REFGUID guidKey, PROPVARIANT* pValue) { return attrs_->GetItem(guidKey, pValue); }
IFACEMETHODIMP PhoneCamActivator::GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType) { return attrs_->GetItemType(guidKey, pType); }
IFACEMETHODIMP PhoneCamActivator::CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult) { return attrs_->CompareItem(guidKey, Value, pbResult); }
IFACEMETHODIMP PhoneCamActivator::Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult) { return attrs_->Compare(pTheirs, MatchType, pbResult); }
IFACEMETHODIMP PhoneCamActivator::GetUINT32(REFGUID guidKey, UINT32* punValue) { return attrs_->GetUINT32(guidKey, punValue); }
IFACEMETHODIMP PhoneCamActivator::GetUINT64(REFGUID guidKey, UINT64* punValue) { return attrs_->GetUINT64(guidKey, punValue); }
IFACEMETHODIMP PhoneCamActivator::GetDouble(REFGUID guidKey, double* pfValue) { return attrs_->GetDouble(guidKey, pfValue); }
IFACEMETHODIMP PhoneCamActivator::GetGUID(REFGUID guidKey, GUID* pguidValue) { return attrs_->GetGUID(guidKey, pguidValue); }
IFACEMETHODIMP PhoneCamActivator::GetStringLength(REFGUID guidKey, UINT32* pcchLength) { return attrs_->GetStringLength(guidKey, pcchLength); }
IFACEMETHODIMP PhoneCamActivator::GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength) { return attrs_->GetString(guidKey, pwszValue, cchBufSize, pcchLength); }
IFACEMETHODIMP PhoneCamActivator::GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength) { return attrs_->GetAllocatedString(guidKey, ppwszValue, pcchLength); }
IFACEMETHODIMP PhoneCamActivator::GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) { return attrs_->GetBlobSize(guidKey, pcbBlobSize); }
IFACEMETHODIMP PhoneCamActivator::GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) { return attrs_->GetBlob(guidKey, pBuf, cbBufSize, pcbBlobSize); }
IFACEMETHODIMP PhoneCamActivator::GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize) { return attrs_->GetAllocatedBlob(guidKey, ppBuf, pcbSize); }
IFACEMETHODIMP PhoneCamActivator::GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv) { return attrs_->GetUnknown(guidKey, riid, ppv); }
IFACEMETHODIMP PhoneCamActivator::SetItem(REFGUID guidKey, REFPROPVARIANT Value) { return attrs_->SetItem(guidKey, Value); }
IFACEMETHODIMP PhoneCamActivator::DeleteItem(REFGUID guidKey) { return attrs_->DeleteItem(guidKey); }
IFACEMETHODIMP PhoneCamActivator::DeleteAllItems() { return attrs_->DeleteAllItems(); }
IFACEMETHODIMP PhoneCamActivator::SetUINT32(REFGUID guidKey, UINT32 unValue) { return attrs_->SetUINT32(guidKey, unValue); }
IFACEMETHODIMP PhoneCamActivator::SetUINT64(REFGUID guidKey, UINT64 unValue) { return attrs_->SetUINT64(guidKey, unValue); }
IFACEMETHODIMP PhoneCamActivator::SetDouble(REFGUID guidKey, double fValue) { return attrs_->SetDouble(guidKey, fValue); }
IFACEMETHODIMP PhoneCamActivator::SetGUID(REFGUID guidKey, REFGUID guidValue) { return attrs_->SetGUID(guidKey, guidValue); }
IFACEMETHODIMP PhoneCamActivator::SetString(REFGUID guidKey, LPCWSTR wszValue) { return attrs_->SetString(guidKey, wszValue); }
IFACEMETHODIMP PhoneCamActivator::SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize) { return attrs_->SetBlob(guidKey, pBuf, cbBufSize); }
IFACEMETHODIMP PhoneCamActivator::SetUnknown(REFGUID guidKey, IUnknown* pUnknown) { return attrs_->SetUnknown(guidKey, pUnknown); }
IFACEMETHODIMP PhoneCamActivator::LockStore() { return attrs_->LockStore(); }
IFACEMETHODIMP PhoneCamActivator::UnlockStore() { return attrs_->UnlockStore(); }
IFACEMETHODIMP PhoneCamActivator::GetCount(UINT32* pcItems) { return attrs_->GetCount(pcItems); }
IFACEMETHODIMP PhoneCamActivator::GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue) { return attrs_->GetItemByIndex(unIndex, pguidKey, pValue); }
IFACEMETHODIMP PhoneCamActivator::CopyAllItems(IMFAttributes* pDest) { return attrs_->CopyAllItems(pDest); }

IFACEMETHODIMP PhoneCamActivator::ActivateObject(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (!source_) {
        HRESULT hr = PhoneCamMediaSource::CreateInstance(&source_);
        if (FAILED(hr)) return hr;
    }
    return source_->QueryInterface(riid, ppv);
}

IFACEMETHODIMP PhoneCamActivator::ShutdownObject() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (source_) {
        source_->Shutdown();
        source_.Reset();
    }
    return S_OK;
}

IFACEMETHODIMP PhoneCamActivator::DetachObject() {
    std::lock_guard<std::mutex> lk(mutex_);
    source_.Reset();
    return S_OK;
}

} // namespace phonecam::mf
