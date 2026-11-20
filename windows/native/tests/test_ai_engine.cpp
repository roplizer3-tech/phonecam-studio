#include "TestCheck.h"
#include "../engine/AiEngine.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <iostream>
#include <vector>

using namespace phonecam;

int main() {
    AiEngine ai;

    std::wstring modelsDir;
    wchar_t envBuf[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"PHONECAM_TEST_MODELS_DIR", envBuf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        modelsDir = envBuf;
    }

    const bool okInit = ai.Initialize(modelsDir);

#if defined(PHONECAM_HAS_ONNX)
    if (!modelsDir.empty()) {
        PC_CHECK(okInit);
        PC_CHECK(ai.AreModelsLoaded());

        constexpr uint32_t W = 480, H = 270;
        std::vector<uint8_t> bgra(static_cast<size_t>(W) * H * 4u, 25u);
        for (uint32_t y = 40; y < 230; ++y) {
            for (uint32_t x = 150; x < 330; ++x) {
                const size_t idx = (static_cast<size_t>(y) * W + x) * 4u;
                bgra[idx + 0] = 145; // B
                bgra[idx + 1] = 175; // G
                bgra[idx + 2] = 225; // R (skin tone silhouette)
                bgra[idx + 3] = 255;
            }
        }

        const bool ran = ai.ProcessFrameSync(bgra.data(), W, H, true, true);
        PC_CHECK(ran);

        AiStateSnapshot snap;
        ai.GetLatestState(snap);
        PC_CHECK(snap.modelsLoaded);
        PC_CHECK(snap.hasValidMask);
        PC_CHECK(snap.mask256.size() == AiStateSnapshot::kMaskW * AiStateSnapshot::kMaskH);
        std::cout << "[PASS] AiEngineTests (ONNX models loaded & executed; DirectML="
                  << (snap.usingDirectML ? "true" : "false") << ")\n";
    } else {
        std::cout << "[PASS] AiEngineTests (PHONECAM_HAS_ONNX built; skipped model inference because PHONECAM_TEST_MODELS_DIR unset)\n";
    }
#else
    (void)okInit;
    std::cout << "[PASS] AiEngineTests (built without ONNX Runtime; gracefully disabled)\n";
#endif

    ai.Shutdown();
    return 0;
}
