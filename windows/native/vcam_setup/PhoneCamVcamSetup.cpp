#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>

#include <iostream>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

// KSCATEGORY_VIDEO_CAMERA {E5323777-F976-4F5B-9B55-B94699C46E44}
constexpr GUID kCategoryVideoCamera = {
    0xE5323777u, 0xF976u, 0x4F5Bu, { 0x9Bu, 0x55u, 0xB9u, 0x46u, 0x99u, 0xC4u, 0x6Eu, 0x44u }
};

constexpr wchar_t kMfClsidStr[] = L"{7A9E4B21-6C3D-4F18-9E2A-51B8C4D730F1}";

using FnMFCreateVirtualCamera = HRESULT(STDAPICALLTYPE*)(
    MFVirtualCameraType,
    MFVirtualCameraLifetime,
    MFVirtualCameraAccess,
    LPCWSTR,
    LPCWSTR,
    const GUID*,
    ULONG,
    IMFVirtualCamera**);

} // namespace

int wmain(int argc, wchar_t** argv) {
    bool uninstall = false;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--uninstall") {
            uninstall = true;
        }
    }

    HMODULE mfSensor = LoadLibraryW(L"mfsensorgroup.dll");
    if (!mfSensor) {
        std::cout << "mfsensorgroup.dll unavailable (Windows 10 or older) - skipping System MF Virtual Camera.\n";
        return 0;
    }

    auto fnCreate = reinterpret_cast<FnMFCreateVirtualCamera>(
        GetProcAddress(mfSensor, "MFCreateVirtualCamera"));
    if (!fnCreate) {
        std::cout << "MFCreateVirtualCamera unavailable - skipping System MF Virtual Camera.\n";
        FreeLibrary(mfSensor);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);

    const GUID categories[1] = { kCategoryVideoCamera };
    ComPtr<IMFVirtualCamera> vcam;
    HRESULT hr = fnCreate(
        MFVirtualCameraType_SoftwareCameraSource,
        MFVirtualCameraLifetime_System,
        MFVirtualCameraAccess_AllUsers,
        L"PhoneCam Studio",
        kMfClsidStr,
        categories,
        1,
        &vcam);

    int exitCode = 0;
    if (SUCCEEDED(hr) && vcam) {
        if (uninstall) {
            hr = vcam->Remove();
            std::cout << (SUCCEEDED(hr) ? "[OK] Removed System Virtual Camera.\n" : "[WARN] Remove returned failure.\n");
        } else {
            hr = vcam->Start(nullptr);
            if (SUCCEEDED(hr)) {
                std::cout << "[OK] Registered Windows 11 Persistent System Virtual Camera ('PhoneCam Studio').\n";
            } else {
                std::cerr << "[WARN] IMFVirtualCamera::Start failed: 0x" << std::hex << hr << "\n";
                exitCode = 1;
            }
        }
    } else if (!uninstall) {
        std::cerr << "[WARN] MFCreateVirtualCamera (System/AllUsers) failed: 0x" << std::hex << hr << "\n";
        exitCode = 1;
    }

    vcam.Reset();
    MFShutdown();
    CoUninitialize();
    FreeLibrary(mfSensor);
    return exitCode;
}
