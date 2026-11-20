#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace phonecam {

struct UsbDeviceEntry {
    std::string serial;
    std::string state; // "device", "unauthorized", "offline"
};

struct AdbResult {
    bool ok = false;
    std::string message;
    std::vector<std::string> devices;
};

struct UsbStatusSnapshot {
    bool adbFound = false;
    std::vector<UsbDeviceEntry> devices;
};

class AdbHelper {
public:
    static std::wstring FindAdbExecutable();
    static UsbStatusSnapshot GetUsbStatus();
    static AdbResult ForwardStreamPort(uint16_t localPort, uint16_t devicePort);
};

} // namespace phonecam
