Texture2D<float4> texIn   : register(t0);
SamplerState      sampLin : register(s0);

cbuffer BeautyCB : register(b0) {
    float texWidth;
    float texHeight;
    float skinSmooth;   // 0.0 .. 1.0
    float denoise;      // 0.0 .. 1.0
    float studioLight;  // 0.0 .. 1.0
    float pad0;
    float pad1;
    float pad2;
};

float SkinProbability(float3 rgb) {
    // ITU-R BT.601 Cb/Cr skin cluster with soft feathering
    float cb = 128.0 - 37.797 * rgb.r - 74.203 * rgb.g + 112.0 * rgb.b;
    float cr = 128.0 + 112.0 * rgb.r - 93.786 * rgb.g - 18.214 * rgb.b;
    float cbDist = max(0.0, max(77.0 - cb, cb - 127.0)) / 14.0;
    float crDist = max(0.0, max(133.0 - cr, cr - 173.0)) / 14.0;
    float luma = dot(rgb, float3(0.299, 0.587, 0.114));
    float lumaMask = smoothstep(0.08, 0.22, luma) * (1.0 - smoothstep(0.88, 0.99, luma));
    return saturate(1.0 - sqrt(cbDist * cbDist + crDist * crDist)) * lumaMask;
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float3 center = texIn.SampleLevel(sampLin, uv, 0).rgb;
    float2 texel = float2(1.0 / max(texWidth, 1.0), 1.0 / max(texHeight, 1.0));

    float skinMask = SkinProbability(center);
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
            float3 s = texIn.SampleLevel(sampLin, uv + kOffsets[i] * texel * radius, 0).rgb;
            float3 d = s - center;
            float dist2 = dot(d, d);
            float w = kSpatial[i] * exp(-dist2 * invTwoSigma2);
            sum += s * w;
            wSum += w;
        }
        float3 filtered = sum / wSum;
        result = lerp(center, filtered, saturate(effSmooth * 0.95));
    }

    // Studio Key-Light: lifts midtones & shadows gently near center with highlight roll-off
    if (studioLight > 0.005) {
        float2 p = uv - 0.5;
        float radial = saturate(1.0 - dot(p, p) * 1.45);
        float lift = studioLight * 0.35 * (0.55 + 0.45 * radial);
        float3 lifted = result + lift * result * (1.0 - result) * 2.2 + lift * 0.08 * (1.0 - result);
        result = saturate(lifted);
    }

    return float4(result, 1.0);
}
