#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include "NetworkClient.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace phonecam {

namespace {

struct WinsockGuard {
    WinsockGuard() {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    ~WinsockGuard() {
        WSACleanup();
    }
};

void EnsureWinsock() {
    static WinsockGuard guard;
}

bool RecvExact(SOCKET s, void* buf, size_t len, const std::atomic<bool>& running) {
    uint8_t* ptr = static_cast<uint8_t*>(buf);
    size_t remaining = len;
    while (remaining > 0 && running.load()) {
        int n = recv(s, reinterpret_cast<char*>(ptr), static_cast<int>(remaining), 0);
        if (n == 0) return false;
        if (n < 0) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) continue;
            return false;
        }
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }
    return remaining == 0;
}

std::string ExtractJsonString(const std::string& json, const std::string& key, const std::string& fallback = "") {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return fallback;
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return fallback;
    size_t end = pos + 1;
    while (end < json.size() && json[end] != '"') {
        if (json[end] == '\\' && end + 1 < json.size()) end += 2;
        else ++end;
    }
    if (end >= json.size()) return fallback;
    return json.substr(pos + 1, end - pos - 1);
}

bool ExtractJsonBool(const std::string& json, const std::string& key, bool fallback = false) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return fallback;
    while (pos + 1 < json.size() && (json[pos + 1] == ' ' || json[pos + 1] == '\t')) ++pos;
    if (json.compare(pos + 1, 4, "true") == 0) return true;
    if (json.compare(pos + 1, 5, "false") == 0) return false;
    return fallback;
}

int ExtractJsonInt(const std::string& json, const std::string& key, int fallback = 0) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return fallback;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return fallback;
    try {
        return std::stoi(json.substr(pos + 1));
    } catch (...) {
        return fallback;
    }
}

std::string ExtractJsonArray(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "[]";
    pos = json.find('[', pos + pattern.size());
    if (pos == std::string::npos) return "[]";
    int depth = 0;
    bool inStr = false;
    for (size_t i = pos; i < json.size(); ++i) {
        char c = json[i];
        if (c == '"' && (i == 0 || json[i - 1] != '\\')) inStr = !inStr;
        if (!inStr) {
            if (c == '[') ++depth;
            else if (c == ']') {
                --depth;
                if (depth == 0) return json.substr(pos, i - pos + 1);
            }
        }
    }
    return "[]";
}

} // namespace

NetworkClient::~NetworkClient() {
    Disconnect();
    StopDiscovery();
}

std::string NetworkClient::Sha256Hex(const std::string& input) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    uint8_t digest[32]{};
    DWORD hashLen = sizeof(digest);

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) {
        if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())), static_cast<ULONG>(input.size()), 0);
            BCryptFinishHash(hash, digest, hashLen, 0);
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }

    char hex[65]{};
    for (size_t i = 0; i < 32; ++i) {
        std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    return std::string(hex, 64);
}

bool NetworkClient::StartDiscovery() {
    EnsureWinsock();
    if (discRunning_.load()) return true;
    discRunning_.store(true);
    discThread_ = std::thread(&NetworkClient::DiscoveryLoop, this);
    return true;
}

void NetworkClient::StopDiscovery() {
    if (!discRunning_.exchange(false)) return;
    if (discSocket_ != static_cast<uintptr_t>(INVALID_SOCKET)) {
        closesocket(static_cast<SOCKET>(discSocket_));
        discSocket_ = static_cast<uintptr_t>(INVALID_SOCKET);
    }
    if (discThread_.joinable()) {
        discThread_.join();
    }
}

void NetworkClient::DiscoveryLoop() {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        discRunning_.store(false);
        return;
    }
    discSocket_ = static_cast<uintptr_t>(s);

    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    DWORD timeoutMs = 500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(kUdpDiscoveryPort);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        discSocket_ = static_cast<uintptr_t>(INVALID_SOCKET);
        discRunning_.store(false);
        return;
    }

    char buf[2048];
    while (discRunning_.load()) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(s, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n > 0) {
            buf[n] = '\0';
            std::string json(buf, static_cast<size_t>(n));
            if (ExtractJsonString(json, "magic") == "PCAM_DISC") {
                char ipStr[INET_ADDRSTRLEN]{};
                inet_ntop(AF_INET, &from.sin_addr, ipStr, sizeof(ipStr));

                DiscoveredDevice d;
                d.ip = ipStr;
                d.deviceName = ExtractJsonString(json, "deviceName", "Android Phone");
                d.tcpPort = ExtractJsonInt(json, "tcpPort", kTcpPort);
                d.pinRequired = ExtractJsonBool(json, "pinRequired", true);
                d.facing = ExtractJsonString(json, "facing", "front");
                d.streaming = ExtractJsonBool(json, "streaming", false);
                d.lastSeenMs = GetTickCount64();

                std::lock_guard<std::mutex> lk(discMutex_);
                auto it = std::find_if(devices_.begin(), devices_.end(), [&](const DiscoveredDevice& x) {
                    return x.ip == d.ip;
                });
                if (it != devices_.end()) *it = d;
                else devices_.push_back(d);
            }
        }

        const uint64_t now = GetTickCount64();
        std::lock_guard<std::mutex> lk(discMutex_);
        devices_.erase(
            std::remove_if(devices_.begin(), devices_.end(), [&](const DiscoveredDevice& x) {
                return (now > x.lastSeenMs) && ((now - x.lastSeenMs) > 6500u);
            }),
            devices_.end());
    }

    closesocket(s);
    discSocket_ = static_cast<uintptr_t>(INVALID_SOCKET);
}

