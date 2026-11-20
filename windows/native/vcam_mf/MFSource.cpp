#include "MFSource.h"
#include "../common/ColorConvert.h"

#include <algorithm>
#include <array>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace phonecam::mf {

namespace {

using FnMFCreateSensorProfileCollection = HRESULT(STDAPICALLTYPE*)(IMFSensorProfileCollection**);
using FnMFCreateSensorProfile = HRESULT(STDAPICALLTYPE*)(REFGUID, UINT32, LPCWSTR, IMFSensorProfile**);

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
    UINT32 avgBitrate = static_cast<UINT32>((static_cast<uint64_t>(width) * height * 12ULL * fpsNum) / std::max(1u, fpsDen));
    if (subtype == MFVideoFormat_RGB32) {
        stride = width * 4u;
        sampleSize = stride * height;
        avgBitrate = static_cast<UINT32>((static_cast<uint64_t>(width) * height * 32ULL * fpsNum) / std::max(1u, fpsDen));
    }
    mt->SetUINT32(MF_MT_DEFAULT_STRIDE, stride);
    mt->SetUINT32(MF_MT_SAMPLE_SIZE, sampleSize);
    mt->SetUINT32(MF_MT_AVG_BITRATE, avgBitrate);

    *ppType = mt.Detach();
    return S_OK;
}

HRESULT CopyFrameIntoBuffer(
    IMFMediaBuffer* buf,
    const uint8_t* nv12Src,
    uint32_t width,
    uint32_t height,
    bool isRgb32,
    std::vector<uint8_t>& rgbScratch) {
    if (!buf || !nv12Src) return E_POINTER;

    const DWORD packedSize = isRgb32 ? (width * height * 4u) : (width * height * 3u / 2u);
    if (isRgb32) {
        rgbScratch.resize(packedSize);
        NV12ToBGRA32(nv12Src, width, height, rgbScratch.data(), width * 4u);
    }

    ComPtr<IMF2DBuffer2> buf2d2;
    BYTE* scan0 = nullptr;
    LONG pitch = 0;
    BYTE* bufStart = nullptr;
    DWORD bufLen = 0;

    if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&buf2d2))) &&
        SUCCEEDED(buf2d2->Lock2DSize(MF2DBuffer_LockFlags_Write, &scan0, &pitch, &bufStart, &bufLen))) {
        if (isRgb32) {
            MFCopyImage(scan0, pitch, rgbScratch.data(), static_cast<LONG>(width * 4u), width * 4u, height);
        } else {
            MFCopyImage(scan0, pitch, nv12Src, static_cast<LONG>(width), width, height);
            BYTE* dstUv = scan0 + static_cast<size_t>(std::abs(pitch)) * height;
            const uint8_t* srcUv = nv12Src + static_cast<size_t>(width) * height;
            MFCopyImage(dstUv, pitch, srcUv, static_cast<LONG>(width), width, height / 2);
        }
        buf2d2->Unlock2D();
        buf->SetCurrentLength(packedSize);
        return S_OK;
    }

    ComPtr<IMF2DBuffer> buf2d;
    if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&buf2d))) &&
        SUCCEEDED(buf2d->Lock2D(&scan0, &pitch))) {
        if (isRgb32) {
            MFCopyImage(scan0, pitch, rgbScratch.data(), static_cast<LONG>(width * 4u), width * 4u, height);
        } else {
            MFCopyImage(scan0, pitch, nv12Src, static_cast<LONG>(width), width, height);
            BYTE* dstUv = scan0 + static_cast<size_t>(std::abs(pitch)) * height;
            const uint8_t* srcUv = nv12Src + static_cast<size_t>(width) * height;
            MFCopyImage(dstUv, pitch, srcUv, static_cast<LONG>(width), width, height / 2);
        }
        buf2d->Unlock2D();
        buf->SetCurrentLength(packedSize);
        return S_OK;
    }

    BYTE* dst = nullptr;
    DWORD maxLen = 0;
    HRESULT hr = buf->Lock(&dst, &maxLen, nullptr);
    if (FAILED(hr)) return hr;
    if (maxLen < packedSize) {
        buf->Unlock();
        return MF_E_BUFFERTOOSMALL;
    }
    if (isRgb32) {
        std::memcpy(dst, rgbScratch.data(), packedSize);
    } else {
        std::memcpy(dst, nv12Src, packedSize);
    }
    buf->Unlock();
    buf->SetCurrentLength(packedSize);
    return S_OK;
}

} // namespace

