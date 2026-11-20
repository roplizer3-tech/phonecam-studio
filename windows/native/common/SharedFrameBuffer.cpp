#include "SharedFrameBuffer.h"

#include <shlobj.h>
#include <algorithm>
#include <cstring>

namespace phonecam {

namespace {

void EnsureParentDirExists(const std::wstring& filePath) {
    const size_t slash = filePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    std::wstring dir = filePath.substr(0, slash);
    size_t pos = 0;
    while ((pos = dir.find_first_of(L"\\/", pos + 1)) != std::wstring::npos) {
        std::wstring sub = dir.substr(0, pos);
        if (!sub.empty() && sub.back() != L':') {
            CreateDirectoryW(sub.c_str(), nullptr);
        }
    }
    CreateDirectoryW(dir.c_str(), nullptr);
}

constexpr size_t TotalMappingBytes() {
    return sizeof(SharedCamHeader) + static_cast<size_t>(kSharedSlotCount) * kSlotStrideBytes;
}

} // namespace

std::wstring DefaultSharedRingPath() {
    PWSTR programData = nullptr;
    std::wstring base = L"C:\\ProgramData";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData)) && programData) {
        base = programData;
        CoTaskMemFree(programData);
    }
    return base + L"\\PhoneCamStudio\\vcam_ring.bin";
}

SharedFrameWriter::~SharedFrameWriter() {
    Close();
}

bool SharedFrameWriter::Open(const std::wstring& path) {
    Close();
    const std::wstring target = path.empty() ? DefaultSharedRingPath() : path;
    EnsureParentDirExists(target);

    file_ = CreateFileW(
        target.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    totalBytes_ = TotalMappingBytes();
    LARGE_INTEGER sizeLi{};
    sizeLi.QuadPart = static_cast<LONGLONG>(totalBytes_);
    if (!SetFilePointerEx(file_, sizeLi, nullptr, FILE_BEGIN) || !SetEndOfFile(file_)) {
        Close();
        return false;
    }

    mapping_ = CreateFileMappingW(
        file_,
        nullptr,
        PAGE_READWRITE,
        static_cast<DWORD>(totalBytes_ >> 32),
        static_cast<DWORD>(totalBytes_ & 0xFFFFFFFFu),
        nullptr);
    if (!mapping_) {
        Close();
        return false;
    }

    base_ = static_cast<uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, totalBytes_));
    if (!base_) {
        Close();
        return false;
    }

    auto* hdr = reinterpret_cast<SharedCamHeader*>(base_);
    if (hdr->magic != kSharedMagic || hdr->version != kSharedVersion) {
        std::memset(hdr, 0, sizeof(SharedCamHeader));
        hdr->magic = kSharedMagic;
        hdr->version = kSharedVersion;
        hdr->width = 1280;
        hdr->height = 720;
        hdr->fpsNum = 30;
        hdr->fpsDen = 1;
    }
    hdr->heartbeatMs.store(GetTickCount64(), std::memory_order_release);
    return true;
}

void SharedFrameWriter::Close() {
    if (base_) {
        auto* hdr = reinterpret_cast<SharedCamHeader*>(base_);
        hdr->heartbeatMs.store(0, std::memory_order_release);
        UnmapViewOfFile(base_);
        base_ = nullptr;
    }
    if (mapping_) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
    totalBytes_ = 0;
}

bool SharedFrameWriter::WriteFrameNV12(
    const uint8_t* nv12Data,
    uint32_t width,
    uint32_t height,
    uint32_t fpsNum,
    uint32_t fpsDen) {
    if (!base_ || !nv12Data || width == 0 || height == 0 ||
        width > kMaxFrameWidth || height > kMaxFrameHeight) {
        return false;
    }
    const size_t frameBytes = static_cast<size_t>(width) * height * 3 / 2;
    if (frameBytes > kSlotStrideBytes) {
        return false;
    }

    auto* hdr = reinterpret_cast<SharedCamHeader*>(base_);
    const uint32_t prevSlot = hdr->activeSlot.load(std::memory_order_relaxed) % kSharedSlotCount;
    const uint32_t nextSlot = (prevSlot + 1u) % kSharedSlotCount;

    uint8_t* slotPtr = base_ + sizeof(SharedCamHeader) + static_cast<size_t>(nextSlot) * kSlotStrideBytes;
    std::memcpy(slotPtr, nv12Data, frameBytes);

    hdr->width = width;
    hdr->height = height;
    hdr->fpsNum = fpsNum > 0 ? fpsNum : 30u;
    hdr->fpsDen = fpsDen > 0 ? fpsDen : 1u;

    hdr->activeSlot.store(nextSlot, std::memory_order_release);
    hdr->frameSeq.fetch_add(1u, std::memory_order_release);
    hdr->heartbeatMs.store(GetTickCount64(), std::memory_order_release);
    return true;
}

