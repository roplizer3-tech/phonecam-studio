#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace phonecam {

struct BluetoothDeviceInfo {
    std::string address; // AA:BB:CC:DD:EE:FF
    std::string name;
    bool connected = false;
    bool authenticated = false;
};

class BluetoothHelper {
public:
    static std::vector<BluetoothDeviceInfo> EnumeratePairedDevices();
    static uint64_t ConnectRfcommSocket(const std::string& macAddr);
};

} // namespace phonecam
