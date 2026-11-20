#include "ColorConvert.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace phonecam {

namespace {

inline uint8_t ClampByte(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

inline void YuvToBgrBt709(int y, int u, int v, uint8_t& outB, uint8_t& outG, uint8_t& outR) {
    const int c = y - 16;
    const int d = u - 128;
    const int e = v - 128;
    // BT.709 integer fixed-point coefficients (*256)
    const int y298 = 298 * c;
    outR = ClampByte((y298 + 459 * e + 128) >> 8);
    outG = ClampByte((y298 - 55 * d - 136 * e + 128) >> 8);
    outB = ClampByte((y298 + 541 * d + 128) >> 8);
}

} // namespace

void NV12ToBGRA32(
    const uint8_t* nv12,
    uint32_t width,
    uint32_t height,
    uint8_t* dstBgra,
    uint32_t dstStrideBytes) {
    if (!nv12 || !dstBgra || width == 0 || height == 0) return;
    const uint8_t* yPlane = nv12;
    const uint8_t* uvPlane = nv12 + static_cast<size_t>(width) * height;

    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t* yRow = yPlane + static_cast<size_t>(row) * width;
        const uint8_t* uvRow = uvPlane + static_cast<size_t>(row / 2) * width;
        uint8_t* dstRow = dstBgra + static_cast<size_t>(row) * dstStrideBytes;
        for (uint32_t col = 0; col < width; ++col) {
            const uint32_t uvIdx = (col & ~1u);
            uint8_t b = 0, g = 0, r = 0;
            YuvToBgrBt709(yRow[col], uvRow[uvIdx], uvRow[uvIdx + 1], b, g, r);
            dstRow[col * 4 + 0] = b;
            dstRow[col * 4 + 1] = g;
            dstRow[col * 4 + 2] = r;
            dstRow[col * 4 + 3] = 255;
        }
    }
}

void NV12ToBGR24BottomUp(
    const uint8_t* nv12,
    uint32_t width,
    uint32_t height,
    uint8_t* dstBgr,
    uint32_t dstStrideBytes) {
    if (!nv12 || !dstBgr || width == 0 || height == 0) return;
    const uint8_t* yPlane = nv12;
    const uint8_t* uvPlane = nv12 + static_cast<size_t>(width) * height;

    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t* yRow = yPlane + static_cast<size_t>(row) * width;
        const uint8_t* uvRow = uvPlane + static_cast<size_t>(row / 2) * width;
        uint8_t* dstRow = dstBgr + static_cast<size_t>(height - 1u - row) * dstStrideBytes;
        for (uint32_t col = 0; col < width; ++col) {
            const uint32_t uvIdx = (col & ~1u);
            uint8_t b = 0, g = 0, r = 0;
            YuvToBgrBt709(yRow[col], uvRow[uvIdx], uvRow[uvIdx + 1], b, g, r);
            dstRow[col * 3 + 0] = b;
            dstRow[col * 3 + 1] = g;
            dstRow[col * 3 + 2] = r;
        }
    }
}

void NV12ToYUY2(
    const uint8_t* nv12,
    uint32_t width,
    uint32_t height,
    uint8_t* dstYuy2,
    uint32_t dstStrideBytes) {
    if (!nv12 || !dstYuy2 || width < 2 || height == 0) return;
    const uint8_t* yPlane = nv12;
    const uint8_t* uvPlane = nv12 + static_cast<size_t>(width) * height;

    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t* yRow = yPlane + static_cast<size_t>(row) * width;
        const uint8_t* uvRow = uvPlane + static_cast<size_t>(row / 2) * width;
        uint8_t* dstRow = dstYuy2 + static_cast<size_t>(row) * dstStrideBytes;
        for (uint32_t col = 0; col + 1 < width; col += 2) {
            dstRow[col * 2 + 0] = yRow[col];
            dstRow[col * 2 + 1] = uvRow[col];
            dstRow[col * 2 + 2] = yRow[col + 1];
            dstRow[col * 2 + 3] = uvRow[col + 1];
        }
    }
}

