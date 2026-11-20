Texture2D<float4> texProc     : register(t0);
Texture2D<float4> texOrig     : register(t1);
Texture2D<float>  texMask     : register(t2);
Texture2D<float4> texCustomBg : register(t3);
SamplerState      sampLin     : register(s0);

cbuffer GradeCB : register(b0) {
    float texWidth;
    float texHeight;
    float exposure;
    float brightness;

    float contrast;
    float highlights;
    float shadows;
    float gammaVal;

    float temperature;
    float tint;
    float saturation;
    float vibrance;

    float sharpen;
    float softGlow;
    float vignette;
    float awbStrength;

    float awbGainR;
    float awbGainG;
    float awbGainB;
    float mirrorCheck;

    float isPreview;
    float splitPos;
    float bypassAll;
    float previewFlipH;

    // v2.0 AI Background, AR Face Filters, Stylized Shaders
    int   bgMode;           // 0=None, 1=DSLR Bokeh, 2=Studio Office, 3=Bokeh Orbs, 4=Cyber Studio, 5=Custom Image
    int   arFilterMode;     // 0=None, 1=Aviator, 2=Round Glasses, 3=Cyber Visor, 4=Halo Crown, 5=Sparkle Aura
    int   styleFilterMode;  // 0=None, 1=Anime/Cartoon, 2=Pencil Sketch, 3=Retro VHS, 4=Cyber Neon, 5=Cinema Halation
    float bgBlurStrength;

    float eyeBrighten;
    float teethWhiten;
    float styleIntensity;
    float timeSec;

    float hasCustomBg;
    float hasAiMask;
    float faceDetected;
    float faceConf;

    float rightEyeX;
    float rightEyeY;
    float leftEyeX;
    float leftEyeY;

    float noseX;
    float noseY;
    float mouthCenterX;
    float mouthCenterY;

    float faceBoxX;
    float faceBoxY;
    float faceBoxW;
    float faceBoxH;
};

