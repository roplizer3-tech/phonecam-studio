#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <mferror.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <wrl/client.h>

#include "../common/SharedFrameBuffer.h"

#include <atomic>
#include <mutex>
#include <vector>

namespace phonecam::mf {

// {7A9E4B21-6C3D-4F18-9E2A-51B8C4D730F1}
inline constexpr CLSID CLSID_PhoneCamMFActivator = {
    0x7a9e4b21, 0x6c3d, 0x4f18, { 0x9e, 0x2a, 0x51, 0xb8, 0xc4, 0xd7, 0x30, 0xf1 }
};

void IncrementModuleRef();
void DecrementModuleRef();
LONG GetModuleRefCount();

class PhoneCamMediaSource;

class PhoneCamMediaStream final : public IMFAttributes, public IMFMediaStream2, public IKsControl {
public:
    PhoneCamMediaStream(PhoneCamMediaSource* parent, int index);
    ~PhoneCamMediaStream();

    HRESULT Initialize();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IMFAttributes
    IFACEMETHODIMP GetItem(REFGUID guidKey, PROPVARIANT* pValue) override;
    IFACEMETHODIMP GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType) override;
    IFACEMETHODIMP CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult) override;
    IFACEMETHODIMP Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult) override;
    IFACEMETHODIMP GetUINT32(REFGUID guidKey, UINT32* punValue) override;
    IFACEMETHODIMP GetUINT64(REFGUID guidKey, UINT64* punValue) override;
    IFACEMETHODIMP GetDouble(REFGUID guidKey, double* pfValue) override;
    IFACEMETHODIMP GetGUID(REFGUID guidKey, GUID* pguidValue) override;
    IFACEMETHODIMP GetStringLength(REFGUID guidKey, UINT32* pcchLength) override;
    IFACEMETHODIMP GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength) override;
    IFACEMETHODIMP GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength) override;
    IFACEMETHODIMP GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) override;
    IFACEMETHODIMP GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) override;
    IFACEMETHODIMP GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize) override;
    IFACEMETHODIMP GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv) override;
    IFACEMETHODIMP SetItem(REFGUID guidKey, REFPROPVARIANT Value) override;
    IFACEMETHODIMP DeleteItem(REFGUID guidKey) override;
    IFACEMETHODIMP DeleteAllItems() override;
    IFACEMETHODIMP SetUINT32(REFGUID guidKey, UINT32 unValue) override;
    IFACEMETHODIMP SetUINT64(REFGUID guidKey, UINT64 unValue) override;
    IFACEMETHODIMP SetDouble(REFGUID guidKey, double fValue) override;
    IFACEMETHODIMP SetGUID(REFGUID guidKey, REFGUID guidValue) override;
    IFACEMETHODIMP SetString(REFGUID guidKey, LPCWSTR wszValue) override;
    IFACEMETHODIMP SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize) override;
    IFACEMETHODIMP SetUnknown(REFGUID guidKey, IUnknown* pUnknown) override;
    IFACEMETHODIMP LockStore() override;
    IFACEMETHODIMP UnlockStore() override;
    IFACEMETHODIMP GetCount(UINT32* pcItems) override;
    IFACEMETHODIMP GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue) override;
    IFACEMETHODIMP CopyAllItems(IMFAttributes* pDest) override;

    // IMFMediaEventGenerator
    IFACEMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue) override;

    // IMFMediaStream
    IFACEMETHODIMP GetMediaSource(IMFMediaSource** ppMediaSource) override;
    IFACEMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** ppStreamDescriptor) override;
    IFACEMETHODIMP RequestSample(IUnknown* pToken) override;

    // IMFMediaStream2
    IFACEMETHODIMP SetStreamState(MF_STREAM_STATE value) override;
    IFACEMETHODIMP GetStreamState(MF_STREAM_STATE* value) override;

    // IKsControl
    IFACEMETHODIMP KsProperty(PKSPROPERTY Property, ULONG PropertyLength, LPVOID PropertyData, ULONG DataLength, ULONG* BytesReturned) override;
    IFACEMETHODIMP KsMethod(PKSMETHOD Method, ULONG MethodLength, LPVOID MethodData, ULONG DataLength, ULONG* BytesReturned) override;
    IFACEMETHODIMP KsEvent(PKSEVENT Event, ULONG EventLength, LPVOID EventData, ULONG DataLength, ULONG* BytesReturned) override;

    HRESULT Start(IMFMediaType* mediaType, const PROPVARIANT* varStartPosition);
    HRESULT Stop();
    HRESULT Pause();
    HRESULT Shutdown();
    HRESULT SetAllocator(IUnknown* pAllocator);
    MFSampleAllocatorUsage GetAllocatorUsage() const { return MFSampleAllocatorUsage_UsesProvidedAllocator; }
    HRESULT SetD3DManager(IUnknown* pManager);
    int StreamIndex() const { return index_; }

