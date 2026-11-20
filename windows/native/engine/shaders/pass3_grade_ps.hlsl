Texture2D<float4> texProc : register(t0);
Texture2D<float4> texOrig : register(t1);
SamplerState      sampLin : register(s0);

cbuffer GradeCB : register(b0) {
    float texWidth;
    float texHeight;
    float exposure;      // -2.0 .. +2.0 EV
    float brightness;    // -0.5 .. +0.5
    float contrast;      //  0.5 ..  1.8 (1.0 neutral)
    float highlights;    // -1.0 .. +1.0
    float shadows;       // -1.0 .. +1.0
    float gammaVal;      //  0.6 ..  1.6 (1.0 neutral)
    float temperature;   // -1.0 .. +1.0
    float tint;          // -1.0 .. +1.0
    float saturation;    //  0.0 ..  2.0 (1.0 neutral)
    float vibrance;      // -1.0 .. +1.0 (0.0 neutral)
    float sharpen;       //  0.0 ..  1.0
    float softGlow;      //  0.0 ..  1.0
    float vignette;      //  0.0 ..  1.0
    float awbStrength;   //  0.0 ..  1.0
    float awbGainR;
    float awbGainG;
    float awbGainB;
    float mirrorCheck;   //  0.0 or 1.0
    float isPreview;     //  0.0 (virtual cam out) or 1.0 (local UI preview)
    float splitPos;      // < 0 disabled, 0..1 split position
    float bypassAll;     // 1.0 bypasses color/beauty grading
    float previewFlipH;  // 1.0 flips preview horizontally (selfie view only)
};

