#pragma once

#include "../common/Protocol.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace phonecam {

struct DiscoveredDevice {
    std::string ip;
    std::string deviceName;
    int tcpPort = kTcpPort;
    bool pinRequired = false;
    std::string facing = "front";
    bool streaming = false;
    uint64_t lastSeenMs = 0;
};

struct ConnectionStatus {
    bool connected = false;
    bool connecting = false;
    bool authenticated = false;
    bool isUsb = false;
    std::string remoteIp;
    std::string deviceName;
    std::string lastError;
    std::string camerasJson = "[]";
    std::string telemetryJson = "{}";
    double latencyMs = 0.0;
    double rxFps = 0.0;
};

using VideoPacketCallback = std::function<void(
    PacketType type,
    uint8_t flags,
    const uint8_t* data,
    size_t size,
    uint32_t timestampMs)>;

class NetworkClient {
public:
    NetworkClient() = default;
    ~NetworkClient();

    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    bool StartDiscovery();
    void StopDiscovery();
    std::vector<DiscoveredDevice> GetDiscoveredDevices();

    bool Connect(const std::string& ip, uint16_t port, const std::string& pin, bool isUsb, VideoPacketCallback onVideo);
    void Disconnect();

    bool SendControlJson(const std::string& json);
    ConnectionStatus GetStatus();

private:
    void DiscoveryLoop();
    void ClientLoop(std::string ip, uint16_t port, std::string pin, bool isUsb, VideoPacketCallback onVideo);
    bool SendPacket(uint64_t sockHandle, PacketType type, uint8_t flags, const void* payload, uint32_t size, uint32_t tsMs);
    static std::string Sha256Hex(const std::string& input);

    std::atomic<bool> discRunning_{ false };
    std::thread discThread_;
    uintptr_t discSocket_ = ~uintptr_t(0);
    std::mutex discMutex_;
    std::vector<DiscoveredDevice> devices_;

    std::atomic<bool> connRunning_{ false };
    std::thread connThread_;
    std::mutex sockMutex_;
    uintptr_t clientSocket_ = ~uintptr_t(0);

    std::mutex statusMutex_;
    ConnectionStatus status_;
};

} // namespace phonecam
