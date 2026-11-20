#pragma once

#include <cstdint>
#include <vector>

namespace phonecam {

void NV12ToBGRA32(
    const uint8_t* nv12,
    uint32_t width,
    uint32_t height,
    uint8_t* dstBgra,
    uint32_t dstStrideBytes);

void NV12ToBGR24BottomUp(
    const uint8_t* nv12,
    uint32_t width,
    uint32_t height,
    uint8_t* dstBgr,
    uint32_t dstStrideBytes);

void NV12ToYUY2(
    const uint8_t* nv12,
    uint32_t width,
    uint32_t height,
    uint8_t* dstYuy2,
    uint32_t dstStrideBytes);

void ResizeNV12(
    const uint8_t* srcNv12,
    uint32_t srcW,
    uint32_t srcH,
    uint8_t* dstNv12,
    uint32_t dstW,
    uint32_t dstH);

void RenderStandbyNV12(
    std::vector<uint8_t>& outNv12,
    uint32_t width,
    uint32_t height,
    uint64_t frameTick);

} // namespace phonecam
