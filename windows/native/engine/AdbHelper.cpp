#include "AdbHelper.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <sstream>

namespace phonecam {

namespace {

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool RunProcessCapture(const std::wstring& exe, const std::wstring& args, std::string& outText, DWORD& exitCode) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return false;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    BOOL ok = CreateProcessW(
        exe.c_str(),
        cmd.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        return false;
    }

    char buf[1024];
    DWORD bytesRead = 0;
    outText.clear();
    while (ReadFile(readPipe, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
        outText.append(buf, bytesRead);
    }
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, 5000);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

} // namespace

std::wstring AdbHelper::FindAdbExecutable() {
    wchar_t exeDir[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    std::wstring base(exeDir);
    size_t pos = base.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        std::wstring localAdb = base.substr(0, pos) + L"\\tools\\adb\\adb.exe";
        if (FileExists(localAdb)) return localAdb;
    }

    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, L"adb.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
        return found;
    }

    wchar_t localAppData[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        std::wstring sdkAdb = std::wstring(localAppData) + L"\\Android\\Sdk\\platform-tools\\adb.exe";
        if (FileExists(sdkAdb)) return sdkAdb;
    }
    return L"";
}

AdbResult AdbHelper::ForwardStreamPort(uint16_t localPort, uint16_t devicePort) {
    AdbResult res;
    std::wstring adb = FindAdbExecutable();
    if (adb.empty()) {
        res.ok = false;
        res.message = "adb.exe not found in PATH, tools\\adb, or Android SDK platform-tools. Enable USB Tethering or place adb.exe in tools\\adb.";
        return res;
    }

    std::string out;
    DWORD exitCode = 1;
    if (!RunProcessCapture(adb, L"devices", out, exitCode) || exitCode != 0) {
        res.ok = false;
        res.message = "Failed executing adb devices: " + out;
        return res;
    }

    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("\tdevice") != std::string::npos) {
            size_t tab = line.find('\t');
            if (tab != std::string::npos) {
                res.devices.push_back(line.substr(0, tab));
            }
        }
    }

    if (res.devices.empty()) {
        res.ok = false;
        res.message = "No authorized Android USB device detected. Enable USB Debugging on your phone and accept the RSA prompt.";
        return res;
    }

    std::wstring fwdArgs = L"forward tcp:" + std::to_wstring(localPort) + L" tcp:" + std::to_wstring(devicePort);
    if (!RunProcessCapture(adb, fwdArgs, out, exitCode) || exitCode != 0) {
        res.ok = false;
        res.message = "adb forward failed: " + out;
        return res;
    }

    res.ok = true;
    res.message = "USB ADB forward ready on 127.0.0.1:" + std::to_string(localPort);
    return res;
}

} // namespace phonecam
