#include "../engine/GpuProcessor.h"
#include "../common/ColorConvert.h"

#include <cassert>
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
            if (top && left) { r = 230; g = 30;  b = 30;  }       // Top-Left: Red
            else if (top && !left) { r = 30;  g = 40;  b = 235; } // Top-Right: Blue
            else if (!top && left) { r = 30;  g = 210; b = 40;  } // Bottom-Left: Green
            else { r = 240; g = 240; b = 240; }                   // Bottom-Right: White

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
    assert(gpu.ProcessFrame(in, p, true, out));
    assert(out.width == W && out.height == H);
    assert(out.nv12.size() == W * H * 3u / 2u);
    assert(out.previewBgra.size() == W * H * 4u);

    uint8_t r, g, b;
    SamplePixel(out.previewBgra, W, W / 4, H / 4, r, g, b); // Top-Left -> Red
    assert(r > 200 && g < 60 && b < 60);
    SamplePixel(out.previewBgra, W, (W * 3) / 4, H / 4, r, g, b); // Top-Right -> Blue
    assert(b > 200 && r < 60);

    // Check NV12 roundtrip preserves Red top-left & Blue top-right
    std::vector<uint8_t> roundtripBgra(W * H * 4u);
    NV12ToBGRA32(out.nv12.data(), W, H, roundtripBgra.data(), W * 4u);
    SamplePixel(roundtripBgra, W, W / 4, H / 4, r, g, b);
    assert(r > 190 && b < 70);
    SamplePixel(roundtripBgra, W, (W * 3) / 4, H / 4, r, g, b);
    assert(b > 190 && r < 70);

    // 2. Output Horizontal Mirror (FlipH) swaps Top-Left Red <-> Top-Right Blue
    p = NeutralParams();
    p.flipH = 1;
    assert(gpu.ProcessFrame(in, p, true, out));
    SamplePixel(out.previewBgra, W, W / 4, H / 4, r, g, b);
    assert(b > 200 && r < 60); // Now Blue on Top-Left!
    SamplePixel(out.previewBgra, W, (W * 3) / 4, H / 4, r, g, b);
    assert(r > 200 && b < 60); // Now Red on Top-Right!

    // 3. Output Vertical Flip (FlipV) swaps Top-Left Red <-> Bottom-Left Green
    p = NeutralParams();
    p.flipV = 1;
    assert(gpu.ProcessFrame(in, p, true, out));
    SamplePixel(out.previewBgra, W, W / 4, H / 4, r, g, b);
    assert(g > 180 && r < 60 && b < 60); // Now Green on Top-Left!

    // 4. Rotation 90 deg CW: Bottom-Left Green moves to Top-Left!
    p = NeutralParams();
    p.manualRotationDeg = 90;
    assert(gpu.ProcessFrame(in, p, true, out));
    SamplePixel(out.previewBgra, W, W / 4, H / 4, r, g, b);
    assert(g > 180 && r < 60);

    // 5. Beauty + Color Grading (B&W Saturation = 0 + SkinSmooth + StudioLight)
    p = NeutralParams();
    p.skinSmooth = 0.8f;
    p.studioLight = 0.5f;
    p.saturation = 0.0f;
    assert(gpu.ProcessFrame(in, p, true, out));
    SamplePixel(out.previewBgra, W, W / 4, H / 4, r, g, b);
    assert(std::abs(static_cast<int>(r) - static_cast<int>(b)) <= 6);
    assert(std::abs(static_cast<int>(r) - static_cast<int>(g)) <= 6);

    // 6. Mirror Check overlay renders crisp green text pixels in top-center banner
    p = NeutralParams();
    p.mirrorCheckOverlay = 1;
    assert(gpu.ProcessFrame(in, p, true, out));
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
    assert(foundBrightOverlay);

    std::cout << "[PASS] GpuPipelineShaderTests (all 4 HLSL passes verified on D3D11 WARP)\n";
    return 0;
}
