#include "TestCheck.h"
#include "../common/Protocol.h"

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

    PC_CHECK(ValidateHeader(h));
    PC_CHECK(ExtractRotationDeg(h.flags) == 270);
    PC_CHECK(IsFrontCameraFlag(h.flags));

    // Reject bad magic
    PacketHeader badMagic = h;
    badMagic.magic = 0x12345678;
    PC_CHECK(!ValidateHeader(badMagic));

    // Reject oversized payload (> 8 MiB)
    PacketHeader oversized = h;
    oversized.payloadSize = kMaxPayloadBytes + 1;
    PC_CHECK(!ValidateHeader(oversized));

    // Reject unknown type
    PacketHeader badType = h;
    badType.type = 99;
    PC_CHECK(!ValidateHeader(badType));

    std::cout << "[PASS] ProtocolTests\n";
    return 0;
}