float Hash21(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float Luma(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float3 SampleBlurredBg(float2 uv, float2 texel, float blurRadius) {
    static const float2 kDisc[12] = {
        float2( 0.00,  0.30), float2( 0.28,  0.14), float2( 0.29, -0.15),
        float2( 0.00, -0.32), float2(-0.29, -0.15), float2(-0.28,  0.14),
        float2( 0.48,  0.55), float2( 0.75,  0.00), float2( 0.45, -0.58),
        float2(-0.48, -0.55), float2(-0.75,  0.00), float2(-0.45,  0.58)
    };
    float3 acc = float3(0, 0, 0);
    [unroll]
    for (int i = 0; i < 12; ++i) {
        acc += texProc.SampleLevel(sampLin, saturate(uv + kDisc[i] * texel * blurRadius), 0).rgb;
    }
    return acc / 12.0;
}

float3 ProceduralVirtualBg(int mode, float2 uv, float aspect) {
    if (mode == 2) {
        // Modern Warm Studio Office Backdrop
        float3 wallTop = float3(0.16, 0.19, 0.26);
        float3 wallBot = float3(0.08, 0.10, 0.15);
        float3 col = lerp(wallTop, wallBot, smoothstep(0.0, 1.0, uv.y));
        // Soft architectural light panel
        float panel = smoothstep(0.32, 0.29, abs(uv.x - 0.75)) * smoothstep(0.38, 0.34, abs(uv.y - 0.40));
        col += panel * float3(0.22, 0.25, 0.32);
        // Warm studio accent lamp glow
        float lamp = exp(-dot((uv - float2(0.18, 0.32)) * float2(aspect, 1.0), (uv - float2(0.18, 0.32)) * float2(aspect, 1.0)) * 12.0);
        col += lamp * float3(0.55, 0.32, 0.14);
        return saturate(col);
    }
    if (mode == 3) {
        // Soft Dreamy Bokeh Orbs
        float3 col = lerp(float3(0.06, 0.08, 0.16), float3(0.14, 0.09, 0.20), uv.y);
        static const float3 kOrbs[6] = {
            float3(0.15, 0.25, 0.12), float3(0.82, 0.22, 0.14),
            float3(0.30, 0.72, 0.16), float3(0.74, 0.78, 0.11),
            float3(0.55, 0.18, 0.09), float3(0.12, 0.65, 0.10)
        };
        [unroll]
        for (int i = 0; i < 6; ++i) {
            float2 c = kOrbs[i].xy + 0.025 * float2(sin(timeSec * 0.6 + i), cos(timeSec * 0.5 + i));
            float d = length((uv - c) * float2(aspect, 1.0));
            float orb = smoothstep(kOrbs[i].z, kOrbs[i].z * 0.65, d);
            float3 tintC = (i % 2 == 0) ? float3(0.45, 0.65, 1.0) : float3(1.0, 0.62, 0.38);
            col += orb * tintC * 0.32;
        }
        return saturate(col);
    }
    // mode == 4: Cyber Neon Studio
    float3 baseC = lerp(float3(0.03, 0.04, 0.10), float3(0.11, 0.03, 0.16), uv.y);
    float2 gridUv = uv * float2(18.0, 10.0);
    float lineX = smoothstep(0.06, 0.0, abs(frac(gridUv.x) - 0.5));
    float lineY = smoothstep(0.06, 0.0, abs(frac(gridUv.y) - 0.5));
    float grid = max(lineX, lineY) * smoothstep(0.35, 0.95, uv.y);
    float horizonGlow = exp(-abs(uv.y - 0.52) * 14.0);
    return saturate(baseC + grid * float3(0.0, 0.65, 0.95) * 0.55 + horizonGlow * float3(0.75, 0.10, 0.65) * 0.45);
}

float SobelEdge(float2 uv, float2 texel) {
    float tl = Luma(texProc.SampleLevel(sampLin, uv + float2(-texel.x, -texel.y), 0).rgb);
    float t  = Luma(texProc.SampleLevel(sampLin, uv + float2(0, -texel.y), 0).rgb);
    float tr = Luma(texProc.SampleLevel(sampLin, uv + float2( texel.x, -texel.y), 0).rgb);
    float l  = Luma(texProc.SampleLevel(sampLin, uv + float2(-texel.x, 0), 0).rgb);
    float r  = Luma(texProc.SampleLevel(sampLin, uv + float2( texel.x, 0), 0).rgb);
    float bl = Luma(texProc.SampleLevel(sampLin, uv + float2(-texel.x,  texel.y), 0).rgb);
    float b  = Luma(texProc.SampleLevel(sampLin, uv + float2(0,  texel.y), 0).rgb);
    float br = Luma(texProc.SampleLevel(sampLin, uv + float2( texel.x,  texel.y), 0).rgb);
    float gx = -tl - 2.0 * l - bl + tr + 2.0 * r + br;
    float gy = -tl - 2.0 * t - tr + bl + 2.0 * b + br;
    return sqrt(gx * gx + gy * gy);
}

float3 ApplyStylizedShader(int mode, float3 col, float2 uv, float2 texel, float intensity) {
    if (mode <= 0 || intensity <= 0.005) return col;
    float3 styled = col;
    if (mode == 1) {
        // Real Anime / Cartoon Cel-Shading + Sobel Ink Contour
        float3 smoothC = SampleBlurredBg(uv, texel, 2.2);
        float levels = 6.0;
        float3 q = floor(smoothC * levels + 0.5) / levels;
        float lum = Luma(q);
        q = saturate(lerp(float3(lum, lum, lum), q, 1.32));
        float edge = smoothstep(0.18, 0.45, SobelEdge(uv, texel * 1.25));
        styled = q * (1.0 - edge * 0.88);
    } else if (mode == 2) {
        // Real Graphite Pencil Sketch on Paper
        float edge = SobelEdge(uv, texel * 1.15);
        float lum = Luma(col);
        float paper = 0.90 + 0.06 * Hash21(floor(uv * float2(texWidth, texHeight) * 0.5));
        float shade = saturate(lum * 0.45 + 0.55 - edge * 1.35);
        styled = float3(0.96, 0.94, 0.89) * paper * shade;
    } else if (mode == 3) {
        // Real Retro VHS Camcorder (Tape Wave + Chromatic Split + Scanlines + Noise)
        float wave = sin(uv.y * 28.0 + timeSec * 5.0) * 0.0025;
        float2 vhsUv = float2(uv.x + wave, uv.y);
        float shift = 0.0045 * intensity;
        float rC = texProc.SampleLevel(sampLin, saturate(vhsUv + float2( shift, 0)), 0).r;
        float gC = texProc.SampleLevel(sampLin, saturate(vhsUv), 0).g;
        float bC = texProc.SampleLevel(sampLin, saturate(vhsUv - float2( shift, 0)), 0).b;
        float scan = 0.88 + 0.12 * sin(uv.y * texHeight * 1.5);
        float grain = (Hash21(uv * 512.0 + frac(timeSec) * 37.0) - 0.5) * 0.10;
        styled = saturate(float3(rC, gC, bC) * scan + grain);
    } else if (mode == 4) {
        // Real Cyberpunk Neon Edges
        float edge = smoothstep(0.10, 0.42, SobelEdge(uv, texel * 1.3));
        float3 neonCol = lerp(float3(0.0, 0.95, 1.0), float3(1.0, 0.15, 0.80), uv.x);
        styled = saturate(col * 0.24 + neonCol * edge * 1.65);
    } else if (mode == 5) {
        // Real Cinematic Halation + Film Grain
        float3 blurBright = max(SampleBlurredBg(uv, texel, 6.0) - 0.65, 0.0);
        float3 halation = blurBright * float3(1.0, 0.35, 0.12) * 1.35;
        float grain = (Hash21(uv * 720.0 + frac(timeSec * 24.0)) - 0.5) * 0.045;
        styled = saturate(col + halation + grain);
    }
    return lerp(col, styled, saturate(intensity));
}

float3 ApplyArFaceOverlay(int mode, float3 col, float2 uv, float aspect) {
    if (mode <= 0 || faceDetected < 0.5) return col;

    float2 rEye = float2(rightEyeX, rightEyeY);
    float2 lEye = float2(leftEyeX, leftEyeY);
    float2 eyeMid = 0.5 * (rEye + lEye);
    float2 eyeVec = float2((lEye.x - rEye.x) * aspect, lEye.y - rEye.y);
    float iod = max(length(eyeVec), 0.04);
    float angle = atan2(eyeVec.y, eyeVec.x);

    float cs = cos(-angle);
    float sn = sin(-angle);
    float2 d = float2((uv.x - eyeMid.x) * aspect, uv.y - eyeMid.y);
    float2 f = float2(d.x * cs - d.y * sn, d.x * sn + d.y * cs) / iod;
    // In face-local units: rightEye is (-0.5, 0), leftEye is (+0.5, 0), forehead is y < 0

    if (mode == 1) {
        // Aviator Sunglasses
        float2 rD = f - float2(-0.52, 0.04);
        float2 lD = f - float2( 0.52, 0.04);
        float rLens = length(float2(rD.x / 0.44, (rD.y - rD.x * 0.12) / 0.36));
        float lLens = length(float2(lD.x / 0.44, (lD.y + lD.x * 0.12) / 0.36));
        float lensDist = min(rLens, lLens);
        float topBar = max(abs(f.x) / 0.96, abs(f.y + 0.24) / 0.032);
        float bridge = max(abs(f.x) / 0.16, abs(f.y + 0.04) / 0.030);
        if (lensDist < 1.0) {
            float border = smoothstep(0.88, 0.96, lensDist);
            float sheen = smoothstep(0.18, 0.0, abs((f.x - f.y * 0.8) + 0.25)) * 0.22;
            float3 tintLens = col * float3(0.16, 0.20, 0.24) + float3(0.04, 0.08, 0.12) + sheen;
            return lerp(tintLens, float3(0.82, 0.68, 0.35), border);
        }
        if (topBar < 1.0 || bridge < 1.0) {
            return float3(0.82, 0.68, 0.35);
        }
    } else if (mode == 2) {
        // Round Studio Glasses
        float rD = length(f - float2(-0.52, 0.0));
        float lD = length(f - float2( 0.52, 0.0));
        float ring = min(abs(rD - 0.36), abs(lD - 0.36));
        float bridge = max(abs(f.x) / 0.17, abs(f.y) / 0.025);
        if (ring < 0.038 || bridge < 1.0) {
            return float3(0.18, 0.20, 0.25);
        }
        if (min(rD, lD) < 0.35) {
            float glint = smoothstep(0.12, 0.0, abs(f.x - f.y * 0.9 - 0.35)) * 0.14;
            return saturate(col * 1.04 + glint);
        }
    } else if (mode == 3) {
        // Cyberpunk Neon Visor
        float boxDist = max(abs(f.x) / 1.02, abs(f.y) / 0.24);
        if (boxDist < 1.0) {
            float edge = smoothstep(0.82, 0.98, boxDist);
            float scan = 0.5 + 0.5 * sin(f.x * 16.0 - timeSec * 6.0);
            float3 neon = lerp(float3(0.0, 0.95, 1.0), float3(1.0, 0.10, 0.85), 0.5 + 0.5 * f.x);
            float3 glass = col * float3(0.25, 0.55, 0.75) + neon * (0.22 + 0.12 * scan);
            return lerp(glass, neon, edge);
        }
    } else if (mode == 4) {
        // Golden Halo Crown floating above forehead
        float2 hP = f - float2(0.0, -1.42);
        float halo = abs(length(float2(hP.x / 0.78, hP.y / 0.20)) - 1.0);
        float glow = exp(-halo * 9.0);
        return saturate(col + float3(1.0, 0.82, 0.25) * glow * 1.15);
    } else if (mode == 5) {
        // Animated Orbiting Sparkle Aura
        float3 acc = col;
        [unroll]
        for (int i = 0; i < 6; ++i) {
            float ang = timeSec * 1.3 + i * 1.0472;
            float2 starPos = float2(cos(ang) * 1.35, -0.25 + sin(ang * 2.0) * 0.65);
            float2 sd = abs(f - starPos);
            float star = exp(-(sd.x + sd.y) * 16.0) + exp(-max(sd.x * 24.0, sd.y * 6.0)) * 0.45 + exp(-max(sd.y * 24.0, sd.x * 6.0)) * 0.45;
            float pulse = 0.55 + 0.45 * sin(timeSec * 5.0 + i * 1.7);
            acc += float3(0.95, 0.90, 0.65) * star * pulse;
        }
        return saturate(acc);
    }
    return col;
}

uint GlyphRow(uint chIdx, uint row) {
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
    float2 boxMin = float2(0.30, 0.04);
    float2 boxMax = float2(0.70, 0.14);
    if (uv.x < boxMin.x || uv.x > boxMax.x || uv.y < boxMin.y || uv.y > boxMax.y) {
        return col;
    }
    float2 localUv = (uv - boxMin) / (boxMax - boxMin);
    int gx = (int)floor(localUv.x * 62.0) - 2;
    int gy = (int)floor(localUv.y * 11.0) - 2;

    static const uint kMsg[10] = { 0, 1, 2, 3, 4, 5, 6, 3, 7, 8 };
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
        float aspect = texWidth / max(texHeight, 1.0);

        // 1. Eye / Detail Clarity + Soft Glow
        if (sharpen > 0.005 || softGlow > 0.005) {
            float3 blur = (
                texProc.SampleLevel(sampLin, sampleUv + float2(-texel.x, 0), 0).rgb +
                texProc.SampleLevel(sampLin, sampleUv + float2( texel.x, 0), 0).rgb +
                texProc.SampleLevel(sampLin, sampleUv + float2(0, -texel.y), 0).rgb +
                texProc.SampleLevel(sampLin, sampleUv + float2(0,  texel.y), 0).rgb
            ) * 0.25;
            if (sharpen > 0.005) {
                col = saturate(col + clamp(col - blur, -0.18, 0.18) * (sharpen * 1.45));
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

        // 2. AI-Anchored Eye Brightening & Teeth Whitening
        if (faceDetected > 0.5) {
            float iod = max(length(float2((leftEyeX - rightEyeX) * aspect, leftEyeY - rightEyeY)), 0.04);
            if (eyeBrighten > 0.005) {
                float dR = length(float2((sampleUv.x - rightEyeX) * aspect, sampleUv.y - rightEyeY));
                float dL = length(float2((sampleUv.x - leftEyeX) * aspect, sampleUv.y - leftEyeY));
                float eyeMask = smoothstep(iod * 0.28, iod * 0.06, min(dR, dL));
                col = saturate(col + eyeMask * eyeBrighten * 0.20 * (1.0 - col));
            }
            if (teethWhiten > 0.005) {
                float dM = length(float2((sampleUv.x - mouthCenterX) * aspect * 0.75, (sampleUv.y - mouthCenterY) * 1.5));
                float mouthMask = smoothstep(iod * 0.34, iod * 0.12, dM);
                float maxC = max(col.r, max(col.g, col.b));
                float minC = min(col.r, min(col.g, col.b));
                float sat = maxC - minC;
                float toothLikelihood = mouthMask * smoothstep(0.32, 0.10, sat) * smoothstep(0.38, 0.58, Luma(col));
                float3 whiter = float3(col.r + 0.06, col.g + 0.07, col.b + 0.13);
                col = saturate(lerp(col, whiter, toothLikelihood * teethWhiten * 0.85));
            }
        }

        // 3. White Balance, Exposure, Brightness, Shadows/Highlights, Contrast, Gamma, Saturation/Vibrance
        float3 wb = lerp(float3(1.0, 1.0, 1.0), float3(awbGainR, awbGainG, awbGainB), awbStrength);
        wb.r *= (1.0 + temperature * 0.18);
        wb.b *= (1.0 - temperature * 0.18);
        wb.g *= (1.0 - tint * 0.12);
        wb.r *= (1.0 + tint * 0.06);
        wb.b *= (1.0 + tint * 0.06);
        col = saturate(col * wb);
        col = saturate(col * exp2(exposure) + brightness);

        float lum = Luma(col);
        col = saturate(col + shadows * 0.28 * (1.0 - smoothstep(0.0, 0.55, lum)) * (1.0 - col) +
                       highlights * 0.25 * smoothstep(0.45, 1.0, lum) * col);
        col = saturate((col - 0.5) * contrast + 0.5);
        col = pow(max(col, 0.0001), 1.0 / max(gammaVal, 0.2));

        float lum2 = Luma(col);
        float satCurr = max(col.r, max(col.g, col.b)) - min(col.r, min(col.g, col.b));
        float skinProt = saturate(1.0 - smoothstep(0.0, 0.35, abs(col.r - col.g - 0.12)));
        float finalSat = max(0.0, saturation * (1.0 + vibrance * (1.0 - satCurr) * (1.0 - 0.45 * skinProt)));
        col = saturate(lerp(float3(lum2, lum2, lum2), col, finalSat));

        // 4. Real AI Background Isolation / Bokeh / Virtual Studio / Custom Image
        if (bgMode > 0 && hasAiMask > 0.5) {
            float rawMask = texMask.SampleLevel(sampLin, sampleUv, 0);
            float fgAlpha = smoothstep(0.28, 0.72, rawMask);
            float3 bgCol;
            if (bgMode == 1) {
                float radius = lerp(2.0, 14.0, saturate(bgBlurStrength));
                bgCol = SampleBlurredBg(sampleUv, texel, radius);
            } else if (bgMode == 5 && hasCustomBg > 0.5) {
                bgCol = texCustomBg.SampleLevel(sampLin, sampleUv, 0).rgb;
            } else {
                bgCol = ProceduralVirtualBg(bgMode, sampleUv, aspect);
            }
            col = lerp(bgCol, col, fgAlpha);
        }

        // 5. Real Stylized Visual Shaders (Anime/Sketch/VHS/Neon/Cinema)
        if (styleFilterMode > 0) {
            col = ApplyStylizedShader(styleFilterMode, col, sampleUv, texel, styleIntensity);
        }

        // 6. Real AR Face Overlays (Aviator/Round Glasses/Cyber Visor/Halo/Sparkles)
        if (arFilterMode > 0) {
            col = ApplyArFaceOverlay(arFilterMode, col, sampleUv, aspect);
        }

        // 7. Vignette
        if (vignette > 0.005) {
            float2 d = (sampleUv - 0.5) * float2(1.15, 1.0);
            col *= saturate(1.0 - vignette * 0.75 * smoothstep(0.22, 0.78, dot(d, d)));
        }
    }

    if (isPreview > 0.5 && splitPos >= 0.0 && abs(uv.x - splitPos) < (1.5 / max(texWidth, 1.0))) {
        col = float3(0.25, 0.75, 1.0);
    }

    if (mirrorCheck > 0.5) {
        col = OverlayMirrorCheck(col, sampleUv);
    }

    return float4(saturate(col), 1.0);
}