// ============================================================================
// PhoneCamMediaStream
// ============================================================================

PhoneCamMediaStream::PhoneCamMediaStream(PhoneCamMediaSource* parent, int index)
    : parent_(parent), index_(index) {
    IncrementModuleRef();
    MFCreateAttributes(&attrs_, 8);
    MFCreateEventQueue(&eventQueue_);
    QueryPerformanceFrequency(&qpcFreq_);
    QueryPerformanceCounter(&nextFrameQpc_);
}

PhoneCamMediaStream::~PhoneCamMediaStream() {
    shmReader_.Close();
    DecrementModuleRef();
}

HRESULT PhoneCamMediaStream::Initialize() {
    if (!attrs_ || !eventQueue_) return E_OUTOFMEMORY;

    attrs_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
    attrs_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, static_cast<UINT32>(index_));
    attrs_->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
    attrs_->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);

    struct MediaSpec {
        GUID subtype;
        uint32_t w, h, fps;
    };
    static const std::array<MediaSpec, 5> kSpecs = {{
        { MFVideoFormat_NV12, 1280, 720, 60 },
        { MFVideoFormat_NV12, 1280, 720, 30 },
        { MFVideoFormat_NV12, 1920, 1080, 30 },
        { MFVideoFormat_RGB32, 1280, 720, 30 },
        { MFVideoFormat_RGB32, 1920, 1080, 30 }
    }};

    std::array<IMFMediaType*, kSpecs.size()> types{};
    HRESULT hr = S_OK;
    for (size_t i = 0; i < kSpecs.size(); ++i) {
        hr = CreateVideoMediaType(kSpecs[i].subtype, kSpecs[i].w, kSpecs[i].h, kSpecs[i].fps, 1, &types[i]);
        if (FAILED(hr)) {
            for (size_t j = 0; j < i; ++j) types[j]->Release();
            return hr;
        }
    }

    hr = MFCreateStreamDescriptor(static_cast<DWORD>(index_), static_cast<DWORD>(types.size()), types.data(), &streamDesc_);
    if (SUCCEEDED(hr) && streamDesc_) {
        defaultType_ = types[0];
        activeType_ = types[0];
        ComPtr<IMFMediaTypeHandler> handler;
        if (SUCCEEDED(streamDesc_->GetMediaTypeHandler(&handler))) {
            handler->SetCurrentMediaType(types[0]);
        }
        streamDesc_->SetUINT32(MF_DEVICESTREAM_STREAM_ID, static_cast<UINT32>(index_));
        streamDesc_->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
        streamDesc_->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
        streamDesc_->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);
    }
    for (IMFMediaType* t : types) t->Release();
    if (FAILED(hr)) return hr;

    shmReader_.Open();
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaStream::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator ||
        riid == IID_IMFMediaStream || riid == IID_IMFMediaStream2) {
        *ppv = static_cast<IMFMediaStream2*>(this);
    } else if (riid == IID_IMFAttributes) {
        *ppv = static_cast<IMFAttributes*>(this);
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

// IMFAttributes delegation
IFACEMETHODIMP PhoneCamMediaStream::GetItem(REFGUID guidKey, PROPVARIANT* pValue) { return attrs_->GetItem(guidKey, pValue); }
IFACEMETHODIMP PhoneCamMediaStream::GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType) { return attrs_->GetItemType(guidKey, pType); }
IFACEMETHODIMP PhoneCamMediaStream::CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult) { return attrs_->CompareItem(guidKey, Value, pbResult); }
IFACEMETHODIMP PhoneCamMediaStream::Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult) { return attrs_->Compare(pTheirs, MatchType, pbResult); }
IFACEMETHODIMP PhoneCamMediaStream::GetUINT32(REFGUID guidKey, UINT32* punValue) { return attrs_->GetUINT32(guidKey, punValue); }
IFACEMETHODIMP PhoneCamMediaStream::GetUINT64(REFGUID guidKey, UINT64* punValue) { return attrs_->GetUINT64(guidKey, punValue); }
IFACEMETHODIMP PhoneCamMediaStream::GetDouble(REFGUID guidKey, double* pfValue) { return attrs_->GetDouble(guidKey, pfValue); }
IFACEMETHODIMP PhoneCamMediaStream::GetGUID(REFGUID guidKey, GUID* pguidValue) { return attrs_->GetGUID(guidKey, pguidValue); }
IFACEMETHODIMP PhoneCamMediaStream::GetStringLength(REFGUID guidKey, UINT32* pcchLength) { return attrs_->GetStringLength(guidKey, pcchLength); }
IFACEMETHODIMP PhoneCamMediaStream::GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength) { return attrs_->GetString(guidKey, pwszValue, cchBufSize, pcchLength); }
IFACEMETHODIMP PhoneCamMediaStream::GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength) { return attrs_->GetAllocatedString(guidKey, ppwszValue, pcchLength); }
IFACEMETHODIMP PhoneCamMediaStream::GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) { return attrs_->GetBlobSize(guidKey, pcbBlobSize); }
IFACEMETHODIMP PhoneCamMediaStream::GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) { return attrs_->GetBlob(guidKey, pBuf, cbBufSize, pcbBlobSize); }
IFACEMETHODIMP PhoneCamMediaStream::GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize) { return attrs_->GetAllocatedBlob(guidKey, ppBuf, pcbSize); }
IFACEMETHODIMP PhoneCamMediaStream::GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv) { return attrs_->GetUnknown(guidKey, riid, ppv); }
IFACEMETHODIMP PhoneCamMediaStream::SetItem(REFGUID guidKey, REFPROPVARIANT Value) { return attrs_->SetItem(guidKey, Value); }
IFACEMETHODIMP PhoneCamMediaStream::DeleteItem(REFGUID guidKey) { return attrs_->DeleteItem(guidKey); }
IFACEMETHODIMP PhoneCamMediaStream::DeleteAllItems() { return attrs_->DeleteAllItems(); }
IFACEMETHODIMP PhoneCamMediaStream::SetUINT32(REFGUID guidKey, UINT32 unValue) { return attrs_->SetUINT32(guidKey, unValue); }
IFACEMETHODIMP PhoneCamMediaStream::SetUINT64(REFGUID guidKey, UINT64 unValue) { return attrs_->SetUINT64(guidKey, unValue); }
IFACEMETHODIMP PhoneCamMediaStream::SetDouble(REFGUID guidKey, double fValue) { return attrs_->SetDouble(guidKey, fValue); }
IFACEMETHODIMP PhoneCamMediaStream::SetGUID(REFGUID guidKey, REFGUID guidValue) { return attrs_->SetGUID(guidKey, guidValue); }
IFACEMETHODIMP PhoneCamMediaStream::SetString(REFGUID guidKey, LPCWSTR wszValue) { return attrs_->SetString(guidKey, wszValue); }
IFACEMETHODIMP PhoneCamMediaStream::SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize) { return attrs_->SetBlob(guidKey, pBuf, cbBufSize); }
IFACEMETHODIMP PhoneCamMediaStream::SetUnknown(REFGUID guidKey, IUnknown* pUnknown) { return attrs_->SetUnknown(guidKey, pUnknown); }
IFACEMETHODIMP PhoneCamMediaStream::LockStore() { return attrs_->LockStore(); }
IFACEMETHODIMP PhoneCamMediaStream::UnlockStore() { return attrs_->UnlockStore(); }
IFACEMETHODIMP PhoneCamMediaStream::GetCount(UINT32* pcItems) { return attrs_->GetCount(pcItems); }
IFACEMETHODIMP PhoneCamMediaStream::GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue) { return attrs_->GetItemByIndex(unIndex, pguidKey, pValue); }
IFACEMETHODIMP PhoneCamMediaStream::CopyAllItems(IMFAttributes* pDest) { return attrs_->CopyAllItems(pDest); }

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
    if (value == MF_STREAM_STATE_RUNNING) return Start(nullptr, nullptr);
    if (value == MF_STREAM_STATE_STOPPED) return Stop();
    if (value == MF_STREAM_STATE_PAUSED) return Pause();
    return MF_E_INVALID_STATE_TRANSITION;
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

