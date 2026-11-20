#include "../common/Protocol.h"

#include <cassert>
#include <iostream>

using namespace phonecam;

int main() {
    PacketHeader h{};
    h.magic = kPacketMagic;
    h.type = static_cast<uint8_t>(PacketType::VideoH264);
    h.flags = kFlagKeyFrame | kFlagFrontCamera | (3 << kFlagRotationShift);
    h.reserved = 0;
    h.payloadSize = 1024 * 64;
    h.timestampMs = 4242;

    assert(ValidateHeader(h));
    assert(ExtractRotationDeg(h.flags) == 270);
    assert(IsFrontCameraFlag(h.flags));

    // Reject bad magic
    PacketHeader badMagic = h;
    badMagic.magic = 0x12345678;
    assert(!ValidateHeader(badMagic));

    // Reject oversized payload (> 8 MiB)
    PacketHeader oversized = h;
    oversized.payloadSize = kMaxPayloadBytes + 1;
    assert(!ValidateHeader(oversized));

    // Reject unknown type
    PacketHeader badType = h;
    badType.type = 99;
    assert(!ValidateHeader(badType));

    std::cout << "[PASS] ProtocolTests\n";
    return 0;
}
