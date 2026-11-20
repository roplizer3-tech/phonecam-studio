#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace phonecam {

struct AiFaceResult {
    bool detected = false;
    float confidence = 0.0f;
    // Normalized [0..1] bounding box in oriented frame space
    float boxX = 0.35f;
    float boxY = 0.20f;
    float boxW = 0.30f;
    float boxH = 0.45f;
    // Normalized [0..1] 5 YuNet landmarks: rightEye, leftEye, nose, rightMouth, leftMouth
    float rightEyeX = 0.44f, rightEyeY = 0.38f;
    float leftEyeX = 0.56f, leftEyeY = 0.38f;
    float noseX = 0.50f, noseY = 0.48f;
    float rightMouthX = 0.45f, rightMouthY = 0.56f;
    float leftMouthX = 0.55f, leftMouthY = 0.56f;
    int framesSinceSeen = 999;
};

struct AiStateSnapshot {
    static constexpr uint32_t kMaskW = 256;
    static constexpr uint32_t kMaskH = 256;

    std::vector<uint8_t> mask256; // 256*256 R8_UNORM matte (255=foreground, 0=background)
    bool hasValidMask = false;
    AiFaceResult face;
    bool usingDirectML = false;
    bool modelsLoaded = false;
};

class AiEngine {
public:
    AiEngine();
    ~AiEngine();

    AiEngine(const AiEngine&) = delete;
    AiEngine& operator=(const AiEngine&) = delete;

    bool Initialize(const std::wstring& modelsDirOverride = L"");
    void Shutdown();

    bool IsModelsLoaded() const { return modelsLoaded_; }
    bool IsUsingDirectML() const { return usingDirectML_; }
    const std::string& StatusSummary() const { return statusSummary_; }

    // Synchronous single-frame inference (used by unit tests & worker loop)
    bool ProcessFrameSync(const uint8_t* bgra, uint32_t w, uint32_t h, bool runSeg, bool runFace);

    // Non-blocking submit from GPU/Video thread; AI worker processes latest frame asynchronously
    void SubmitFrameAsync(const uint8_t* bgra, uint32_t w, uint32_t h, bool needSeg, bool needFace);
    void GetLatestState(AiStateSnapshot& outSnapshot);

    static std::wstring FindModelsDirectory();

private:
    struct Impl;
    Impl* impl_ = nullptr;

    void WorkerLoop();

    bool modelsLoaded_ = false;
    bool usingDirectML_ = false;
    std::string statusSummary_ = "Not initialized";

    std::mutex stateMutex_;
    AiStateSnapshot currentState_;

    std::mutex jobMutex_;
    std::condition_variable jobCv_;
    bool hasPendingJob_ = false;
    bool pendingNeedSeg_ = false;
    bool pendingNeedFace_ = false;
    uint32_t pendingW_ = 0;
    uint32_t pendingH_ = 0;
    std::vector<uint8_t> pendingBgra_;

    std::atomic<bool> running_{ false };
    std::thread workerThread_;
};

} // namespace phonecam