std::vector<DiscoveredDevice> NetworkClient::GetDiscoveredDevices() {
    std::lock_guard<std::mutex> lk(discMutex_);
    return devices_;
}

bool NetworkClient::Connect(const std::string& ip, uint16_t port, const std::string& pin, bool isUsb, VideoPacketCallback onVideo) {
    EnsureWinsock();
    Disconnect();

    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        status_ = ConnectionStatus{};
        status_.connecting = true;
        status_.isUsb = isUsb;
        status_.remoteIp = ip;
    }

    connRunning_.store(true);
    connThread_ = std::thread(&NetworkClient::ClientLoop, this, ip, port, pin, isUsb, std::move(onVideo));
    return true;
}

void NetworkClient::Disconnect() {
    connRunning_.store(false);
    {
        std::lock_guard<std::mutex> lk(sockMutex_);
        if (clientSocket_ != static_cast<uintptr_t>(INVALID_SOCKET)) {
            shutdown(static_cast<SOCKET>(clientSocket_), SD_BOTH);
            closesocket(static_cast<SOCKET>(clientSocket_));
            clientSocket_ = static_cast<uintptr_t>(INVALID_SOCKET);
        }
    }
    if (connThread_.joinable()) {
        connThread_.join();
    }
    std::lock_guard<std::mutex> lk(statusMutex_);
    status_.connected = false;
    status_.connecting = false;
    status_.authenticated = false;
    status_.rxFps = 0.0;
}

bool NetworkClient::SendPacket(uint64_t sockHandle, PacketType type, uint8_t flags, const void* payload, uint32_t size, uint32_t tsMs) {
    if (sockHandle == static_cast<uint64_t>(INVALID_SOCKET) || size > kMaxPayloadBytes) return false;
    PacketHeader hdr{};
    hdr.magic = kPacketMagic;
    hdr.type = static_cast<uint8_t>(type);
    hdr.flags = flags;
    hdr.reserved = 0;
    hdr.payloadSize = size;
    hdr.timestampMs = tsMs;

    std::lock_guard<std::mutex> lk(sockMutex_);
    if (clientSocket_ != sockHandle) return false;
    SOCKET s = static_cast<SOCKET>(clientSocket_);

    const char* hdrPtr = reinterpret_cast<const char*>(&hdr);
    int sent = 0;
    while (sent < static_cast<int>(sizeof(hdr))) {
        int r = send(s, hdrPtr + sent, static_cast<int>(sizeof(hdr)) - sent, 0);
        if (r <= 0) return false;
        sent += r;
    }

    if (size > 0 && payload) {
        const char* pPtr = static_cast<const char*>(payload);
        uint32_t pSent = 0;
        while (pSent < size) {
            int r = send(s, pPtr + pSent, static_cast<int>(size - pSent), 0);
            if (r <= 0) return false;
            pSent += static_cast<uint32_t>(r);
        }
    }
    return true;
}

bool NetworkClient::SendControlJson(const std::string& json) {
    uint64_t s = static_cast<uint64_t>(INVALID_SOCKET);
    {
        std::lock_guard<std::mutex> lk(sockMutex_);
        s = clientSocket_;
    }
    return SendPacket(s, PacketType::Control, 0, json.data(), static_cast<uint32_t>(json.size()), static_cast<uint32_t>(GetTickCount64()));
}

ConnectionStatus NetworkClient::GetStatus() {
    std::lock_guard<std::mutex> lk(statusMutex_);
    return status_;
}

