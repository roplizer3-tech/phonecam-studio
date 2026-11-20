#pragma once

#include <cstdint>
#include <cstring>

namespace phonecam {

inline constexpr uint16_t kTcpPort = 54820;
inline constexpr uint16_t kUdpDiscoveryPort = 54821;
inline constexpr uint32_t kPacketMagic = 0x5043414Du; // "PCAM" little-endian
inline constexpr uint32_t kHeaderSize = 16u;
inline constexpr uint32_t kMaxPayloadBytes = 8u * 1024u * 1024u;
inline constexpr int kProtocolVersion = 1;

enum class PacketType : uint8_t {
    Hello = 1,
    Auth = 2,
    VideoH264 = 3,
    VideoMjpeg = 4,
    Control = 5,
    Telemetry = 6,
    Ping = 7,
    Pong = 8
};

enum PacketFlags : uint8_t {
    kFlagKeyFrame = 0x01,
    kFlagCodecConfig = 0x02,
    kFlagFrontCamera = 0x04,
    kFlagRotationMask = 0x18,
    kFlagRotationShift = 3,
    kFlagMirroredBySensor = 0x20
};

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;
    uint8_t type;
    uint8_t flags;
    uint16_t reserved;
    uint32_t payloadSize;
    uint32_t timestampMs;
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == kHeaderSize, "PacketHeader must be exactly 16 bytes");

inline bool ValidateHeader(const PacketHeader& h) {
    if (h.magic != kPacketMagic) return false;
    if (h.payloadSize > kMaxPayloadBytes) return false;
    if (h.type < static_cast<uint8_t>(PacketType::Hello) ||
        h.type > static_cast<uint8_t>(PacketType::Pong)) {
        return false;
    }
    return true;
}

inline int ExtractRotationDeg(uint8_t flags) {
    const int code = (flags & kFlagRotationMask) >> kFlagRotationShift;
    return (code & 0x03) * 90;
}

inline bool IsFrontCameraFlag(uint8_t flags) {
    return (flags & kFlagFrontCamera) != 0;
}

} // namespace phonecam
