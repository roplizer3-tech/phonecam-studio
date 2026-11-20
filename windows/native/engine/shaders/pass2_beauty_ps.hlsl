Texture2D<float4> texIn   : register(t0);
SamplerState      sampLin : register(s0);

cbuffer BeautyCB : register(b0) {
    float texWidth;
    float texHeight;
    float skinSmooth;   // 0.0 .. 1.0
    float denoise;      // 0.0 .. 1.0

    float studioLight;  // 0.0 .. 1.0
    float faceSlim;     // 0.0 .. 1.0
    float eyeEnlarge;   // 0.0 .. 1.0
    float faceDetected; // 0.0 or 1.0

    float faceCenterX;
    float faceCenterY;
    float faceScale;
    float pad0;

    float rightEyeX;
    float rightEyeY;
    float leftEyeX;
    float leftEyeY;
};

float2 WarpEye(float2 uv, float2 eyePos, float strength, float radius, float aspect) {
    float2 d = uv - eyePos;
    float2 dAspect = float2(d.x * aspect, d.y);
    float dist = length(dAspect);
    if (dist < radius && dist > 1e-5) {
        float t = 1.0 - (dist / radius);
        float scale = 1.0 - strength * 0.28 * t * t;
        return eyePos + d * scale;
    }
    return uv;
}

float2 WarpCheekSlim(float2 uv, float2 cheekPos, float2 inwardDir, float strength, float radius, float aspect) {
    float2 d = uv - cheekPos;
    float2 dAspect = float2(d.x * aspect, d.y);
    float dist = length(dAspect);
    if (dist < radius) {
        float t = 1.0 - (dist / radius);
        // Sample further outward along -inwardDir so cheeks pull smoothly inward
        return uv - inwardDir * (strength * 0.032 * t * t);
    }
    return uv;
}

float SkinProbability(float3 rgb) {
    float cb = 128.0 - 37.797 * rgb.r - 74.203 * rgb.g + 112.0 * rgb.b;
    float cr = 128.0 + 112.0 * rgb.r - 93.786 * rgb.g - 18.214 * rgb.b;
    float cbDist = max(0.0, max(77.0 - cb, cb - 127.0)) / 14.0;
    float crDist = max(0.0, max(133.0 - cr, cr - 173.0)) / 14.0;
    float luma = dot(rgb, float3(0.299, 0.587, 0.114));
    float lumaMask = smoothstep(0.08, 0.22, luma) * (1.0 - smoothstep(0.88, 0.99, luma));
    return saturate(1.0 - sqrt(cbDist * cbDist + crDist * crDist)) * lumaMask;
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float aspect = texWidth / max(texHeight, 1.0);
    float2 warpedUv = uv;

    // Real Face Mesh UV Warping driven by YuNet AI Landmarks
    if (faceDetected > 0.5) {
        float safeScale = clamp(faceScale, 0.08, 0.65);
        if (eyeEnlarge > 0.005) {
            float eyeRad = safeScale * 0.22;
            warpedUv = WarpEye(warpedUv, float2(rightEyeX, rightEyeY), eyeEnlarge, eyeRad, aspect);
            warpedUv = WarpEye(warpedUv, float2(leftEyeX, leftEyeY), eyeEnlarge, eyeRad, aspect);
        }
        if (faceSlim > 0.005) {
            float jawRad = safeScale * 0.52;
            float2 rCheek = float2(faceCenterX - safeScale * 0.36, faceCenterY + safeScale * 0.14);
            float2 lCheek = float2(faceCenterX + safeScale * 0.36, faceCenterY + safeScale * 0.14);
            warpedUv = WarpCheekSlim(warpedUv, rCheek, float2( 1.0, -0.20), faceSlim, jawRad, aspect);
            warpedUv = WarpCheekSlim(warpedUv, lCheek, float2(-1.0, -0.20), faceSlim, jawRad, aspect);
        }
    }

    warpedUv = saturate(warpedUv);
    float3 center = texIn.SampleLevel(sampLin, warpedUv, 0).rgb;
    float2 texel = float2(1.0 / max(texWidth, 1.0), 1.0 / max(texHeight, 1.0));

    float skinMask = SkinProbability(center);
    if (faceDetected > 0.5) {
        // Protect eyes from softening
        float dR = length(float2((warpedUv.x - rightEyeX) * aspect, warpedUv.y - rightEyeY));
        float dL = length(float2((warpedUv.x - leftEyeX) * aspect, warpedUv.y - leftEyeY));
        float eyeProtect = smoothstep(faceScale * 0.08, faceScale * 0.18, min(dR, dL));
        skinMask *= eyeProtect;
    }

    float effSmooth = saturate(skinSmooth * skinMask + denoise * 0.45);
    float3 result = center;

    if (effSmooth > 0.005) {
        float sigmaColor = lerp(0.035, 0.14, effSmooth);
        float invTwoSigma2 = 1.0 / (2.0 * sigmaColor * sigmaColor);
        float radius = lerp(1.25, 2.75, effSmooth);

        float3 sum = center;
        float wSum = 1.0;

        static const float2 kOffsets[8] = {
            float2(-1, -1), float2(0, -1), float2(1, -1),
            float2(-1,  0),                float2(1,  0),
            float2(-1,  1), float2(0,  1), float2(1,  1)
        };
        static const float kSpatial[8] = {
            0.70, 0.90, 0.70,
            0.90,       0.90,
            0.70, 0.90, 0.70
        };

        [unroll]
        for (int i = 0; i < 8; ++i) {
            float3 s = texIn.SampleLevel(sampLin, saturate(warpedUv + kOffsets[i] * texel * radius), 0).rgb;
            float3 d = s - center;
            float w = kSpatial[i] * exp(-dot(d, d) * invTwoSigma2);
            sum += s * w;
            wSum += w;
        }
        result = lerp(center, sum / wSum, saturate(effSmooth * 0.95));
    }

    // Studio Key-Light centered softly around face if detected, otherwise frame center
    if (studioLight > 0.005) {
        float2 lightCenter = (faceDetected > 0.5) ? float2(faceCenterX, faceCenterY) : float2(0.5, 0.5);
        float2 p = warpedUv - lightCenter;
        float radial = saturate(1.0 - dot(p, p) * 1.55);
        float lift = studioLight * 0.36 * (0.50 + 0.50 * radial);
        result = saturate(result + lift * result * (1.0 - result) * 2.2 + lift * 0.08 * (1.0 - result));
    }

    return float4(result, 1.0);
}