private:
    std::atomic<ULONG> refCount_{ 1 };
    std::mutex mutex_;
    PhoneCamMediaSource* parent_; // Non-owning back-pointer cleared on Shutdown
    int index_ = 0;

    Microsoft::WRL::ComPtr<IMFAttributes> attrs_;
    Microsoft::WRL::ComPtr<IMFStreamDescriptor> streamDesc_;
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> eventQueue_;
    Microsoft::WRL::ComPtr<IMFVideoSampleAllocatorEx> allocator_;
    Microsoft::WRL::ComPtr<IMFMediaType> defaultType_;
    Microsoft::WRL::ComPtr<IMFMediaType> activeType_;

    MF_STREAM_STATE state_ = MF_STREAM_STATE_STOPPED;
    bool shutdown_ = false;
    bool allocatorInit_ = false;
    GUID activeSubtype_ = MFVideoFormat_NV12;
    uint32_t outW_ = 1280;
    uint32_t outH_ = 720;
    uint32_t fpsNum_ = 30;
    uint32_t fpsDen_ = 1;

    SharedFrameReader shmReader_;
    std::vector<uint8_t> rawNv12_;
    std::vector<uint8_t> scaledNv12_;
    std::vector<uint8_t> rgbScratch_;
    LARGE_INTEGER qpcFreq_{};
    LARGE_INTEGER nextFrameQpc_{};
    uint64_t frameTick_ = 0;
};

