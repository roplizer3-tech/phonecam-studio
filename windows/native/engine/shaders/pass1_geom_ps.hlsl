Texture2D<float>  texY    : register(t0);
Texture2D<float2> texUV   : register(t1);
Texture2D<float4> texBGRA : register(t2);
SamplerState      sampLin : register(s0);

cbuffer GeomCB : register(b0) {
    float srcWidth;
    float srcHeight;
    float outWidth;
    float outHeight;
    int   inputMode;    // 0 = NV12 (texY + texUV), 1 = BGRA (texBGRA)
    int   rotationDeg;  // 0, 90, 180, 270 clockwise
    int   flipH;        // 0 or 1
    int   flipV;        // 0 or 1
    int   framingMode;  // 0 = Fill/Crop, 1 = Fit+Blur, 2 = Fit+Black
    float zoom;         // 1.0 .. 3.0
    float panX;         // -0.5 .. 0.5
    float panY;         // -0.5 .. 0.5
};

float3 SampleSource(float2 uv) {
    if (inputMode == 1) {
        return texBGRA.SampleLevel(sampLin, uv, 0).rgb;
    }
    float y  = texY.SampleLevel(sampLin, uv, 0);
    float2 c = texUV.SampleLevel(sampLin, uv, 0);
    // BT.709 limited range (Y: 16..235, CbCr: 16..240)
    float yy = (y - (16.0 / 255.0)) * (255.0 / 219.0);
    float cb = (c.x - (128.0 / 255.0)) * (255.0 / 224.0);
    float cr = (c.y - (128.0 / 255.0)) * (255.0 / 224.0);
    float r = yy + 1.5748 * cr;
    float g = yy - 0.1873 * cb - 0.4681 * cr;
    float b = yy + 1.8556 * cb;
    return saturate(float3(r, g, b));
}

float2 RotateInverse(float2 uv, int deg) {
    int r = ((deg / 90) % 4 + 4) % 4;
    if (r == 1) return float2(uv.y, 1.0 - uv.x);
    if (r == 2) return float2(1.0 - uv.x, 1.0 - uv.y);
    if (r == 3) return float2(1.0 - uv.y, uv.x);
    return uv;
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float2 p = uv - 0.5;
    if (flipH != 0) p.x = -p.x;
    if (flipV != 0) p.y = -p.y;

    float safeZoom = max(zoom, 1.0);
    p = (p / safeZoom) + float2(panX, panY);

    int rotSteps = ((rotationDeg / 90) % 4 + 4) % 4;
    bool swapped = (rotSteps == 1 || rotSteps == 3);
    float orientedW = swapped ? srcHeight : srcWidth;
    float orientedH = swapped ? srcWidth : srcHeight;
    float orientedAspect = orientedW / max(orientedH, 1.0);
    float outAspect = outWidth / max(outHeight, 1.0);
    float ratio = orientedAspect / max(outAspect, 0.001);

    float2 pMapped = p;
    if (framingMode == 0) {
        // Smart Fill (Crop to fill output)
        if (ratio > 1.0) {
            pMapped.x /= ratio;
        } else {
            pMapped.y *= ratio;
        }
    } else {
        // Fit entire phone image inside 16:9 frame
        if (ratio > 1.0) {
            pMapped.y *= ratio;
        } else {
            pMapped.x /= ratio;
        }
    }

    float2 orientedUV = pMapped + 0.5;
    if (framingMode != 0 && (orientedUV.x < 0.0 || orientedUV.x > 1.0 || orientedUV.y < 0.0 || orientedUV.y > 1.0)) {
        if (framingMode == 2) {
            return float4(0.02, 0.02, 0.03, 1.0);
        }
        // Framing Mode 1: Blurred + darkened background fill
        float2 bgP = p;
        if (ratio > 1.0) bgP.x /= ratio; else bgP.y *= ratio;
        float2 bgUV = saturate(RotateInverse(bgP + 0.5, rotationDeg));
        float3 acc = float3(0, 0, 0);
        float2 stepUv = float2(12.0 / max(srcWidth, 1.0), 12.0 / max(srcHeight, 1.0));
        [unroll]
        for (int dy = -1; dy <= 1; ++dy) {
            [unroll]
            for (int dx = -1; dx <= 1; ++dx) {
                acc += SampleSource(saturate(bgUV + float2(dx, dy) * stepUv));
            }
        }
        return float4((acc / 9.0) * 0.38, 1.0);
    }

    float2 srcUV = RotateInverse(saturate(orientedUV), rotationDeg);
    return float4(SampleSource(srcUV), 1.0);
}