HRESULT PhoneCamMediaStream::SetAllocator(IUnknown* pAllocator) {
    if (!pAllocator) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    allocator_.Reset();
    return pAllocator->QueryInterface(IID_PPV_ARGS(&allocator_));
}

HRESULT PhoneCamMediaStream::SetD3DManager(IUnknown* pManager) {
    if (!pManager) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (allocator_) {
        allocator_->SetDirectXManager(pManager);
    }
    return S_OK;
}

HRESULT PhoneCamMediaStream::Start(IMFMediaType* mediaType, const PROPVARIANT* varStartPosition) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_) return MF_E_SHUTDOWN;

    IMFMediaType* chosenType = mediaType ? mediaType : (activeType_ ? activeType_.Get() : defaultType_.Get());
    if (chosenType) {
        activeType_ = chosenType;
        GUID sub{};
        if (SUCCEEDED(chosenType->GetGUID(MF_MT_SUBTYPE, &sub))) {
            activeSubtype_ = sub;
        }
        UINT32 w = 1280, h = 720, fn = 60, fd = 1;
        if (SUCCEEDED(MFGetAttributeSize(chosenType, MF_MT_FRAME_SIZE, &w, &h)) && w > 0 && h > 0) {
            outW_ = w;
            outH_ = h;
        }
        if (SUCCEEDED(MFGetAttributeRatio(chosenType, MF_MT_FRAME_RATE, &fn, &fd)) && fn > 0 && fd > 0) {
            fpsNum_ = fn;
            fpsDen_ = fd;
        }
        if (allocator_) {
            if (allocatorInit_) {
                allocator_->UninitializeSampleAllocator();
                allocatorInit_ = false;
            }
            if (SUCCEEDED(allocator_->InitializeSampleAllocator(10, chosenType))) {
                allocatorInit_ = true;
            }
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
    if (allocator_ && allocatorInit_) {
        allocator_->UninitializeSampleAllocator();
        allocatorInit_ = false;
    }
    state_ = MF_STREAM_STATE_STOPPED;
    if (eventQueue_) {
        eventQueue_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
    }
    return S_OK;
}

HRESULT PhoneCamMediaStream::Pause() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_) return MF_E_SHUTDOWN;
    if (state_ != MF_STREAM_STATE_RUNNING) return MF_E_INVALID_STATE_TRANSITION;
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
    if (allocator_ && allocatorInit_) {
        allocator_->UninitializeSampleAllocator();
        allocatorInit_ = false;
    }
    allocator_.Reset();
    if (eventQueue_) {
        eventQueue_->Shutdown();
        eventQueue_.Reset();
    }
    streamDesc_.Reset();
    defaultType_.Reset();
    activeType_.Reset();
    attrs_.Reset();
    shmReader_.Close();
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaStream::RequestSample(IUnknown* pToken) {
    uint32_t outW = 1280, outH = 720, fpsNum = 60, fpsDen = 1;
    LARGE_INTEGER targetQpc{};
    LARGE_INTEGER freq{};

    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (shutdown_) return MF_E_SHUTDOWN;
        if (state_ != MF_STREAM_STATE_RUNNING) return MF_E_INVALIDREQUEST;

        outW = outW_;
        outH = outH_;
        fpsNum = fpsNum_;
        fpsDen = fpsDen_;
        freq = qpcFreq_;

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const LONGLONG stepTicks = (freq.QuadPart * fpsDen) / std::max(1u, fpsNum);
        if (nextFrameQpc_.QuadPart < now.QuadPart - stepTicks * 2) {
            nextFrameQpc_.QuadPart = now.QuadPart;
        }
        targetQpc = nextFrameQpc_;
        nextFrameQpc_.QuadPart += stepTicks;
    }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (targetQpc.QuadPart > now.QuadPart && freq.QuadPart > 0) {
        const LONGLONG waitMs = ((targetQpc.QuadPart - now.QuadPart) * 1000LL) / freq.QuadPart;
        if (waitMs > 0 && waitMs <= 35) {
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

    const bool isRgb32 = (activeSubtype_ == MFVideoFormat_RGB32);
    const DWORD bufSize = isRgb32 ? (outW * outH * 4u) : (outW * outH * 3u / 2u);

    ComPtr<IMFSample> sample;
    if (allocator_ && allocatorInit_) {
        HRESULT hr = allocator_->AllocateSample(&sample);
        if (FAILED(hr) || !sample) return FAILED(hr) ? hr : E_FAIL;
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(hr = sample->GetBufferByIndex(0, &buf))) return hr;
        if (FAILED(hr = CopyFrameIntoBuffer(buf.Get(), nv12Ready, outW, outH, isRgb32, rgbScratch_))) return hr;
    } else {
        ComPtr<IMFMediaBuffer> buf;
        HRESULT hr = MFCreateMemoryBuffer(bufSize, &buf);
        if (FAILED(hr)) return hr;
        if (FAILED(hr = CopyFrameIntoBuffer(buf.Get(), nv12Ready, outW, outH, isRgb32, rgbScratch_))) return hr;
        if (FAILED(hr = MFCreateSample(&sample))) return hr;
        sample->AddBuffer(buf.Get());
    }

    const LONGLONG sampleDur = static_cast<LONGLONG>((10000000ULL * fpsDen) / std::max(1u, fpsNum));
    sample->SetSampleTime(MFGetSystemTime());
    sample->SetSampleDuration(sampleDur);
    sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
    if (pToken) {
        sample->SetUnknown(MFSampleExtension_Token, pToken);
    }

    return eventQueue_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
}