class PhoneCamMediaSource final :
    public IMFAttributes,
    public IMFMediaSourceEx,
    public IMFGetService,
    public IKsControl,
    public IMFSampleAllocatorControl {
public:
    static HRESULT CreateInstance(IMFAttributes* activatorAttrs, PhoneCamMediaSource** ppSource);
    ~PhoneCamMediaSource();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IMFAttributes
    IFACEMETHODIMP GetItem(REFGUID guidKey, PROPVARIANT* pValue) override;
    IFACEMETHODIMP GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType) override;
    IFACEMETHODIMP CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult) override;
    IFACEMETHODIMP Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult) override;
    IFACEMETHODIMP GetUINT32(REFGUID guidKey, UINT32* punValue) override;
    IFACEMETHODIMP GetUINT64(REFGUID guidKey, UINT64* punValue) override;
    IFACEMETHODIMP GetDouble(REFGUID guidKey, double* pfValue) override;
    IFACEMETHODIMP GetGUID(REFGUID guidKey, GUID* pguidValue) override;
    IFACEMETHODIMP GetStringLength(REFGUID guidKey, UINT32* pcchLength) override;
    IFACEMETHODIMP GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength) override;
    IFACEMETHODIMP GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength) override;
    IFACEMETHODIMP GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) override;
    IFACEMETHODIMP GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) override;
    IFACEMETHODIMP GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize) override;
    IFACEMETHODIMP GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv) override;
    IFACEMETHODIMP SetItem(REFGUID guidKey, REFPROPVARIANT Value) override;
    IFACEMETHODIMP DeleteItem(REFGUID guidKey) override;
    IFACEMETHODIMP DeleteAllItems() override;
    IFACEMETHODIMP SetUINT32(REFGUID guidKey, UINT32 unValue) override;
    IFACEMETHODIMP SetUINT64(REFGUID guidKey, UINT64 unValue) override;
    IFACEMETHODIMP SetDouble(REFGUID guidKey, double fValue) override;
    IFACEMETHODIMP SetGUID(REFGUID guidKey, REFGUID guidValue) override;
    IFACEMETHODIMP SetString(REFGUID guidKey, LPCWSTR wszValue) override;
    IFACEMETHODIMP SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize) override;
    IFACEMETHODIMP SetUnknown(REFGUID guidKey, IUnknown* pUnknown) override;
    IFACEMETHODIMP LockStore() override;
    IFACEMETHODIMP UnlockStore() override;
    IFACEMETHODIMP GetCount(UINT32* pcItems) override;
    IFACEMETHODIMP GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue) override;
    IFACEMETHODIMP CopyAllItems(IMFAttributes* pDest) override;

    // IMFMediaEventGenerator
    IFACEMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent) override;
    IFACEMETHODIMP QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue) override;

    // IMFMediaSource
    IFACEMETHODIMP GetCharacteristics(DWORD* pdwCharacteristics) override;
    IFACEMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** ppPresentationDescriptor) override;
    IFACEMETHODIMP Start(IMFPresentationDescriptor* pPresentationDescriptor, const GUID* pguidTimeFormat, const PROPVARIANT* pvarStartPosition) override;
    IFACEMETHODIMP Stop() override;
    IFACEMETHODIMP Pause() override;
    IFACEMETHODIMP Shutdown() override;

    // IMFMediaSourceEx
    IFACEMETHODIMP GetSourceAttributes(IMFAttributes** ppAttributes) override;
    IFACEMETHODIMP GetStreamAttributes(DWORD dwStreamIdentifier, IMFAttributes** ppAttributes) override;
    IFACEMETHODIMP SetD3DManager(IUnknown* pManager) override;

    // IMFGetService
    IFACEMETHODIMP GetService(REFGUID guidService, REFIID riid, LPVOID* ppvObject) override;

    // IKsControl
    IFACEMETHODIMP KsProperty(PKSPROPERTY Property, ULONG PropertyLength, LPVOID PropertyData, ULONG DataLength, ULONG* BytesReturned) override;
    IFACEMETHODIMP KsMethod(PKSMETHOD Method, ULONG MethodLength, LPVOID MethodData, ULONG DataLength, ULONG* BytesReturned) override;
    IFACEMETHODIMP KsEvent(PKSEVENT Event, ULONG EventLength, LPVOID EventData, ULONG DataLength, ULONG* BytesReturned) override;

    // IMFSampleAllocatorControl
    IFACEMETHODIMP SetDefaultAllocator(DWORD dwOutputStreamID, IUnknown* pAllocator) override;
    IFACEMETHODIMP GetAllocatorUsage(DWORD dwOutputStreamID, DWORD* pdwInputStreamID, MFSampleAllocatorUsage* peUsage) override;

private:
    PhoneCamMediaSource() = default;
    HRESULT Initialize(IMFAttributes* activatorAttrs);

    std::atomic<ULONG> refCount_{ 1 };
    std::mutex mutex_;
    bool shutdown_ = false;
    bool streamAnnounced_ = false;
    Microsoft::WRL::ComPtr<IMFAttributes> attrs_;
    Microsoft::WRL::ComPtr<IMFMediaEventQueue> eventQueue_;
    Microsoft::WRL::ComPtr<IMFPresentationDescriptor> presDesc_;
    Microsoft::WRL::ComPtr<PhoneCamMediaStream> stream_;
};

