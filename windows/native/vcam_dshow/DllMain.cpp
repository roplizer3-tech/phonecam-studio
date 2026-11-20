#include "DSFilter.h"

#include <atomic>
#include <string>

namespace phonecam::ds {

namespace {

HMODULE g_module = nullptr;
std::atomic<LONG> g_refCount{ 0 };

class FilterClassFactory final : public IClassFactory {
public:
    FilterClassFactory() { IncrementModuleRef(); }
    ~FilterClassFactory() { DecrementModuleRef(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
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

    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        return PhoneCamDSFilter::CreateInstance(riid, ppv);
    }

    IFACEMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) IncrementModuleRef();
        else DecrementModuleRef();
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_{ 1 };
};

HRESULT WriteRegValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName, const wchar_t* data) {
    HKEY hKey = nullptr;
    LONG r = RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    const DWORD bytes = static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t));
    r = RegSetValueExW(hKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(data), bytes);
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(r);
}

} // namespace

void IncrementModuleRef() { g_refCount.fetch_add(1, std::memory_order_relaxed); }
void DecrementModuleRef() { g_refCount.fetch_sub(1, std::memory_order_relaxed); }
LONG GetModuleRefCount() { return g_refCount.load(std::memory_order_relaxed); }

} // namespace phonecam::ds

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        phonecam::ds::g_module = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow() {
    return phonecam::ds::GetModuleRefCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid != phonecam::ds::CLSID_PhoneCamDSFilter) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    auto* factory = new phonecam::ds::FilterClassFactory();
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllRegisterServer() {
    wchar_t dllPath[MAX_PATH]{};
    if (GetModuleFileNameW(phonecam::ds::g_module, dllPath, MAX_PATH) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const wchar_t* clsidKey = L"SOFTWARE\\Classes\\CLSID\\{8B1F5C32-7D4E-4A29-8F3B-62C9D5E841A2}";
    const wchar_t* inprocKey = L"SOFTWARE\\Classes\\CLSID\\{8B1F5C32-7D4E-4A29-8F3B-62C9D5E841A2}\\InprocServer32";

    HRESULT hr = phonecam::ds::WriteRegValue(HKEY_LOCAL_MACHINE, clsidKey, nullptr, L"PhoneCam Studio (DirectShow)");
    if (FAILED(hr)) return hr;
    hr = phonecam::ds::WriteRegValue(HKEY_LOCAL_MACHINE, inprocKey, nullptr, dllPath);
    if (FAILED(hr)) return hr;
    hr = phonecam::ds::WriteRegValue(HKEY_LOCAL_MACHINE, inprocKey, L"ThreadingModel", L"Both");
    if (FAILED(hr)) return hr;

    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Microsoft::WRL::ComPtr<IFilterMapper2> mapper;
    hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mapper));
    if (SUCCEEDED(hr) && mapper) {
        REGPINTYPES pinTypes{};
        pinTypes.clsMajorType = &MEDIATYPE_Video;
        pinTypes.clsMinorType = &MEDIASUBTYPE_NULL;

        REGFILTERPINS2 pinInfo{};
        pinInfo.dwFlags = REG_PINFLAG_B_OUTPUT;
        pinInfo.cInstances = 1;
        pinInfo.nMediaTypes = 1;
        pinInfo.lpMediaType = &pinTypes;
        pinInfo.nMediums = 0;
        pinInfo.lpMedium = nullptr;
        pinInfo.clsPinCategory = &PIN_CATEGORY_CAPTURE;

        REGFILTER2 rf2{};
        rf2.dwVersion = 2;
        rf2.dwMerit = MERIT_DO_NOT_USE;
        rf2.cPins2 = 1;
        rf2.rgPins2 = &pinInfo;

        Microsoft::WRL::ComPtr<IMoniker> moniker;
        hr = mapper->RegisterFilter(
            phonecam::ds::CLSID_PhoneCamDSFilter,
            L"PhoneCam Studio (DirectShow)",
            &moniker,
            &CLSID_VideoInputDeviceCategory,
            L"PhoneCam Studio (DirectShow)",
            &rf2);
    }
    if (SUCCEEDED(coHr)) {
        CoUninitialize();
    }
    return hr;
}

STDAPI DllUnregisterServer() {
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Microsoft::WRL::ComPtr<IFilterMapper2> mapper;
    if (SUCCEEDED(CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mapper))) && mapper) {
        mapper->UnregisterFilter(&CLSID_VideoInputDeviceCategory, L"PhoneCam Studio (DirectShow)", phonecam::ds::CLSID_PhoneCamDSFilter);
    }
    if (SUCCEEDED(coHr)) {
        CoUninitialize();
    }

    const wchar_t* clsidKey = L"SOFTWARE\\Classes\\CLSID\\{8B1F5C32-7D4E-4A29-8F3B-62C9D5E841A2}";
    LONG r = RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidKey);
    if (r == ERROR_FILE_NOT_FOUND) return S_OK;
    return HRESULT_FROM_WIN32(r);
}