// ============================================================================
// PhoneCamMediaSource
// ============================================================================

HRESULT PhoneCamMediaSource::CreateInstance(IMFAttributes* activatorAttrs, PhoneCamMediaSource** ppSource) {
    if (!ppSource) return E_POINTER;
    *ppSource = nullptr;
    auto* src = new PhoneCamMediaSource();
    HRESULT hr = src->Initialize(activatorAttrs);
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

HRESULT PhoneCamMediaSource::Initialize(IMFAttributes* activatorAttrs) {
    IncrementModuleRef();

    HRESULT hr = MFCreateAttributes(&attrs_, 12);
    if (FAILED(hr)) return hr;
    if (activatorAttrs) {
        activatorAttrs->CopyAllItems(attrs_.Get());
    }
    attrs_->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    attrs_->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, L"PhoneCam Studio");

    // Populate Sensor Profile Collection dynamically via mfsensorgroup.dll (Windows 10 1803+ / Windows 11)
    if (HMODULE mfSensor = LoadLibraryW(L"mfsensorgroup.dll")) {
        auto fnCreateCol = reinterpret_cast<FnMFCreateSensorProfileCollection>(
            GetProcAddress(mfSensor, "MFCreateSensorProfileCollection"));
        auto fnCreateProf = reinterpret_cast<FnMFCreateSensorProfile>(
            GetProcAddress(mfSensor, "MFCreateSensorProfile"));
        if (fnCreateCol && fnCreateProf) {
            ComPtr<IMFSensorProfileCollection> col;
            if (SUCCEEDED(fnCreateCol(&col)) && col) {
                ComPtr<IMFSensorProfile> pLegacy;
                if (SUCCEEDED(fnCreateProf(KSCAMERAPROFILE_Legacy, 0, nullptr, &pLegacy)) && pLegacy) {
                    pLegacy->AddProfileFilter(0, L"((RES==;FRT<=30,1;SUT==))");
                    col->AddProfile(pLegacy.Get());
                }
                ComPtr<IMFSensorProfile> pHfr;
                if (SUCCEEDED(fnCreateProf(KSCAMERAPROFILE_HighFrameRate, 0, nullptr, &pHfr)) && pHfr) {
                    pHfr->AddProfileFilter(0, L"((RES==;FRT>=60,1;SUT==))");
                    col->AddProfile(pHfr.Get());
                }
                attrs_->SetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION, col.Get());
            }
        }
        FreeLibrary(mfSensor);
    }

    hr = MFCreateEventQueue(&eventQueue_);
    if (FAILED(hr)) return hr;

    stream_.Attach(new PhoneCamMediaStream(this, 0));
    hr = stream_->Initialize();
    if (FAILED(hr)) return hr;

    ComPtr<IMFStreamDescriptor> sd;
    hr = stream_->GetStreamDescriptor(&sd);
    if (FAILED(hr)) return hr;

    IMFStreamDescriptor* sdArr[1] = { sd.Get() };
    hr = MFCreatePresentationDescriptor(1, sdArr, &presDesc_);
    if (FAILED(hr)) return hr;
    presDesc_->SelectStream(0);

    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaSource::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator ||
        riid == IID_IMFMediaSource || riid == IID_IMFMediaSourceEx) {
        *ppv = static_cast<IMFMediaSourceEx*>(this);
    } else if (riid == IID_IMFAttributes) {
        *ppv = static_cast<IMFAttributes*>(this);
    } else if (riid == IID_IMFGetService) {
        *ppv = static_cast<IMFGetService*>(this);
    } else if (riid == __uuidof(IKsControl)) {
        *ppv = static_cast<IKsControl*>(this);
    } else if (riid == IID_IMFSampleAllocatorControl) {
        *ppv = static_cast<IMFSampleAllocatorControl*>(this);
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

// IMFAttributes delegation
IFACEMETHODIMP PhoneCamMediaSource::GetItem(REFGUID guidKey, PROPVARIANT* pValue) { return attrs_->GetItem(guidKey, pValue); }
IFACEMETHODIMP PhoneCamMediaSource::GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType) { return attrs_->GetItemType(guidKey, pType); }
IFACEMETHODIMP PhoneCamMediaSource::CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult) { return attrs_->CompareItem(guidKey, Value, pbResult); }
IFACEMETHODIMP PhoneCamMediaSource::Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult) { return attrs_->Compare(pTheirs, MatchType, pbResult); }
IFACEMETHODIMP PhoneCamMediaSource::GetUINT32(REFGUID guidKey, UINT32* punValue) { return attrs_->GetUINT32(guidKey, punValue); }
IFACEMETHODIMP PhoneCamMediaSource::GetUINT64(REFGUID guidKey, UINT64* punValue) { return attrs_->GetUINT64(guidKey, punValue); }
IFACEMETHODIMP PhoneCamMediaSource::GetDouble(REFGUID guidKey, double* pfValue) { return attrs_->GetDouble(guidKey, pfValue); }
IFACEMETHODIMP PhoneCamMediaSource::GetGUID(REFGUID guidKey, GUID* pguidValue) { return attrs_->GetGUID(guidKey, pguidValue); }
IFACEMETHODIMP PhoneCamMediaSource::GetStringLength(REFGUID guidKey, UINT32* pcchLength) { return attrs_->GetStringLength(guidKey, pcchLength); }
IFACEMETHODIMP PhoneCamMediaSource::GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength) { return attrs_->GetString(guidKey, pwszValue, cchBufSize, pcchLength); }
IFACEMETHODIMP PhoneCamMediaSource::GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength) { return attrs_->GetAllocatedString(guidKey, ppwszValue, pcchLength); }
IFACEMETHODIMP PhoneCamMediaSource::GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) { return attrs_->GetBlobSize(guidKey, pcbBlobSize); }
IFACEMETHODIMP PhoneCamMediaSource::GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) { return attrs_->GetBlob(guidKey, pBuf, cbBufSize, pcbBlobSize); }
IFACEMETHODIMP PhoneCamMediaSource::GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize) { return attrs_->GetAllocatedBlob(guidKey, ppBuf, pcbSize); }
IFACEMETHODIMP PhoneCamMediaSource::GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv) { return attrs_->GetUnknown(guidKey, riid, ppv); }
IFACEMETHODIMP PhoneCamMediaSource::SetItem(REFGUID guidKey, REFPROPVARIANT Value) { return attrs_->SetItem(guidKey, Value); }
IFACEMETHODIMP PhoneCamMediaSource::DeleteItem(REFGUID guidKey) { return attrs_->DeleteItem(guidKey); }
IFACEMETHODIMP PhoneCamMediaSource::DeleteAllItems() { return attrs_->DeleteAllItems(); }
IFACEMETHODIMP PhoneCamMediaSource::SetUINT32(REFGUID guidKey, UINT32 unValue) { return attrs_->SetUINT32(guidKey, unValue); }
IFACEMETHODIMP PhoneCamMediaSource::SetUINT64(REFGUID guidKey, UINT64 unValue) { return attrs_->SetUINT64(guidKey, unValue); }
IFACEMETHODIMP PhoneCamMediaSource::SetDouble(REFGUID guidKey, double fValue) { return attrs_->SetDouble(guidKey, fValue); }
IFACEMETHODIMP PhoneCamMediaSource::SetGUID(REFGUID guidKey, REFGUID guidValue) { return attrs_->SetGUID(guidKey, guidValue); }
IFACEMETHODIMP PhoneCamMediaSource::SetString(REFGUID guidKey, LPCWSTR wszValue) { return attrs_->SetString(guidKey, wszValue); }
IFACEMETHODIMP PhoneCamMediaSource::SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize) { return attrs_->SetBlob(guidKey, pBuf, cbBufSize); }
IFACEMETHODIMP PhoneCamMediaSource::SetUnknown(REFGUID guidKey, IUnknown* pUnknown) { return attrs_->SetUnknown(guidKey, pUnknown); }
IFACEMETHODIMP PhoneCamMediaSource::LockStore() { return attrs_->LockStore(); }
IFACEMETHODIMP PhoneCamMediaSource::UnlockStore() { return attrs_->UnlockStore(); }
IFACEMETHODIMP PhoneCamMediaSource::GetCount(UINT32* pcItems) { return attrs_->GetCount(pcItems); }
IFACEMETHODIMP PhoneCamMediaSource::GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue) { return attrs_->GetItemByIndex(unIndex, pguidKey, pValue); }
IFACEMETHODIMP PhoneCamMediaSource::CopyAllItems(IMFAttributes* pDest) { return attrs_->CopyAllItems(pDest); }

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

    eventQueue_->QueueEventParamUnk(
        streamAnnounced_ ? MEUpdatedStream : MENewStream,
        GUID_NULL,
        S_OK,
        static_cast<IMFMediaStream2*>(stream_.Get()));
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
    attrs_.Reset();
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaSource::GetSourceAttributes(IMFAttributes** ppAttributes) {
    if (!ppAttributes) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !attrs_) return MF_E_SHUTDOWN;
    *ppAttributes = static_cast<IMFAttributes*>(this);
    AddRef();
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaSource::GetStreamAttributes(DWORD dwStreamIdentifier, IMFAttributes** ppAttributes) {
    if (!ppAttributes) return E_POINTER;
    if (dwStreamIdentifier != 0) return MF_E_INVALIDSTREAMNUMBER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !stream_) return MF_E_SHUTDOWN;
    *ppAttributes = static_cast<IMFAttributes*>(stream_.Get());
    (*ppAttributes)->AddRef();
    return S_OK;
}

