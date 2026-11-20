#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2bth.h>
#include <windows.h>
#include <bluetoothapis.h>

#include "BluetoothClient.h"

#include <cstdio>
#include <cstring>

namespace phonecam {

namespace {

// {7a9e4b21-6c3d-4f18-9e2a-51b8c4d730f9}
constexpr GUID kPhoneCamBtUuid = {
    0x7a9e4b21u, 0x6c3du, 0x4f18u, { 0x9eu, 0x2au, 0x51u, 0xb8u, 0xc4u, 0xd7u, 0x30u, 0xf9u }
};

std::string WideToUtf8(const wchar_t* wstr) {
    if (!wstr || !*wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return "";
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out.data(), len, nullptr, nullptr);
    return out;
}

bool ParseBtAddr(const std::string& mac, BTH_ADDR& outAddr) {
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    BTH_ADDR addr = 0;
    int nibbles = 0;
    for (char c : mac) {
        if (c == ':' || c == '-') continue;
        int v = hexVal(c);
        if (v < 0) return false;
        addr = (addr << 4) | static_cast<BTH_ADDR>(v);
        ++nibbles;
    }
    if (nibbles != 12) return false;
    outAddr = addr;
    return true;
}

} // namespace

std::vector<BluetoothDeviceInfo> BluetoothHelper::EnumeratePairedDevices() {
    std::vector<BluetoothDeviceInfo> list;

    BLUETOOTH_DEVICE_SEARCH_PARAMS params{};
    params.dwSize = sizeof(params);
    params.fReturnAuthenticated = TRUE;
    params.fReturnRemembered = TRUE;
    params.fReturnUnknown = FALSE;
    params.fReturnConnected = TRUE;
    params.fIssueInquiry = FALSE;
    params.cTimeoutMultiplier = 1;

    BLUETOOTH_DEVICE_INFO devInfo{};
    devInfo.dwSize = sizeof(devInfo);

    HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&params, &devInfo);
    if (!hFind) return list;

    do {
        BluetoothDeviceInfo info;
        const auto& bytes = devInfo.Address.rgBytes;
        char macBuf[32]{};
        std::snprintf(
            macBuf,
            sizeof(macBuf),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            bytes[5], bytes[4], bytes[3], bytes[2], bytes[1], bytes[0]);
        info.address = macBuf;
        info.name = WideToUtf8(devInfo.szName);
        if (info.name.empty()) info.name = info.address;
        info.connected = devInfo.fConnected != FALSE;
        info.authenticated = devInfo.fAuthenticated != FALSE;
        list.push_back(std::move(info));

        std::memset(&devInfo, 0, sizeof(devInfo));
        devInfo.dwSize = sizeof(devInfo);
    } while (BluetoothFindNextDevice(hFind, &devInfo));

    BluetoothFindDeviceClose(hFind);
    return list;
}

uint64_t BluetoothHelper::ConnectRfcommSocket(const std::string& macAddr) {
    BTH_ADDR btAddr = 0;
    if (!ParseBtAddr(macAddr, btAddr)) return static_cast<uint64_t>(INVALID_SOCKET);

    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (s == INVALID_SOCKET) return static_cast<uint64_t>(INVALID_SOCKET);

    DWORD timeoutMs = 1500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    SOCKADDR_BTH sa{};
    sa.addressFamily = AF_BTH;
    sa.btAddr = btAddr;
    sa.serviceClassId = kPhoneCamBtUuid;
    sa.port = 0; // Resolve SDP port automatically from serviceClassId

    if (connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == SOCKET_ERROR) {
        closesocket(s);
        return static_cast<uint64_t>(INVALID_SOCKET);
    }

    return static_cast<uint64_t>(s);
}

} // namespace phonecam
