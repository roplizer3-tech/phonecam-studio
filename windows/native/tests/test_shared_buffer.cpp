#include "../common/SharedFrameBuffer.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace phonecam;

int main() {
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring ringPath = std::wstring(tempDir) + L"phonecam_test_ring_" + std::to_wstring(GetCurrentProcessId()) + L".bin";
    DeleteFileW(ringPath.c_str());

    SharedFrameWriter writer;
    assert(writer.Open(ringPath));

    SharedFrameReader reader;
    assert(reader.Open(ringPath));

    constexpr uint32_t W = 640, H = 360;
    constexpr size_t FrameBytes = W * H * 3 / 2;

    std::atomic<bool> done{ false };
    std::atomic<uint64_t> readsOk{ 0 };

    std::thread readerThread([&]() {
        std::vector<uint8_t> buf;
        uint64_t lastSeq = 0;
        while (!done.load()) {
            uint32_t w = 0, h = 0, fn = 0, fd = 0;
            uint64_t seq = 0;
            if (reader.ReadLatestFrameNV12(buf, w, h, fn, fd, seq)) {
                assert(w == W && h == H);
                assert(buf.size() == FrameBytes);
                assert(seq >= lastSeq);
                lastSeq = seq;
                // Every byte of Y plane for a written frame equals its uniform tag
                const uint8_t tag = buf[0];
                assert(buf[W * H - 1] == tag);
                assert(buf[W * H / 2] == tag);
                readsOk.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::vector<uint8_t> frame(FrameBytes, 0);
    for (uint32_t i = 1; i <= 150; ++i) {
        const uint8_t tag = static_cast<uint8_t>((i % 200) + 20);
        std::fill(frame.begin(), frame.begin() + W * H, tag);
        std::fill(frame.begin() + W * H, frame.end(), 128);
        assert(writer.WriteFrameNV12(frame.data(), W, H, 30, 1));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    done.store(true);
    readerThread.join();
    assert(readsOk.load() > 0);

    // Closing writer clears heartbeat -> reader should report no live stream
    writer.Close();
    std::vector<uint8_t> stale;
    uint32_t w = 0, h = 0, fn = 0, fd = 0;
    uint64_t seq = 0;
    assert(!reader.ReadLatestFrameNV12(stale, w, h, fn, fd, seq));

    reader.Close();
    DeleteFileW(ringPath.c_str());

    std::cout << "[PASS] SharedFrameBufferTests (verified reads=" << readsOk.load() << ")\n";
    return 0;
}
