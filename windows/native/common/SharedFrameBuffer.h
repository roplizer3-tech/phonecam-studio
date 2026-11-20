#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace phonecam {

inline constexpr uint32_t kSharedMagic = 0x50435356u; // "PCSV"
inline constexpr uint32_t kSharedVersion = 1u;
inline constexpr uint32_t kSharedSlotCount = 3u;
inline constexpr uint32_t kMaxFrameWidth = 1920u;
inline constexpr uint32_t kMaxFrameHeight = 1080u;
inline constexpr uint32_t kSlotStrideBytes = 3u * 1024u * 1024u; // 3 MiB per NV12 slot
inline constexpr uint64_t kHeartbeatTimeoutMs = 2000u;

struct alignas(64) SharedCamHeader {
    uint32_t magic;
    uint32_t version;
    std::atomic<uint32_t> activeSlot;
    uint32_t reserved0;
    std::atomic<uint64_t> frameSeq;
    std::atomic<uint64_t> heartbeatMs;
    uint32_t width;
    uint32_t height;
    uint32_t fpsNum;
    uint32_t fpsDen;
    uint32_t reserved1[4];
};

static_assert(std::atomic<uint32_t>::is_always_lock_free, "uint32_t atomic must be lock-free");
static_assert(std::atomic<uint64_t>::is_always_lock_free, "uint64_t atomic must be lock-free");
static_assert(sizeof(SharedCamHeader) == 64, "SharedCamHeader must stay 64 bytes");

std::wstring DefaultSharedRingPath();

class SharedFrameWriter {
public:
    SharedFrameWriter() = default;
    ~SharedFrameWriter();

    SharedFrameWriter(const SharedFrameWriter&) = delete;
    SharedFrameWriter& operator=(const SharedFrameWriter&) = delete;

    bool Open(const std::wstring& path = L"");
    void Close();
    bool IsOpen() const { return base_ != nullptr; }

    bool WriteFrameNV12(const uint8_t* nv12Data, uint32_t width, uint32_t height, uint32_t fpsNum, uint32_t fpsDen);
    void TouchHeartbeat();

private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    uint8_t* base_ = nullptr;
    size_t totalBytes_ = 0;
};

class SharedFrameReader {
public:
    SharedFrameReader() = default;
    ~SharedFrameReader();

    SharedFrameReader(const SharedFrameReader&) = delete;
    SharedFrameReader& operator=(const SharedFrameReader&) = delete;

    bool Open(const std::wstring& path = L"");
    void Close();
    bool IsOpen() const { return base_ != nullptr; }

    bool ReadLatestFrameNV12(
        std::vector<uint8_t>& outNv12,
        uint32_t& outWidth,
        uint32_t& outHeight,
        uint32_t& outFpsNum,
        uint32_t& outFpsDen,
        uint64_t& outSeq,
        uint64_t maxAgeMs = kHeartbeatTimeoutMs);

private:
    std::wstring path_;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    const uint8_t* base_ = nullptr;
    size_t totalBytes_ = 0;
};

} // namespace phonecam
