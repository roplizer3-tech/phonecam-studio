#pragma once

#include <cstdint>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>

namespace phonecam {

inline constexpr wchar_t kMfActivatorClsidStr[] = L"{7A9E4B21-6C3D-4F18-9E2A-51B8C4D730F1}";
inline constexpr wchar_t kDsFilterClsidStr[] = L"{8B1F5C32-7D4E-4A29-8F3B-62C9D5E841A2}";

struct VirtualCamDiagnostics {
    bool isWindows11OrNewer = false;
    bool mfApiAvailable = false;
    bool mfComRegistered = false;
    bool dsComRegistered = false;
    bool active = false;
    std::string activeBackend;
    std::string lastError;
};

class VirtualCamManager {
public:
    VirtualCamManager() = default;
    ~VirtualCamManager();

    VirtualCamManager(const VirtualCamManager&) = delete;
    VirtualCamManager& operator=(const VirtualCamManager&) = delete;

    bool Start();
    void Stop();
    VirtualCamDiagnostics GetDiagnostics() const;

private:
    static bool IsRegKeyPresent(const wchar_t* subKey);
    static bool IsWindows11Build();

    HMODULE mfSensorLib_ = nullptr;
    Microsoft::WRL::ComPtr<IMFVirtualCamera> virtualCam_;
    bool mfStarted_ = false;
    bool active_ = false;
    std::string activeBackend_;
    std::string lastError_;
};

} // namespace phonecam