void ResizeNV12(
    const uint8_t* srcNv12,
    uint32_t srcW,
    uint32_t srcH,
    uint8_t* dstNv12,
    uint32_t dstW,
    uint32_t dstH) {
    if (!srcNv12 || !dstNv12 || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0) return;
    if (srcW == dstW && srcH == dstH) {
        std::memcpy(dstNv12, srcNv12, static_cast<size_t>(srcW) * srcH * 3 / 2);
        return;
    }

    const uint8_t* srcY = srcNv12;
    const uint8_t* srcUV = srcNv12 + static_cast<size_t>(srcW) * srcH;
    uint8_t* dstY = dstNv12;
    uint8_t* dstUV = dstNv12 + static_cast<size_t>(dstW) * dstH;

    for (uint32_t y = 0; y < dstH; ++y) {
        const uint32_t sy = (y * srcH) / dstH;
        const uint8_t* sRow = srcY + static_cast<size_t>(sy) * srcW;
        uint8_t* dRow = dstY + static_cast<size_t>(y) * dstW;
        for (uint32_t x = 0; x < dstW; ++x) {
            const uint32_t sx = (x * srcW) / dstW;
            dRow[x] = sRow[sx];
        }
    }

    const uint32_t srcUvH = srcH / 2;
    const uint32_t dstUvH = dstH / 2;
    const uint32_t srcUvW = srcW / 2;
    const uint32_t dstUvW = dstW / 2;
    for (uint32_t y = 0; y < dstUvH; ++y) {
        const uint32_t sy = (y * srcUvH) / dstUvH;
        const uint8_t* sRow = srcUV + static_cast<size_t>(sy) * srcW;
        uint8_t* dRow = dstUV + static_cast<size_t>(y) * dstW;
        for (uint32_t x = 0; x < dstUvW; ++x) {
            const uint32_t sx = ((x * srcUvW) / dstUvW) * 2u;
            dRow[x * 2 + 0] = sRow[sx + 0];
            dRow[x * 2 + 1] = sRow[sx + 1];
        }
    }
}

void RenderStandbyNV12(
    std::vector<uint8_t>& outNv12,
    uint32_t width,
    uint32_t height,
    uint64_t frameTick) {
    const size_t ySize = static_cast<size_t>(width) * height;
    const size_t uvSize = ySize / 2;
    outNv12.resize(ySize + uvSize);

    uint8_t* yPlane = outNv12.data();
    uint8_t* uvPlane = outNv12.data() + ySize;

    // Dark studio slate background (Y=24, U=134, V=124 -> deep slate blue)
    std::memset(yPlane, 24, ySize);
    for (size_t i = 0; i + 1 < uvSize; i += 2) {
        uvPlane[i] = 136;
        uvPlane[i + 1] = 122;
    }

    const uint32_t cx = width / 2;
    const uint32_t cy = height / 2;
    const uint32_t outerR = std::min(width, height) / 7;
    const uint32_t innerR = outerR / 2;
    const uint32_t pulse = static_cast<uint32_t>((frameTick * 6) % width);

    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* yRow = yPlane + static_cast<size_t>(y) * width;
        const int dy = static_cast<int>(y) - static_cast<int>(cy);
        for (uint32_t x = 0; x < width; ++x) {
            const int dx = static_cast<int>(x) - static_cast<int>(cx);
            const uint32_t dist2 = static_cast<uint32_t>(dx * dx + dy * dy);
            if (dist2 <= innerR * innerR) {
                yRow[x] = 165; // Bright lens core
            } else if (dist2 <= outerR * outerR && dist2 >= (outerR - 6) * (outerR - 6)) {
                yRow[x] = 120; // Lens ring
            } else if (y >= height - 6) {
                const int dPulse = std::abs(static_cast<int>(x) - static_cast<int>(pulse));
                if (dPulse < 90) {
                    yRow[x] = static_cast<uint8_t>(160 - dPulse);
                }
            }
        }
    }
}

} // namespace phonecam