void SharedFrameWriter::TouchHeartbeat() {
    if (!base_) return;
    auto* hdr = reinterpret_cast<SharedCamHeader*>(base_);
    hdr->heartbeatMs.store(GetTickCount64(), std::memory_order_release);
}

SharedFrameReader::~SharedFrameReader() {
    Close();
}

bool SharedFrameReader::Open(const std::wstring& path) {
    Close();
    path_ = path.empty() ? DefaultSharedRingPath() : path;
    totalBytes_ = TotalMappingBytes();

    file_ = CreateFileW(
        path_.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file_, &fileSize) || static_cast<size_t>(fileSize.QuadPart) < totalBytes_) {
        Close();
        return false;
    }

    mapping_ = CreateFileMappingW(
        file_,
        nullptr,
        PAGE_READONLY,
        static_cast<DWORD>(totalBytes_ >> 32),
        static_cast<DWORD>(totalBytes_ & 0xFFFFFFFFu),
        nullptr);
    if (!mapping_) {
        Close();
        return false;
    }

    base_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, totalBytes_));
    if (!base_) {
        Close();
        return false;
    }
    return true;
}

void SharedFrameReader::Close() {
    if (base_) {
        UnmapViewOfFile(base_);
        base_ = nullptr;
    }
    if (mapping_) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
}

bool SharedFrameReader::ReadLatestFrameNV12(
    std::vector<uint8_t>& outNv12,
    uint32_t& outWidth,
    uint32_t& outHeight,
    uint32_t& outFpsNum,
    uint32_t& outFpsDen,
    uint64_t& outSeq,
    uint64_t maxAgeMs) {
    if (!base_) {
        if (!Open(path_)) return false;
    }

    const auto* hdr = reinterpret_cast<const SharedCamHeader*>(base_);
    if (hdr->magic != kSharedMagic || hdr->version != kSharedVersion) {
        return false;
    }

    const uint64_t hb = hdr->heartbeatMs.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    if (hb == 0 || (now > hb && (now - hb) > maxAgeMs)) {
        return false;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        const uint64_t seqBefore = hdr->frameSeq.load(std::memory_order_acquire);
        if (seqBefore == 0) return false;

        const uint32_t slot = hdr->activeSlot.load(std::memory_order_acquire) % kSharedSlotCount;
        const uint32_t w = hdr->width;
        const uint32_t h = hdr->height;
        const uint32_t fn = hdr->fpsNum;
        const uint32_t fd = hdr->fpsDen;

        if (w == 0 || h == 0 || w > kMaxFrameWidth || h > kMaxFrameHeight) {
            return false;
        }
        const size_t frameBytes = static_cast<size_t>(w) * h * 3 / 2;
        if (frameBytes > kSlotStrideBytes) return false;

        outNv12.resize(frameBytes);
        const uint8_t* slotPtr = base_ + sizeof(SharedCamHeader) + static_cast<size_t>(slot) * kSlotStrideBytes;
        std::memcpy(outNv12.data(), slotPtr, frameBytes);

        std::atomic_thread_fence(std::memory_order_acquire);
        const uint64_t seqAfter = hdr->frameSeq.load(std::memory_order_acquire);
        if (seqBefore == seqAfter || (seqAfter - seqBefore) < kSharedSlotCount) {
            outWidth = w;
            outHeight = h;
            outFpsNum = fn;
            outFpsDen = fd;
            outSeq = seqAfter;
            return true;
        }
    }
    return false;
}

} // namespace phonecam