IFACEMETHODIMP PhoneCamMediaSource::SetD3DManager(IUnknown* pManager) {
    if (!pManager) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !stream_) return MF_E_SHUTDOWN;
    return stream_->SetD3DManager(pManager);
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

IFACEMETHODIMP PhoneCamMediaSource::SetDefaultAllocator(DWORD dwOutputStreamID, IUnknown* pAllocator) {
    if (!pAllocator) return E_POINTER;
    if (dwOutputStreamID != 0) return MF_E_INVALIDSTREAMNUMBER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !stream_) return MF_E_SHUTDOWN;
    return stream_->SetAllocator(pAllocator);
}

IFACEMETHODIMP PhoneCamMediaSource::GetAllocatorUsage(DWORD dwOutputStreamID, DWORD* pdwInputStreamID, MFSampleAllocatorUsage* peUsage) {
    if (!pdwInputStreamID || !peUsage) return E_POINTER;
    if (dwOutputStreamID != 0) return MF_E_INVALIDSTREAMNUMBER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (shutdown_ || !stream_) return MF_E_SHUTDOWN;
    *pdwInputStreamID = dwOutputStreamID;
    *peUsage = stream_->GetAllocatorUsage();
    return S_OK;
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
    MFCreateAttributes(&attrs_, 8);
    if (attrs_) {
        attrs_->SetUINT32(MF_VIRTUALCAMERA_PROVIDE_ASSOCIATED_CAMERA_SOURCES, 1);
        attrs_->SetGUID(MFT_TRANSFORM_CLSID_Attribute, CLSID_PhoneCamMFActivator);
    }
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
        HRESULT hr = PhoneCamMediaSource::CreateInstance(static_cast<IMFAttributes*>(this), &source_);
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
