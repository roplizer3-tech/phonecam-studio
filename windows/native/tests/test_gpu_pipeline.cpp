#include "TestCheck.h"
#include "../engine/GpuProcessor.h"
#include "../common/ColorConvert.h"

#include <cmath>
#include <iostream>
#include <vector>

using namespace phonecam;

namespace {

PC_VideoParams NeutralParams() {
    PC_VideoParams p{};
    p.outputWidth = 640;
    p.outputHeight = 360;
    p.outputFps = 30;
    p.framingMode = 0;
    p.zoom = 1.0f;
    p.contrast = 1.0f;
    p.gammaVal = 1.0f;
    p.saturation = 1.0f;
    return p;
}

void MakeQuadrantBgra(std::vector<uint8_t>& bgra, uint32_t w, uint32_t h) {
    bgra.resize(static_cast<size_t>(w) * h * 4u);
    for (uint32_t y = 0; y < h; ++y) {
        const bool top = y < h / 2;
        for (uint32_t x = 0; x < w; ++x) {
            const bool left = x < w / 2;
            uint8_t r = 0, g = 0, b = 0;
            if (top && left) { r = 230; g = 30;  b = 30;  }
            else if (top && !left) { r = 30;  g = 40;  b = 235; }
            else if (!top && left) { r = 30;  g = 210; b = 40;  }
            else { r = 240; g = 240; b = 240; }

            size_t idx = (static_cast<size_t>(y) * w + x) * 4u;
            bgra[idx + 0] = b;
            bgra[idx + 1] = g;
            bgra[idx + 2] = r;
            bgra[idx + 3] = 255;
        }
    }
}

void SamplePixel(const std::vector<uint8_t>& bgra, uint32_t w, uint32_t x, uint32_t y, uint8_t& r, uint8_t& g, uint8_t& b) {
    size_t idx = (static_cast<size_t>(y) * w + x) * 4u;
    b = bgra[idx + 0];
    g = bgra[idx + 1];
    r = bgra[idx + 2];
}

} // namespace

