Texture2D<float4> texRGBA : register(t0);
SamplerState      sampLin : register(s0);

// BT.709 RGB -> limited-range Y [16..235]
float PS_ToY(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float3 rgb = texRGBA.SampleLevel(sampLin, uv, 0).rgb;
    float y = (16.0 / 255.0) + dot(rgb, float3(0.182586, 0.614231, 0.062007));
    return saturate(y);
}

// BT.709 RGB -> limited-range interleaved CbCr [16..240]
float2 PS_ToUV(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float3 rgb = texRGBA.SampleLevel(sampLin, uv, 0).rgb;
    float cb = (128.0 / 255.0) + dot(rgb, float3(-0.100644, -0.338572, 0.439216));
    float cr = (128.0 / 255.0) + dot(rgb, float3(0.439216, -0.398942, -0.040274));
    return saturate(float2(cb, cr));
}