uint GlyphRow(uint chIdx, uint row) {
    // 5-bit rows (bit 4 = leftmost pixel) for: '1','2','3',' ','A','B','C','-','>'
    static const uint kFont[9][7] = {
        { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, // '1'
        { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F }, // '2'
        { 0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E }, // '3'
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // ' '
        { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // 'A'
        { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, // 'B'
        { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, // 'C'
        { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 }, // '-'
        { 0x08, 0x04, 0x02, 0x1F, 0x02, 0x04, 0x08 }  // '>'
    };
    return kFont[min(chIdx, 8u)][min(row, 6u)];
}

float3 OverlayMirrorCheck(float3 col, float2 uv) {
    // Banner centered near top: "123 ABC ->" (10 chars * 6 px = 60 x 11 grid)
    float2 boxMin = float2(0.30, 0.04);
    float2 boxMax = float2(0.70, 0.14);
    if (uv.x < boxMin.x || uv.x > boxMax.x || uv.y < boxMin.y || uv.y > boxMax.y) {
        return col;
    }
    float2 localUv = (uv - boxMin) / (boxMax - boxMin);
    int gx = (int)floor(localUv.x * 62.0) - 2; // 10 chars * 6 cols = 60 + margin
    int gy = (int)floor(localUv.y * 11.0) - 2; // 7 rows + margin

    static const uint kMsg[10] = { 0, 1, 2, 3, 4, 5, 6, 3, 7, 8 }; // "123 ABC ->"
    bool lit = false;
    if (gx >= 0 && gx < 60 && gy >= 0 && gy < 7) {
        uint charPos = (uint)(gx / 6);
        uint colInChar = (uint)(gx % 6);
        if (colInChar < 5u) {
            uint rowMask = GlyphRow(kMsg[charPos], (uint)gy);
            lit = ((rowMask >> (4u - colInChar)) & 1u) != 0u;
        }
    }
    float3 bg = lerp(col, float3(0.05, 0.10, 0.22), 0.85);
    return lit ? float3(0.22, 0.96, 0.65) : bg;
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float2 sampleUv = (isPreview > 0.5 && previewFlipH > 0.5) ? float2(1.0 - uv.x, uv.y) : uv;
    float3 orig = texOrig.SampleLevel(sampLin, sampleUv, 0).rgb;
    float3 col  = texProc.SampleLevel(sampLin, sampleUv, 0).rgb;

    if (bypassAll > 0.5 || (isPreview > 0.5 && splitPos >= 0.0 && uv.x < splitPos)) {
        col = orig;
    } else {
        float2 texel = float2(1.0 / max(texWidth, 1.0), 1.0 / max(texHeight, 1.0));

        // 1. Eye / Detail Clarity (clamped unsharp mask) + Soft Glow
        if (sharpen > 0.005 || softGlow > 0.005) {
            float3 blur = (
                texProc.SampleLevel(sampLin, sampleUv + float2(-texel.x, 0), 0).rgb +
                texProc.SampleLevel(sampLin, sampleUv + float2( texel.x, 0), 0).rgb +
                texProc.SampleLevel(sampLin, sampleUv + float2(0, -texel.y), 0).rgb +
                texProc.SampleLevel(sampLin, sampleUv + float2(0,  texel.y), 0).rgb
            ) * 0.25;
            if (sharpen > 0.005) {
                float3 detail = clamp(col - blur, -0.18, 0.18);
                col = saturate(col + detail * (sharpen * 1.45));
            }
            if (softGlow > 0.005) {
                float3 glowBlur = (
                    texProc.SampleLevel(sampLin, sampleUv + float2(-3.0 * texel.x, -3.0 * texel.y), 0).rgb +
                    texProc.SampleLevel(sampLin, sampleUv + float2( 3.0 * texel.x, -3.0 * texel.y), 0).rgb +
                    texProc.SampleLevel(sampLin, sampleUv + float2(-3.0 * texel.x,  3.0 * texel.y), 0).rgb +
                    texProc.SampleLevel(sampLin, sampleUv + float2( 3.0 * texel.x,  3.0 * texel.y), 0).rgb
                ) * 0.25;
                col = saturate(lerp(col, max(col, glowBlur * 1.06), softGlow * 0.45));
            }
        }

        // 2. White Balance (Auto WB + Manual Temperature & Tint)
        float3 wb = lerp(float3(1.0, 1.0, 1.0), float3(awbGainR, awbGainG, awbGainB), awbStrength);
        wb.r *= (1.0 + temperature * 0.18);
        wb.b *= (1.0 - temperature * 0.18);
        wb.g *= (1.0 - tint * 0.12);
        wb.r *= (1.0 + tint * 0.06);
        wb.b *= (1.0 + tint * 0.06);
        col = saturate(col * wb);

        // 3. Exposure & Brightness
        col = saturate(col * exp2(exposure) + brightness);

        // 4. Shadows & Highlights
        float lum = dot(col, float3(0.2126, 0.7152, 0.0722));
        float shadowMask = (1.0 - smoothstep(0.0, 0.55, lum));
        float highMask   = smoothstep(0.45, 1.0, lum);
        col = saturate(col + shadows * 0.28 * shadowMask * (1.0 - col) + highlights * 0.25 * highMask * col);

        // 5. Contrast & Gamma
        col = saturate((col - 0.5) * contrast + 0.5);
        col = pow(max(col, 0.0001), 1.0 / max(gammaVal, 0.2));

        // 6. Saturation & Skin-Safe Vibrance
        float lum2 = dot(col, float3(0.2126, 0.7152, 0.0722));
        float maxC = max(col.r, max(col.g, col.b));
        float minC = min(col.r, min(col.g, col.b));
        float satCurr = maxC - minC;
        float skinProt = saturate(1.0 - smoothstep(0.0, 0.35, abs(col.r - col.g - 0.12)));
        float vibScale = 1.0 + vibrance * (1.0 - satCurr) * (1.0 - 0.45 * skinProt);
        float finalSat = max(0.0, saturation * vibScale);
        col = saturate(lerp(float3(lum2, lum2, lum2), col, finalSat));

        // 7. Vignette
        if (vignette > 0.005) {
            float2 d = (sampleUv - 0.5) * float2(1.15, 1.0);
            float v = 1.0 - vignette * 0.75 * smoothstep(0.22, 0.78, dot(d, d));
            col *= saturate(v);
        }
    }

    // Split compare divider line (Preview only)
    if (isPreview > 0.5 && splitPos >= 0.0 && abs(uv.x - splitPos) < (1.5 / max(texWidth, 1.0))) {
        col = float3(0.25, 0.75, 1.0);
    }

    // Mirror Check asymmetric text overlay ("123 ABC ->")
    if (mirrorCheck > 0.5) {
        col = OverlayMirrorCheck(col, sampleUv);
    }

    return float4(saturate(col), 1.0);
}