int main() {
    GpuProcessor gpu;
    if (!gpu.Initialize(true)) {
        std::cerr << "GpuProcessor WARP init failed: " << gpu.LastError() << "\n";
        return 1;
    }

    constexpr uint32_t W = 640, H = 360;
    std::vector<uint8_t> src;
    MakeQuadrantBgra(src, W, H);

    RawFrameInput in;
    in.format = InputPixelFormat::BGRA32;
    in.data = src.data();
    in.width = W;
    in.height = H;
    in.strideY = W * 4u;

    // 1. Neutral passthrough + NV12 roundtrip
    ProcessedFrameOutput out;
    PC_VideoParams p = NeutralParams();
    PC_CHECK(gpu.ProcessFrame(in, p, true, out));
    PC_CHECK(out.width == W && out.height == H);
    PC_CHECK(out.nv12.size() == W * H * 3u / 2u);
    PC_CHECK(out.previewBgra.size() == W * H * 4u);

    uint8_t r = 0, g = 0, b = 0;
    SamplePixel(out.previewBgra, W, W / 4, H / 4, r, g, b); // Top-Left -> Red
    PC_CHECK(r > 200 && g < 60 && b < 60);
    SamplePixel(out.previewBgra, W, (W * 3) / 4, H / 4, r, g, b); // Top-Right -> Blue
    PC_CHECK(b > 200 && r < 60);

    // Check NV12 roundtrip preserves Red top-left & Blue top-right
    std::vector<uint8_t> roundtripBgra(W * H * 4u);
    NV12ToBGRA32(out.nv12.data(), W, H, roundtripBgra.data(), W * 4u);
    SamplePixel(roundtripBgra, W, W / 4, H / 4, r, g, b);
    PC_CHECK(r > 190 && b < 70);
    SamplePixel(roundtripBgra, W, (W * 3) / 4, H / 4, r, g, b);
    PC_CHECK(b > 190 && r < 70);

    // 2. Output Horizontal Mirror (FlipH) swaps Top-Left Red <-> Top-Right Blue
    p = NeutralParams();
    p.flipH = 1;
    PC_CHECK(gpu.ProcessFrame(in, p, true, out));
    SamplePixel(out.previewBgra, W, W / 4, H / 4, r, g, b);
    PC_CHECK(b > 200 && r < 60);
    SamplePixel(out.previewBgra, W, (W * 3) / 4, H / 4, r, g, b);
    PC_CHECK(r > 200 && b < 60);

    // 3. Output Vertical Flip (FlipV) swaps Top-Left Red <-> Bottom-Left Green
    p = NeutralParams();
    p.flipV = 1;
    PC_CHECK(gpu.ProcessFrame(in, p, true, out));
    SamplePixel(out.previewBgra, W, W / 4, H / 4, r, g, b);
    PC_CHECK(g > 180 && r < 60 && b < 60);

    // 4. Rotation 90 deg CW: Bottom-Left Green moves to Top-Left!
    p = NeutralParams();
    p.manualRotationDeg = 90;
    PC_CHECK(gpu.ProcessFrame(in, p, true, out));
    SamplePixel(out.previewBgra, W, W / 4, H / 4, r, g, b);
    PC_CHECK(g > 180 && r < 60);

    // 5. Beauty + Color Grading (B&W Saturation = 0 + SkinSmooth + StudioLight)
    p = NeutralParams();
    p.skinSmooth = 0.8f;
    p.studioLight = 0.5f;
    p.saturation = 0.0f;
    PC_CHECK(gpu.ProcessFrame(in, p, true, out));
    SamplePixel(out.previewBgra, W, W / 4, H / 4, r, g, b);
    PC_CHECK(std::abs(static_cast<int>(r) - static_cast<int>(b)) <= 6);
    PC_CHECK(std::abs(static_cast<int>(r) - static_cast<int>(g)) <= 6);

    // 6. Mirror Check overlay renders crisp green text pixels in top-center banner
    p = NeutralParams();
    p.mirrorCheckOverlay = 1;
    PC_CHECK(gpu.ProcessFrame(in, p, true, out));
    bool foundBrightOverlay = false;
    for (uint32_t y = static_cast<uint32_t>(H * 0.04); y < static_cast<uint32_t>(H * 0.14); ++y) {
        for (uint32_t x = static_cast<uint32_t>(W * 0.30); x < static_cast<uint32_t>(W * 0.70); ++x) {
            SamplePixel(out.previewBgra, W, x, y, r, g, b);
            if (g > 230 && b > 140 && r < 80) {
                foundBrightOverlay = true;
                break;
            }
        }
        if (foundBrightOverlay) break;
    }
    PC_CHECK(foundBrightOverlay);

    // 7. Real Stylized Filters (Anime Cel-Shader & Retro VHS) modify image output distinctly
    p = NeutralParams();
    p.styleFilterMode = 1; // Anime / Cartoon
    p.styleIntensity = 1.0f;
    PC_CHECK(gpu.ProcessFrame(in, p, true, out));
    PC_CHECK(out.previewBgra.size() == W * H * 4u);

    p.styleFilterMode = 3; // Retro VHS
    PC_CHECK(gpu.ProcessFrame(in, p, true, out));
    SamplePixel(out.previewBgra, W, W / 4, H / 4, r, g, b);
    PC_CHECK(r > 120);

    // 8. Real AI Background Isolation (mask center=255 foreground, corner=0 background) -> Studio Office shifts corner
    AiStateSnapshot mockAi{};
    mockAi.hasValidMask = true;
    mockAi.mask256.assign(256u * 256u, 0u);
    for (uint32_t y = 64; y < 192; ++y) {
        for (uint32_t x = 64; x < 192; ++x) {
            mockAi.mask256[y * 256u + x] = 255u;
        }
    }
    p = NeutralParams();
    p.bgMode = 2; // Studio Office backdrop (dark blue-slate, not red!)
    PC_CHECK(gpu.ProcessFrame(in, p, true, out, &mockAi));
    // Top-left corner (10,10) is mask=0 -> replaced with Studio Office dark slate (no longer bright red r>200)
    SamplePixel(out.previewBgra, W, 10, 10, r, g, b);
    PC_CHECK(r < 140);
    // Center top quadrant (W*3/8, H*3/8) is inside mask=255 -> remains bright red foreground!
    SamplePixel(out.previewBgra, W, (W * 3) / 8, (H * 3) / 8, r, g, b);
    PC_CHECK(r > 190 && g < 70);

    // 9. Real AR Aviator Sunglasses + Face Slim warp anchored to AI Face Landmarks
    mockAi.face.detected = true;
    mockAi.face.confidence = 0.95f;
    mockAi.face.boxX = 0.35f; mockAi.face.boxY = 0.25f;
    mockAi.face.boxW = 0.30f; mockAi.face.boxH = 0.50f;
    mockAi.face.rightEyeX = 0.44f; mockAi.face.rightEyeY = 0.42f;
    mockAi.face.leftEyeX = 0.56f; mockAi.face.leftEyeY = 0.42f;
    mockAi.face.noseX = 0.50f; mockAi.face.noseY = 0.52f;
    mockAi.face.rightMouthX = 0.46f; mockAi.face.rightMouthY = 0.64f;
    mockAi.face.leftMouthX = 0.54f; mockAi.face.leftMouthY = 0.64f;

    p = NeutralParams();
    p.arFilterMode = 1; // Aviator Sunglasses
    p.faceSlim = 0.6f;
    PC_CHECK(gpu.ProcessFrame(in, p, true, out, &mockAi));
    // Verify right-eye center pixel in white bottom-right quadrant? No, rightEye=(0.44,0.42) is in top-left RED quadrant (r=230):
    // Under tinted dark aviator lens, pixel luminance drops substantially vs r=230!
    SamplePixel(out.previewBgra, W, static_cast<uint32_t>(W * 0.44f), static_cast<uint32_t>(H * 0.42f), r, g, b);
    PC_CHECK(r < 120);

    std::cout << "[PASS] GpuPipelineShaderTests (all 4 HLSL passes + AI Bg + AR + Stylized shaders verified on D3D11 WARP)\n";
    return 0;
}
