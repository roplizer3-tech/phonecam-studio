#include "MFSource.h"

#include <atomic>
#include <string>

namespace phonecam::mf {

namespace {

HMODULE g_module = nullptr;
std::atomic<LONG> g_refCount{ 0 };

class ActivatorClassFactory final : public IClassFactory {
public:
    ActivatorClassFactory() { IncrementModuleRef(); }
    ~ActivatorClassFactory() { DecrementModuleRef(); }

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

    IFACEMETHODIMP_(ULONG) AddRef() override {
        return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG c = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (c == 0) delete this;
        return c;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        return PhoneCamActivator::CreateInstance(riid, ppv);
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

} // namespace phonecam::mf

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        phonecam::mf::g_module = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow() {
    return phonecam::mf::GetModuleRefCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid != phonecam::mf::CLSID_PhoneCamMFActivator) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    auto* factory = new phonecam::mf::ActivatorClassFactory();
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllRegisterServer() {
    wchar_t dllPath[MAX_PATH]{};
    if (GetModuleFileNameW(phonecam::mf::g_module, dllPath, MAX_PATH) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const wchar_t* clsidKey = L"SOFTWARE\\Classes\\CLSID\\{7A9E4B21-6C3D-4F18-9E2A-51B8C4D730F1}";
    const wchar_t* inprocKey = L"SOFTWARE\\Classes\\CLSID\\{7A9E4B21-6C3D-4F18-9E2A-51B8C4D730F1}\\InprocServer32";

    HRESULT hr = phonecam::mf::WriteRegValue(HKEY_LOCAL_MACHINE, clsidKey, nullptr, L"PhoneCam Studio Media Foundation Source");
    if (FAILED(hr)) return hr;
    hr = phonecam::mf::WriteRegValue(HKEY_LOCAL_MACHINE, inprocKey, nullptr, dllPath);
    if (FAILED(hr)) return hr;
    return phonecam::mf::WriteRegValue(HKEY_LOCAL_MACHINE, inprocKey, L"ThreadingModel", L"Both");
}

STDAPI DllUnregisterServer() {
    const wchar_t* clsidKey = L"SOFTWARE\\Classes\\CLSID\\{7A9E4B21-6C3D-4F18-9E2A-51B8C4D730F1}";
    LONG r = RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidKey);
    if (r == ERROR_FILE_NOT_FOUND) return S_OK;
    return HRESULT_FROM_WIN32(r);
}