class PhoneCamActivator final : public IMFActivate {
public:
    static HRESULT CreateInstance(REFIID riid, void** ppv);
    ~PhoneCamActivator();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IMFAttributes
    IFACEMETHODIMP GetItem(REFGUID guidKey, PROPVARIANT* pValue) override;
    IFACEMETHODIMP GetItemType(REFGUID guidKey, MF_ATTRIBUTE_TYPE* pType) override;
    IFACEMETHODIMP CompareItem(REFGUID guidKey, REFPROPVARIANT Value, BOOL* pbResult) override;
    IFACEMETHODIMP Compare(IMFAttributes* pTheirs, MF_ATTRIBUTES_MATCH_TYPE MatchType, BOOL* pbResult) override;
    IFACEMETHODIMP GetUINT32(REFGUID guidKey, UINT32* punValue) override;
    IFACEMETHODIMP GetUINT64(REFGUID guidKey, UINT64* punValue) override;
    IFACEMETHODIMP GetDouble(REFGUID guidKey, double* pfValue) override;
    IFACEMETHODIMP GetGUID(REFGUID guidKey, GUID* pguidValue) override;
    IFACEMETHODIMP GetStringLength(REFGUID guidKey, UINT32* pcchLength) override;
    IFACEMETHODIMP GetString(REFGUID guidKey, LPWSTR pwszValue, UINT32 cchBufSize, UINT32* pcchLength) override;
    IFACEMETHODIMP GetAllocatedString(REFGUID guidKey, LPWSTR* ppwszValue, UINT32* pcchLength) override;
    IFACEMETHODIMP GetBlobSize(REFGUID guidKey, UINT32* pcbBlobSize) override;
    IFACEMETHODIMP GetBlob(REFGUID guidKey, UINT8* pBuf, UINT32 cbBufSize, UINT32* pcbBlobSize) override;
    IFACEMETHODIMP GetAllocatedBlob(REFGUID guidKey, UINT8** ppBuf, UINT32* pcbSize) override;
    IFACEMETHODIMP GetUnknown(REFGUID guidKey, REFIID riid, LPVOID* ppv) override;
    IFACEMETHODIMP SetItem(REFGUID guidKey, REFPROPVARIANT Value) override;
    IFACEMETHODIMP DeleteItem(REFGUID guidKey) override;
    IFACEMETHODIMP DeleteAllItems() override;
    IFACEMETHODIMP SetUINT32(REFGUID guidKey, UINT32 unValue) override;
    IFACEMETHODIMP SetUINT64(REFGUID guidKey, UINT64 unValue) override;
    IFACEMETHODIMP SetDouble(REFGUID guidKey, double fValue) override;
    IFACEMETHODIMP SetGUID(REFGUID guidKey, REFGUID guidValue) override;
    IFACEMETHODIMP SetString(REFGUID guidKey, LPCWSTR wszValue) override;
    IFACEMETHODIMP SetBlob(REFGUID guidKey, const UINT8* pBuf, UINT32 cbBufSize) override;
    IFACEMETHODIMP SetUnknown(REFGUID guidKey, IUnknown* pUnknown) override;
    IFACEMETHODIMP LockStore() override;
    IFACEMETHODIMP UnlockStore() override;
    IFACEMETHODIMP GetCount(UINT32* pcItems) override;
    IFACEMETHODIMP GetItemByIndex(UINT32 unIndex, GUID* pguidKey, PROPVARIANT* pValue) override;
    IFACEMETHODIMP CopyAllItems(IMFAttributes* pDest) override;

    // IMFActivate
    IFACEMETHODIMP ActivateObject(REFIID riid, void** ppv) override;
    IFACEMETHODIMP ShutdownObject() override;
    IFACEMETHODIMP DetachObject() override;

private:
    PhoneCamActivator();
    std::atomic<ULONG> refCount_{ 1 };
    std::mutex mutex_;
    Microsoft::WRL::ComPtr<IMFAttributes> attrs_;
    Microsoft::WRL::ComPtr<PhoneCamMediaSource> source_;
};

} // namespace phonecam::mf