void NetworkClient::ClientLoop(std::string ip, uint16_t port, std::string pin, bool, VideoPacketCallback onVideo) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        std::lock_guard<std::mutex> lk(statusMutex_);
        status_.connecting = false;
        status_.lastError = "Failed creating TCP socket";
        return;
    }

    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
    DWORD timeoutMs = 1500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    int rcvBuf = 512 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        closesocket(s);
        std::lock_guard<std::mutex> lk(statusMutex_);
        status_.connecting = false;
        status_.lastError = "Invalid IP address";
        return;
    }

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        std::lock_guard<std::mutex> lk(statusMutex_);
        status_.connecting = false;
        status_.lastError = "Cannot connect to " + ip + ":" + std::to_string(port);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(sockMutex_);
        clientSocket_ = static_cast<uintptr_t>(s);
    }
    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        status_.connecting = false;
        status_.connected = true;
        status_.lastError.clear();
    }

    std::vector<uint8_t> payload;
    uint64_t lastPingMs = GetTickCount64();
    uint64_t fpsWindowStart = GetTickCount64();
    uint32_t frameCounter = 0;

    while (connRunning_.load()) {
        PacketHeader hdr{};
        if (!RecvExact(s, &hdr, sizeof(hdr), connRunning_)) break;
        if (!ValidateHeader(hdr)) {
            std::lock_guard<std::mutex> lk(statusMutex_);
            status_.lastError = "Protocol header validation failed";
            break;
        }
        payload.resize(hdr.payloadSize);
        if (hdr.payloadSize > 0 && !RecvExact(s, payload.data(), hdr.payloadSize, connRunning_)) {
            break;
        }

        const auto pType = static_cast<PacketType>(hdr.type);
        switch (pType) {
            case PacketType::Hello: {
                std::string json(reinterpret_cast<const char*>(payload.data()), payload.size());
                bool pinReq = ExtractJsonBool(json, "pinRequired", false);
                std::string nonce = ExtractJsonString(json, "nonce");
                std::string devName = ExtractJsonString(json, "deviceName", "Android");
                std::string cams = ExtractJsonArray(json, "cameras");
                {
                    std::lock_guard<std::mutex> lk(statusMutex_);
                    status_.deviceName = devName;
                    status_.camerasJson = cams;
                    status_.authenticated = !pinReq;
                }
                if (pinReq) {
                    std::string hash = Sha256Hex(nonce + ":" + pin);
                    std::string authJson = "{\"pinHash\":\"" + hash + "\"}";
                    SendPacket(static_cast<uint64_t>(s), PacketType::Auth, 0, authJson.data(), static_cast<uint32_t>(authJson.size()), 0);
                }
                break;
            }
            case PacketType::Auth: {
                std::string json(reinterpret_cast<const char*>(payload.data()), payload.size());
                bool ok = ExtractJsonBool(json, "ok", false);
                std::string reason = ExtractJsonString(json, "reason", "Authentication failed");
                std::lock_guard<std::mutex> lk(statusMutex_);
                status_.authenticated = ok;
                if (!ok) status_.lastError = reason;
                break;
            }
            case PacketType::VideoH264:
            case PacketType::VideoMjpeg: {
                if ((hdr.flags & kFlagCodecConfig) == 0) {
                    ++frameCounter;
                }
                if (onVideo) {
                    onVideo(pType, hdr.flags, payload.data(), payload.size(), hdr.timestampMs);
                }
                break;
            }
            case PacketType::Telemetry: {
                std::string json(reinterpret_cast<const char*>(payload.data()), payload.size());
                std::lock_guard<std::mutex> lk(statusMutex_);
                status_.telemetryJson = json;
                break;
            }
            case PacketType::Pong: {
                if (payload.size() == sizeof(uint64_t)) {
                    uint64_t sentAt = 0;
                    std::memcpy(&sentAt, payload.data(), sizeof(uint64_t));
                    uint64_t now = GetTickCount64();
                    if (now >= sentAt) {
                        std::lock_guard<std::mutex> lk(statusMutex_);
                        status_.latencyMs = static_cast<double>(now - sentAt);
                    }
                }
                break;
            }
            default:
                break;
        }

        const uint64_t now = GetTickCount64();
        if (now - lastPingMs >= 1000u) {
            lastPingMs = now;
            SendPacket(static_cast<uint64_t>(s), PacketType::Ping, 0, &now, sizeof(now), static_cast<uint32_t>(now));
        }
        if (now - fpsWindowStart >= 1000u) {
            double elapsedSec = static_cast<double>(now - fpsWindowStart) / 1000.0;
            double fps = elapsedSec > 0.0 ? (static_cast<double>(frameCounter) / elapsedSec) : 0.0;
            fpsWindowStart = now;
            frameCounter = 0;
            std::lock_guard<std::mutex> lk(statusMutex_);
            status_.rxFps = fps;
        }
    }

    {
        std::lock_guard<std::mutex> lk(sockMutex_);
        if (clientSocket_ == static_cast<uintptr_t>(s)) {
            closesocket(s);
            clientSocket_ = static_cast<uintptr_t>(INVALID_SOCKET);
        }
    }
    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        status_.connected = false;
        status_.connecting = false;
        status_.authenticated = false;
        status_.rxFps = 0.0;
    }
}

} // namespace phonecam
