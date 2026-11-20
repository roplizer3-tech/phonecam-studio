#include "DSFilter.h"
#include "../common/ColorConvert.h"

#include <algorithm>
#include <chrono>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace phonecam::ds {

namespace {

struct FormatSpec {
    GUID subtype;
    DWORD compression;
    WORD bitCount;
    LONG width;
    LONG height;
    LONG fps;
};

static const FormatSpec kFormats[PhoneCamDSPin::kFormatCount] = {
    { MEDIASUBTYPE_NV12,  MAKEFOURCC('N', 'V', '1', '2'), 12, 1280,  720, 30 },
    { MEDIASUBTYPE_NV12,  MAKEFOURCC('N', 'V', '1', '2'), 12, 1920, 1080, 30 },
    { MEDIASUBTYPE_YUY2,  MAKEFOURCC('Y', 'U', 'Y', '2'), 16, 1280,  720, 30 },
    { MEDIASUBTYPE_RGB24, BI_RGB,                         24, 1280,  720, 30 },
    { MEDIASUBTYPE_RGB24, BI_RGB,                         24, 1920, 1080, 30 }
};

bool ExtractVideoInfo(const AM_MEDIA_TYPE* pmt, uint32_t& w, uint32_t& h, uint32_t& fps, GUID& subtype) {
    if (!pmt || pmt->majortype != MEDIATYPE_Video || !pmt->pbFormat) return false;
    subtype = pmt->subtype;
    if (pmt->formattype == FORMAT_VideoInfo && pmt->cbFormat >= sizeof(VIDEOINFOHEADER)) {
        const auto* vih = reinterpret_cast<const VIDEOINFOHEADER*>(pmt->pbFormat);
        w = static_cast<uint32_t>(std::abs(vih->bmiHeader.biWidth));
        h = static_cast<uint32_t>(std::abs(vih->bmiHeader.biHeight));
        fps = vih->AvgTimePerFrame > 0 ? static_cast<uint32_t>(10000000LL / vih->AvgTimePerFrame) : 30u;
        return w > 0 && h > 0;
    }
    if (pmt->formattype == FORMAT_VideoInfo2 && pmt->cbFormat >= sizeof(VIDEOINFOHEADER2)) {
        const auto* vih = reinterpret_cast<const VIDEOINFOHEADER2*>(pmt->pbFormat);
        w = static_cast<uint32_t>(std::abs(vih->bmiHeader.biWidth));
        h = static_cast<uint32_t>(std::abs(vih->bmiHeader.biHeight));
        fps = vih->AvgTimePerFrame > 0 ? static_cast<uint32_t>(10000000LL / vih->AvgTimePerFrame) : 30u;
        return w > 0 && h > 0;
    }
    return false;
}

class PinEnumerator final : public IEnumPins {
public:
    explicit PinEnumerator(IPin* pin, ULONG pos = 0) : pin_(pin), pos_(pos) {
        IncrementModuleRef();
    }
    ~PinEnumerator() { DecrementModuleRef(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEnumPins) {
            *ppv = static_cast<IEnumPins*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return refCount_.fetch_add(1, std::memory_order_relaxed) + 1; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG c = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (c == 0) delete this;
        return c;
    }
    IFACEMETHODIMP Next(ULONG cPins, IPin** ppPins, ULONG* pcFetched) override {
        if (!ppPins) return E_POINTER;
        ULONG fetched = 0;
        if (pos_ == 0 && cPins > 0) {
            ppPins[0] = pin_.Get();
            ppPins[0]->AddRef();
            pos_ = 1;
            fetched = 1;
        }
        if (pcFetched) *pcFetched = fetched;
        return fetched == cPins ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Skip(ULONG cPins) override {
        if (pos_ + cPins <= 1) { pos_ += cPins; return S_OK; }
        pos_ = 1;
        return S_FALSE;
    }
    IFACEMETHODIMP Reset() override { pos_ = 0; return S_OK; }
    IFACEMETHODIMP Clone(IEnumPins** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = new PinEnumerator(pin_.Get(), pos_);
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_{ 1 };
    ComPtr<IPin> pin_;
    ULONG pos_;
};

class MediaTypeEnumerator final : public IEnumMediaTypes {
public:
    explicit MediaTypeEnumerator(ULONG pos = 0) : pos_(pos) { IncrementModuleRef(); }
    ~MediaTypeEnumerator() { DecrementModuleRef(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEnumMediaTypes) {
            *ppv = static_cast<IEnumMediaTypes*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return refCount_.fetch_add(1, std::memory_order_relaxed) + 1; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG c = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (c == 0) delete this;
        return c;
    }
    IFACEMETHODIMP Next(ULONG cMediaTypes, AM_MEDIA_TYPE** ppMediaTypes, ULONG* pcFetched) override {
        if (!ppMediaTypes) return E_POINTER;
        ULONG fetched = 0;
        while (fetched < cMediaTypes && pos_ < PhoneCamDSPin::kFormatCount) {
            ppMediaTypes[fetched] = PhoneCamDSPin::BuildFormatByIndex(static_cast<int>(pos_));
            if (!ppMediaTypes[fetched]) break;
            ++fetched;
            ++pos_;
        }
        if (pcFetched) *pcFetched = fetched;
        return fetched == cMediaTypes ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Skip(ULONG cMediaTypes) override {
        pos_ = std::min<ULONG>(pos_ + cMediaTypes, PhoneCamDSPin::kFormatCount);
        return pos_ < PhoneCamDSPin::kFormatCount ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Reset() override { pos_ = 0; return S_OK; }
    IFACEMETHODIMP Clone(IEnumMediaTypes** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = new MediaTypeEnumerator(pos_);
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_{ 1 };
    ULONG pos_;
};

} // namespace

void FreeMediaTypeFields(AM_MEDIA_TYPE& mt) {
    if (mt.cbFormat != 0 && mt.pbFormat) {
        CoTaskMemFree(mt.pbFormat);
        mt.pbFormat = nullptr;
        mt.cbFormat = 0;
    }
    if (mt.pUnk) {
        mt.pUnk->Release();
        mt.pUnk = nullptr;
    }
}

void DeleteMediaTypePtr(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return;
    FreeMediaTypeFields(*pmt);
    CoTaskMemFree(pmt);
}

HRESULT CopyMediaTypeData(AM_MEDIA_TYPE* dst, const AM_MEDIA_TYPE* src) {
    if (!dst || !src) return E_POINTER;
    *dst = *src;
    dst->pbFormat = nullptr;
    dst->pUnk = nullptr;
    if (src->cbFormat > 0 && src->pbFormat) {
        dst->pbFormat = static_cast<BYTE*>(CoTaskMemAlloc(src->cbFormat));
        if (!dst->pbFormat) return E_OUTOFMEMORY;
        std::memcpy(dst->pbFormat, src->pbFormat, src->cbFormat);
    }
    if (src->pUnk) {
        dst->pUnk = src->pUnk;
        dst->pUnk->AddRef();
    }
    return S_OK;
}

// ============================================================================
// PhoneCamDSPin
// ============================================================================

PhoneCamDSPin::PhoneCamDSPin(PhoneCamDSFilter* filter) : filter_(filter) {
    IncrementModuleRef();
}

PhoneCamDSPin::~PhoneCamDSPin() {
    streaming_.store(false);
    if (streamThread_.joinable()) streamThread_.join();
    if (hasConnectedMt_) FreeMediaTypeFields(connectedMt_);
    if (hasPreferredMt_) FreeMediaTypeFields(preferredMt_);
    DecrementModuleRef();
}

AM_MEDIA_TYPE* PhoneCamDSPin::BuildFormatByIndex(int index) {
    if (index < 0 || index >= kFormatCount) return nullptr;
    const FormatSpec& spec = kFormats[index];

    auto* pmt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
    if (!pmt) return nullptr;
    std::memset(pmt, 0, sizeof(AM_MEDIA_TYPE));

    auto* vih = static_cast<VIDEOINFOHEADER*>(CoTaskMemAlloc(sizeof(VIDEOINFOHEADER)));
    if (!vih) {
        CoTaskMemFree(pmt);
        return nullptr;
    }
    std::memset(vih, 0, sizeof(VIDEOINFOHEADER));

    const DWORD sampleSize = (spec.subtype == MEDIASUBTYPE_NV12)
        ? static_cast<DWORD>(spec.width * spec.height * 3 / 2)
        : static_cast<DWORD>(spec.width * spec.height * (spec.bitCount / 8));

    vih->AvgTimePerFrame = 10000000LL / spec.fps;
    vih->dwBitRate = sampleSize * static_cast<DWORD>(spec.fps) * 8u;
    vih->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih->bmiHeader.biWidth = spec.width;
    vih->bmiHeader.biHeight = spec.height;
    vih->bmiHeader.biPlanes = 1;
    vih->bmiHeader.biBitCount = spec.bitCount;
    vih->bmiHeader.biCompression = spec.compression;
    vih->bmiHeader.biSizeImage = sampleSize;

    pmt->majortype = MEDIATYPE_Video;
    pmt->subtype = spec.subtype;
    pmt->bFixedSizeSamples = TRUE;
    pmt->bTemporalCompression = FALSE;
    pmt->lSampleSize = sampleSize;
    pmt->formattype = FORMAT_VideoInfo;
    pmt->cbFormat = sizeof(VIDEOINFOHEADER);
    pmt->pbFormat = reinterpret_cast<BYTE*>(vih);
    return pmt;
}

bool PhoneCamDSPin::IsFormatSupported(const AM_MEDIA_TYPE* pmt) const {
    uint32_t w = 0, h = 0, fps = 0;
    GUID sub{};
    if (!ExtractVideoInfo(pmt, w, h, fps, sub)) return false;
    if (sub != MEDIASUBTYPE_NV12 && sub != MEDIASUBTYPE_YUY2 && sub != MEDIASUBTYPE_RGB24) {
        return false;
    }
    return (w == 1280 && h == 720) || (w == 1920 && h == 1080);
}

IFACEMETHODIMP PhoneCamDSPin::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IPin) {
        *ppv = static_cast<IPin*>(this);
    } else if (riid == IID_IAMStreamConfig) {
        *ppv = static_cast<IAMStreamConfig*>(this);
    } else if (riid == IID_IKsPropertySet) {
        *ppv = static_cast<IKsPropertySet*>(this);
    } else if (riid == IID_IQualityControl) {
        *ppv = static_cast<IQualityControl*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) PhoneCamDSPin::AddRef() { return refCount_.fetch_add(1, std::memory_order_relaxed) + 1; }
IFACEMETHODIMP_(ULONG) PhoneCamDSPin::Release() {
    const ULONG c = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (c == 0) delete this;
    return c;
}

IFACEMETHODIMP PhoneCamDSPin::Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) {
    if (!pReceivePin) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (connectedPin_) return VFW_E_ALREADY_CONNECTED;
    if (filter_ && filter_->State() != State_Stopped) return VFW_E_NOT_STOPPED;

    auto tryConnectWith = [&](const AM_MEDIA_TYPE* candidate) -> HRESULT {
        if (!candidate || !IsFormatSupported(candidate)) return VFW_E_TYPE_NOT_ACCEPTED;

        ComPtr<IMemInputPin> inPin;
        if (FAILED(pReceivePin->QueryInterface(IID_PPV_ARGS(&inPin)))) return VFW_E_NO_TRANSPORT;

        HRESULT hr = pReceivePin->ReceiveConnection(this, candidate);
        if (FAILED(hr)) return hr;

        hr = NegotiateAllocator(inPin.Get(), *candidate);
        if (FAILED(hr)) {
            pReceivePin->Disconnect();
            return hr;
        }

        if (hasConnectedMt_) FreeMediaTypeFields(connectedMt_);
        CopyMediaTypeData(&connectedMt_, candidate);
        hasConnectedMt_ = true;
        connectedPin_ = pReceivePin;
        memInputPin_ = inPin;
        return S_OK;
    };

    if (pmt) {
        return tryConnectWith(pmt);
    }
    if (hasPreferredMt_) {
        if (SUCCEEDED(tryConnectWith(&preferredMt_))) return S_OK;
    }
    for (int i = 0; i < kFormatCount; ++i) {
        AM_MEDIA_TYPE* cand = BuildFormatByIndex(i);
        if (!cand) continue;
        HRESULT hr = tryConnectWith(cand);
        DeleteMediaTypePtr(cand);
        if (SUCCEEDED(hr)) return S_OK;
    }
    return VFW_E_NO_ACCEPTABLE_TYPES;
}

HRESULT PhoneCamDSPin::NegotiateAllocator(IMemInputPin* inputPin, const AM_MEDIA_TYPE& mt) {
    ComPtr<IMemAllocator> alloc;
    if (FAILED(inputPin->GetAllocator(&alloc)) || !alloc) {
        HRESULT hr = CoCreateInstance(CLSID_MemoryAllocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&alloc));
        if (FAILED(hr)) return hr;
    }

    ALLOCATOR_PROPERTIES req{}, actual{};
    req.cBuffers = 3;
    req.cbBuffer = std::max<long>(static_cast<long>(mt.lSampleSize), 1280 * 720 * 3);
    req.cbAlign = 16;
    req.cbPrefix = 0;

    HRESULT hr = alloc->SetProperties(&req, &actual);
    if (FAILED(hr)) return hr;
    hr = inputPin->NotifyAllocator(alloc.Get(), FALSE);
    if (FAILED(hr)) return hr;

    allocator_ = alloc;
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSPin::ReceiveConnection(IPin*, const AM_MEDIA_TYPE*) { return E_UNEXPECTED; }

IFACEMETHODIMP PhoneCamDSPin::Disconnect() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!connectedPin_) return S_FALSE;
    if (filter_ && filter_->State() != State_Stopped) return VFW_E_NOT_STOPPED;
    if (allocator_) {
        allocator_->Decommit();
        allocator_.Reset();
    }
    memInputPin_.Reset();
    connectedPin_.Reset();
    if (hasConnectedMt_) {
        FreeMediaTypeFields(connectedMt_);
        hasConnectedMt_ = false;
    }
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSPin::ConnectedTo(IPin** ppPin) {
    if (!ppPin) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (!connectedPin_) { *ppPin = nullptr; return VFW_E_NOT_CONNECTED; }
    *ppPin = connectedPin_.Get();
    (*ppPin)->AddRef();
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSPin::ConnectionMediaType(AM_MEDIA_TYPE* pmt) {
    if (!pmt) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (!connectedPin_ || !hasConnectedMt_) return VFW_E_NOT_CONNECTED;
    return CopyMediaTypeData(pmt, &connectedMt_);
}

IFACEMETHODIMP PhoneCamDSPin::QueryPinInfo(PIN_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    pInfo->pFilter = filter_;
    if (pInfo->pFilter) pInfo->pFilter->AddRef();
    pInfo->dir = PINDIR_OUTPUT;
    wcscpy_s(pInfo->achName, L"Capture");
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSPin::QueryDirection(PIN_DIRECTION* pPinDir) {
    if (!pPinDir) return E_POINTER;
    *pPinDir = PINDIR_OUTPUT;
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSPin::QueryId(LPWSTR* Id) {
    if (!Id) return E_POINTER;
    const wchar_t* src = L"Capture";
    size_t bytes = (wcslen(src) + 1) * sizeof(wchar_t);
    *Id = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
    if (!*Id) return E_OUTOFMEMORY;
    std::memcpy(*Id, src, bytes);
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSPin::QueryAccept(const AM_MEDIA_TYPE* pmt) {
    return IsFormatSupported(pmt) ? S_OK : S_FALSE;
}

IFACEMETHODIMP PhoneCamDSPin::EnumMediaTypes(IEnumMediaTypes** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new MediaTypeEnumerator();
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSPin::QueryInternalConnections(IPin**, ULONG*) { return E_NOTIMPL; }
IFACEMETHODIMP PhoneCamDSPin::EndOfStream() { return S_OK; }
IFACEMETHODIMP PhoneCamDSPin::BeginFlush() { return S_OK; }
IFACEMETHODIMP PhoneCamDSPin::EndFlush() { return S_OK; }
IFACEMETHODIMP PhoneCamDSPin::NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) { return S_OK; }

IFACEMETHODIMP PhoneCamDSPin::SetFormat(AM_MEDIA_TYPE* pmt) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (connectedPin_) return VFW_E_ALREADY_CONNECTED;
    if (!pmt) {
        if (hasPreferredMt_) { FreeMediaTypeFields(preferredMt_); hasPreferredMt_ = false; }
        return S_OK;
    }
    if (!IsFormatSupported(pmt)) return VFW_E_INVALIDMEDIATYPE;
    if (hasPreferredMt_) FreeMediaTypeFields(preferredMt_);
    CopyMediaTypeData(&preferredMt_, pmt);
    hasPreferredMt_ = true;
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSPin::GetFormat(AM_MEDIA_TYPE** ppmt) {
    if (!ppmt) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    if (hasConnectedMt_) {
        *ppmt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
        if (!*ppmt) return E_OUTOFMEMORY;
        return CopyMediaTypeData(*ppmt, &connectedMt_);
    }
    if (hasPreferredMt_) {
        *ppmt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
        if (!*ppmt) return E_OUTOFMEMORY;
        return CopyMediaTypeData(*ppmt, &preferredMt_);
    }
    *ppmt = BuildFormatByIndex(0);
    return *ppmt ? S_OK : E_OUTOFMEMORY;
}

IFACEMETHODIMP PhoneCamDSPin::GetNumberOfCapabilities(int* piCount, int* piSize) {
    if (!piCount || !piSize) return E_POINTER;
    *piCount = kFormatCount;
    *piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSPin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) {
    if (!ppmt || !pSCC) return E_POINTER;
    if (iIndex < 0 || iIndex >= kFormatCount) return S_FALSE;
    *ppmt = BuildFormatByIndex(iIndex);
    if (!*ppmt) return E_OUTOFMEMORY;

    const FormatSpec& s = kFormats[iIndex];
    auto* caps = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(pSCC);
    std::memset(caps, 0, sizeof(VIDEO_STREAM_CONFIG_CAPS));
    caps->guid = FORMAT_VideoInfo;
    caps->VideoStandard = AnalogVideo_None;
    caps->InputSize = { s.width, s.height };
    caps->MinCroppingSize = { s.width, s.height };
    caps->MaxCroppingSize = { s.width, s.height };
    caps->MinOutputSize = { s.width, s.height };
    caps->MaxOutputSize = { s.width, s.height };
    caps->MinFrameInterval = 10000000LL / 60;
    caps->MaxFrameInterval = 10000000LL / 15;
    caps->MinBitsPerSecond = 1000000;
    caps->MaxBitsPerSecond = 500000000;
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSPin::Set(REFGUID, DWORD, LPVOID, DWORD, LPVOID, DWORD) { return E_PROP_SET_UNSUPPORTED; }

IFACEMETHODIMP PhoneCamDSPin::Get(REFGUID guidPropSet, DWORD dwPropID, LPVOID, DWORD, LPVOID pPropData, DWORD cbPropData, DWORD* pcbReturned) {
    if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
    if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
    if (pcbReturned) *pcbReturned = sizeof(GUID);
    if (!pPropData) return S_OK;
    if (cbPropData < sizeof(GUID)) return E_UNEXPECTED;
    *reinterpret_cast<GUID*>(pPropData) = PIN_CATEGORY_CAPTURE;
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSPin::QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport) {
    if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
    if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
    if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSPin::Notify(IBaseFilter*, Quality) { return S_OK; }
IFACEMETHODIMP PhoneCamDSPin::SetSink(IQualityControl*) { return S_OK; }

HRESULT PhoneCamDSPin::OnStateChanged(FILTER_STATE newState) {
    std::unique_lock<std::mutex> lk(mutex_);
    if (newState == State_Stopped) {
        streaming_.store(false);
        if (allocator_) allocator_->Decommit();
        std::thread t = std::move(streamThread_);
        lk.unlock();
        if (t.joinable()) {
            t.join();
        }
        return S_OK;
    }

    if (!connectedPin_ || !allocator_ || !memInputPin_) return S_OK;
    if (newState == State_Paused) {
        allocator_->Commit();
    }
    if (newState == State_Running && !streaming_.load()) {
        allocator_->Commit();
        streaming_.store(true);
        streamThread_ = std::thread(&PhoneCamDSPin::StreamLoop, this);
    }
    return S_OK;
}

void PhoneCamDSPin::StreamLoop() {
    uint32_t outW = 1280, outH = 720, fps = 30;
    GUID subtype = MEDIASUBTYPE_NV12;
    ComPtr<IMemAllocator> alloc;
    ComPtr<IMemInputPin> inPin;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!hasConnectedMt_ || !allocator_ || !memInputPin_) return;
        ExtractVideoInfo(&connectedMt_, outW, outH, fps, subtype);
        alloc = allocator_;
        inPin = memInputPin_;
    }

    SharedFrameReader reader;
    reader.Open();
    std::vector<uint8_t> rawNv12;
    std::vector<uint8_t> scaledNv12;
    uint64_t tick = 0;
    REFERENCE_TIME sampleTime = 0;
    const REFERENCE_TIME frameDur = 10000000LL / std::max(1u, fps);
    const auto period = std::chrono::microseconds(1000000LL / std::max(1u, fps));
    auto nextWake = std::chrono::steady_clock::now();

    while (streaming_.load()) {
        ComPtr<IMediaSample> sample;
        if (FAILED(alloc->GetBuffer(&sample, nullptr, nullptr, 0)) || !sample) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        uint32_t srcW = 0, srcH = 0, srcFn = 30, srcFd = 1;
        uint64_t seq = 0;
        if (!reader.ReadLatestFrameNV12(rawNv12, srcW, srcH, srcFn, srcFd, seq)) {
            srcW = outW;
            srcH = outH;
            RenderStandbyNV12(rawNv12, srcW, srcH, ++tick);
        }

        const uint8_t* nv12Ready = rawNv12.data();
        if (srcW != outW || srcH != outH) {
            scaledNv12.resize(static_cast<size_t>(outW) * outH * 3u / 2u);
            ResizeNV12(rawNv12.data(), srcW, srcH, scaledNv12.data(), outW, outH);
            nv12Ready = scaledNv12.data();
        }

        BYTE* dst = nullptr;
        if (SUCCEEDED(sample->GetPointer(&dst)) && dst) {
            long bufMax = sample->GetSize();
            if (subtype == MEDIASUBTYPE_NV12) {
                long need = static_cast<long>(outW * outH * 3u / 2u);
                if (bufMax >= need) {
                    std::memcpy(dst, nv12Ready, static_cast<size_t>(need));
                    sample->SetActualDataLength(need);
                }
            } else if (subtype == MEDIASUBTYPE_YUY2) {
                long need = static_cast<long>(outW * outH * 2u);
                if (bufMax >= need) {
                    NV12ToYUY2(nv12Ready, outW, outH, dst, outW * 2u);
                    sample->SetActualDataLength(need);
                }
            } else if (subtype == MEDIASUBTYPE_RGB24) {
                long need = static_cast<long>(outW * outH * 3u);
                if (bufMax >= need) {
                    NV12ToBGR24BottomUp(nv12Ready, outW, outH, dst, outW * 3u);
                    sample->SetActualDataLength(need);
                }
            }
        }

        REFERENCE_TIME stopTime = sampleTime + frameDur;
        sample->SetTime(&sampleTime, &stopTime);
        sample->SetSyncPoint(TRUE);
        sampleTime = stopTime;

        if (FAILED(inPin->Receive(sample.Get()))) {
            break;
        }

        nextWake += period;
        auto now = std::chrono::steady_clock::now();
        if (nextWake > now) {
            std::this_thread::sleep_until(nextWake);
        } else if (now - nextWake > period * 3) {
            nextWake = now;
        }
    }
}

// ============================================================================
// PhoneCamDSFilter
// ============================================================================

HRESULT PhoneCamDSFilter::CreateInstance(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    auto* f = new PhoneCamDSFilter();
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

PhoneCamDSFilter::PhoneCamDSFilter() {
    IncrementModuleRef();
    capturePin_.Attach(new PhoneCamDSPin(this));
}

PhoneCamDSFilter::~PhoneCamDSFilter() {
    Stop();
    DecrementModuleRef();
}

IFACEMETHODIMP PhoneCamDSFilter::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IPersist || riid == IID_IMediaFilter || riid == IID_IBaseFilter) {
        *ppv = static_cast<IBaseFilter*>(this);
    } else if (riid == IID_IAMFilterMiscFlags) {
        *ppv = static_cast<IAMFilterMiscFlags*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) PhoneCamDSFilter::AddRef() { return refCount_.fetch_add(1, std::memory_order_relaxed) + 1; }
IFACEMETHODIMP_(ULONG) PhoneCamDSFilter::Release() {
    const ULONG c = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (c == 0) delete this;
    return c;
}

IFACEMETHODIMP PhoneCamDSFilter::GetClassID(CLSID* pClassID) {
    if (!pClassID) return E_POINTER;
    *pClassID = CLSID_PhoneCamDSFilter;
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSFilter::Stop() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_ = State_Stopped;
    }
    return capturePin_->OnStateChanged(State_Stopped);
}

IFACEMETHODIMP PhoneCamDSFilter::Pause() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_ = State_Paused;
    }
    return capturePin_->OnStateChanged(State_Paused);
}

IFACEMETHODIMP PhoneCamDSFilter::Run(REFERENCE_TIME) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_ = State_Running;
    }
    return capturePin_->OnStateChanged(State_Running);
}

IFACEMETHODIMP PhoneCamDSFilter::GetState(DWORD, FILTER_STATE* State) {
    if (!State) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    *State = state_;
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSFilter::SetSyncSource(IReferenceClock* pClock) {
    std::lock_guard<std::mutex> lk(mutex_);
    clock_ = pClock;
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSFilter::GetSyncSource(IReferenceClock** pClock) {
    if (!pClock) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    *pClock = clock_.Get();
    if (*pClock) (*pClock)->AddRef();
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSFilter::EnumPins(IEnumPins** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = new PinEnumerator(capturePin_.Get());
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSFilter::FindPin(LPCWSTR Id, IPin** ppPin) {
    if (!ppPin) return E_POINTER;
    *ppPin = nullptr;
    if (Id && wcscmp(Id, L"Capture") == 0) {
        *ppPin = capturePin_.Get();
        (*ppPin)->AddRef();
        return S_OK;
    }
    return VFW_E_NOT_FOUND;
}

IFACEMETHODIMP PhoneCamDSFilter::QueryFilterInfo(FILTER_INFO* pInfo) {
    if (!pInfo) return E_POINTER;
    std::lock_guard<std::mutex> lk(mutex_);
    wcsncpy_s(pInfo->achName, name_.c_str(), _TRUNCATE);
    pInfo->pGraph = graph_;
    if (pInfo->pGraph) pInfo->pGraph->AddRef();
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSFilter::JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) {
    std::lock_guard<std::mutex> lk(mutex_);
    graph_ = pGraph;
    name_ = (pName && *pName) ? pName : L"PhoneCam Studio (DirectShow)";
    return S_OK;
}

IFACEMETHODIMP PhoneCamDSFilter::QueryVendorInfo(LPWSTR* pVendorInfo) {
    if (!pVendorInfo) return E_POINTER;
    const wchar_t* v = L"PhoneCam Studio";
    size_t bytes = (wcslen(v) + 1) * sizeof(wchar_t);
    *pVendorInfo = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
    if (!*pVendorInfo) return E_OUTOFMEMORY;
    std::memcpy(*pVendorInfo, v, bytes);
    return S_OK;
}

IFACEMETHODIMP_(ULONG) PhoneCamDSFilter::GetMiscFlags() {
    return AM_FILTER_MISC_FLAGS_IS_SOURCE;
}

} // namespace phonecam::ds
