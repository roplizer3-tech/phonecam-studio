#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dshow.h>
#include <amvideo.h>
#include <dvdmedia.h>
#include <wrl/client.h>

#include "../common/SharedFrameBuffer.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace phonecam::ds {

// {8B1F5C32-7D4E-4A29-8F3B-62C9D5E841A2}
inline constexpr CLSID CLSID_PhoneCamDSFilter = {
    0x8b1f5c32, 0x7d4e, 0x4a29, { 0x8f, 0x3b, 0x62, 0xc9, 0xd5, 0xe8, 0x41, 0xa2 }
};

void IncrementModuleRef();
void DecrementModuleRef();
LONG GetModuleRefCount();

void FreeMediaTypeFields(AM_MEDIA_TYPE& mt);
void DeleteMediaTypePtr(AM_MEDIA_TYPE* pmt);
HRESULT CopyMediaTypeData(AM_MEDIA_TYPE* dst, const AM_MEDIA_TYPE* src);

class PhoneCamDSFilter;

class PhoneCamDSPin final : public IPin, public IAMStreamConfig, public IKsPropertySet, public IQualityControl {
public:
    explicit PhoneCamDSPin(PhoneCamDSFilter* filter);
    ~PhoneCamDSPin();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IPin
    IFACEMETHODIMP Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt) override;
    IFACEMETHODIMP ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt) override;
    IFACEMETHODIMP Disconnect() override;
    IFACEMETHODIMP ConnectedTo(IPin** ppPin) override;
    IFACEMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE* pmt) override;
    IFACEMETHODIMP QueryPinInfo(PIN_INFO* pInfo) override;
    IFACEMETHODIMP QueryDirection(PIN_DIRECTION* pPinDir) override;
    IFACEMETHODIMP QueryId(LPWSTR* Id) override;
    IFACEMETHODIMP QueryAccept(const AM_MEDIA_TYPE* pmt) override;
    IFACEMETHODIMP EnumMediaTypes(IEnumMediaTypes** ppEnum) override;
    IFACEMETHODIMP QueryInternalConnections(IPin** apPin, ULONG* nPin) override;
    IFACEMETHODIMP EndOfStream() override;
    IFACEMETHODIMP BeginFlush() override;
    IFACEMETHODIMP EndFlush() override;
    IFACEMETHODIMP NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) override;

    // IAMStreamConfig
    IFACEMETHODIMP SetFormat(AM_MEDIA_TYPE* pmt) override;
    IFACEMETHODIMP GetFormat(AM_MEDIA_TYPE** ppmt) override;
    IFACEMETHODIMP GetNumberOfCapabilities(int* piCount, int* piSize) override;
    IFACEMETHODIMP GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) override;

    // IKsPropertySet
    IFACEMETHODIMP Set(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData, DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData) override;
    IFACEMETHODIMP Get(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData, DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData, DWORD* pcbReturned) override;
    IFACEMETHODIMP QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport) override;

    // IQualityControl
    IFACEMETHODIMP Notify(IBaseFilter* pSelf, Quality q) override;
    IFACEMETHODIMP SetSink(IQualityControl* piqc) override;

    HRESULT OnStateChanged(FILTER_STATE newState);
    static AM_MEDIA_TYPE* BuildFormatByIndex(int index);
    static constexpr int kFormatCount = 5;

private:
    bool IsFormatSupported(const AM_MEDIA_TYPE* pmt) const;
    HRESULT NegotiateAllocator(IMemInputPin* inputPin, const AM_MEDIA_TYPE& mt);
    void StreamLoop();

    std::atomic<ULONG> refCount_{ 1 };
    std::mutex mutex_;
    PhoneCamDSFilter* filter_; // Non-owning back-pointer
    Microsoft::WRL::ComPtr<IPin> connectedPin_;
    Microsoft::WRL::ComPtr<IMemInputPin> memInputPin_;
    Microsoft::WRL::ComPtr<IMemAllocator> allocator_;
    AM_MEDIA_TYPE connectedMt_{};
    bool hasConnectedMt_ = false;
    AM_MEDIA_TYPE preferredMt_{};
    bool hasPreferredMt_ = false;

    std::atomic<bool> streaming_{ false };
    std::thread streamThread_;
};

class PhoneCamDSFilter final : public IBaseFilter, public IAMFilterMiscFlags {
public:
    static HRESULT CreateInstance(REFIID riid, void** ppv);
    ~PhoneCamDSFilter();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IPersist
    IFACEMETHODIMP GetClassID(CLSID* pClassID) override;

    // IMediaFilter
    IFACEMETHODIMP Stop() override;
    IFACEMETHODIMP Pause() override;
    IFACEMETHODIMP Run(REFERENCE_TIME tStart) override;
    IFACEMETHODIMP GetState(DWORD dwMilliSecsTimeout, FILTER_STATE* State) override;
    IFACEMETHODIMP SetSyncSource(IReferenceClock* pClock) override;
    IFACEMETHODIMP GetSyncSource(IReferenceClock** pClock) override;

    // IBaseFilter
    IFACEMETHODIMP EnumPins(IEnumPins** ppEnum) override;
    IFACEMETHODIMP FindPin(LPCWSTR Id, IPin** ppPin) override;
    IFACEMETHODIMP QueryFilterInfo(FILTER_INFO* pInfo) override;
    IFACEMETHODIMP JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName) override;
    IFACEMETHODIMP QueryVendorInfo(LPWSTR* pVendorInfo) override;

    // IAMFilterMiscFlags
    IFACEMETHODIMP_(ULONG) GetMiscFlags() override;

    FILTER_STATE State() const { return state_; }

private:
    PhoneCamDSFilter();

    std::atomic<ULONG> refCount_{ 1 };
    std::mutex mutex_;
    FILTER_STATE state_ = State_Stopped;
    IFilterGraph* graph_ = nullptr; // Non-owning per DirectShow contract
    std::wstring name_ = L"PhoneCam Studio (DirectShow)";
    Microsoft::WRL::ComPtr<IReferenceClock> clock_;
    Microsoft::WRL::ComPtr<PhoneCamDSPin> capturePin_;
};

} // namespace phonecam::ds
