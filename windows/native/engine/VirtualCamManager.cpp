#include "VirtualCamManager.h"

#include <mfapi.h>
#include <mfidl.h>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace phonecam {

namespace {

// KSCATEGORY_VIDEO_CAMERA {E5323777-F976-4F5B-9B55-B94699C46E44}
constexpr GUID kCategoryVideoCamera = {
    0xE5323777u, 0xF976u, 0x4F5Bu, { 0x9Bu, 0x55u, 0xB9u, 0x46u, 0x99u, 0xC4u, 0x6Eu, 0x44u }
};

using FnMFCreateVirtualCamera = HRESULT(STDAPICALLTYPE*)(
    MFVirtualCameraType,
    MFVirtualCameraLifetime,
    MFVirtualCameraAccess,
    LPCWSTR,
    LPCWSTR,
    const GUID*,
    ULONG,
    IMFVirtualCamera**);

bool IsPhoneCamEnumeratedInMf() {
    ComPtr<IMFAttributes> attr;
    if (FAILED(MFCreateAttributes(&attr, 1))) return false;
    if (FAILED(attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
        return false;
    }
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attr.Get(), &devices, &count))) return false;
    bool found = false;
    for (UINT32 i = 0; i < count; ++i) {
        if (!found && devices[i]) {
            wchar_t name[256]{};
            UINT32 len = 0;
            if (SUCCEEDED(devices[i]->GetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, name, 256, &len))) {
                if (wcsstr(name, L"PhoneCam Studio") != nullptr) {
                    found = true;
                }
            }
        }
        if (devices[i]) devices[i]->Release();
    }
    CoTaskMemFree(devices);
    return found;
}

} // namespace

VirtualCamManager::~VirtualCamManager() {
    Stop();
    if (mfSensorLib_) {
        FreeLibrary(mfSensorLib_);
        mfSensorLib_ = nullptr;
    }
}

bool VirtualCamManager::IsRegKeyPresent(const wchar_t* subKey) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegCloseKey(key);
        return true;
    }
    return false;
}

bool VirtualCamManager::IsWindows11Build() {
    using FnRtlGetVersion = LONG(WINAPI*)(OSVERSIONINFOEXW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    auto fn = reinterpret_cast<FnRtlGetVersion>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (!fn) return false;
    OSVERSIONINFOEXW ovi{};
    ovi.dwOSVersionInfoSize = sizeof(ovi);
    if (fn(&ovi) != 0) return false;
    return ovi.dwMajorVersion > 10 || (ovi.dwMajorVersion == 10 && ovi.dwBuildNumber >= 22000);
}

bool VirtualCamManager::Start() {
    Stop();
    lastError_.clear();

    if (!mfSensorLib_) {
        mfSensorLib_ = LoadLibraryW(L"mfsensorgroup.dll");
    }

    auto fnCreate = mfSensorLib_
        ? reinterpret_cast<FnMFCreateVirtualCamera>(GetProcAddress(mfSensorLib_, "MFCreateVirtualCamera"))
        : nullptr;

    if (fnCreate) {
        if (!mfStarted_ && SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
            mfStarted_ = true;
        }

        // If persistent System Virtual Camera was already installed by PhoneCamVcamSetup.exe, reuse it cleanly!
        if (IsPhoneCamEnumeratedInMf()) {
            active_ = true;
            activeBackend_ = "Windows 11 Persistent System Virtual Camera (PhoneCam Studio)";
            return true;
        }

        const GUID categories[1] = { kCategoryVideoCamera };
        HRESULT hr = fnCreate(
            MFVirtualCameraType_SoftwareCameraSource,
            MFVirtualCameraLifetime_Session,
            MFVirtualCameraAccess_CurrentUser,
            L"PhoneCam Studio",
            kMfActivatorClsidStr,
            categories,
            1,
            &virtualCam_);
        if (SUCCEEDED(hr) && virtualCam_) {
            hr = virtualCam_->Start(nullptr);
            if (SUCCEEDED(hr)) {
                active_ = true;
                activeBackend_ = "Windows 11 Media Foundation (Session Virtual Camera)";
                return true;
            }
            std::ostringstream oss;
            oss << "IMFVirtualCamera::Start failed (HRESULT 0x" << std::hex << hr
                << "). Ensure PhoneCamMFSource.dll is registered via Installer or Install-VirtualCam.bat as Administrator.";
            lastError_ = oss.str();
            virtualCam_.Reset();
        } else {
            std::ostringstream oss;
            oss << "MFCreateVirtualCamera failed (HRESULT 0x" << std::hex << hr << ").";
            lastError_ = oss.str();
        }
        if (mfStarted_) {
            MFShutdown();
            mfStarted_ = false;
        }
    }

    // Windows 10 or DirectShow fallback
    const bool dsOk = IsRegKeyPresent(L"SOFTWARE\\Classes\\CLSID\\{8B1F5C32-7D4E-4A29-8F3B-62C9D5E841A2}");
    if (dsOk) {
        active_ = true;
        activeBackend_ = "DirectShow Virtual Camera (PhoneCam Studio (DirectShow))";
        return true;
    }

    if (lastError_.empty()) {
        lastError_ = "Virtual camera COM DLLs are not registered yet. Run Installer or Install-VirtualCam.bat as Administrator.";
    }
    return false;
}

void VirtualCamManager::Stop() {
    if (virtualCam_) {
        virtualCam_->Stop();
        virtualCam_->Remove();
        virtualCam_.Reset();
    }
    if (mfStarted_) {
        MFShutdown();
        mfStarted_ = false;
    }
    active_ = false;
    activeBackend_.clear();
}

VirtualCamDiagnostics VirtualCamManager::GetDiagnostics() const {
    VirtualCamDiagnostics d;
    d.isWindows11OrNewer = IsWindows11Build();
    HMODULE h = mfSensorLib_ ? mfSensorLib_ : GetModuleHandleW(L"mfsensorgroup.dll");
    bool loadedTemp = false;
    if (!h) {
        h = LoadLibraryW(L"mfsensorgroup.dll");
        loadedTemp = (h != nullptr);
    }
    d.mfApiAvailable = (h != nullptr && GetProcAddress(h, "MFCreateVirtualCamera") != nullptr);
    if (loadedTemp && h) FreeLibrary(h);

    d.mfComRegistered = IsRegKeyPresent(L"SOFTWARE\\Classes\\CLSID\\{7A9E4B21-6C3D-4F18-9E2A-51B8C4D730F1}");
    d.dsComRegistered = IsRegKeyPresent(L"SOFTWARE\\Classes\\CLSID\\{8B1F5C32-7D4E-4A29-8F3B-62C9D5E841A2}");
    d.active = active_;
    d.activeBackend = activeBackend_;
    d.lastError = lastError_;
    return d;
}

} // namespace phonecam
