using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace PhoneCamStudio.Models;

public sealed class ColorBeautySettings
{
    public int BgMode { get; set; } = 0;           // 0=None, 1=DSLR Bokeh, 2=Studio Office, 3=Bokeh Orbs, 4=Cyber Studio, 5=Custom Image
    public int ArFilterMode { get; set; } = 0;     // 0=None, 1=Aviator, 2=Round Glasses, 3=Cyber Visor, 4=Halo Crown, 5=Sparkle Aura
    public int StyleFilterMode { get; set; } = 0;  // 0=None, 1=Anime, 2=Pencil Sketch, 3=Retro VHS, 4=Cyber Neon, 5=Cinema Halation
    public bool AutoFrameEnabled { get; set; } = false;

    public float BgBlurStrength { get; set; } = 0.65f;
    public float FaceSlim { get; set; } = 0f;
    public float EyeEnlarge { get; set; } = 0f;
    public float EyeBrighten { get; set; } = 0f;
    public float TeethWhiten { get; set; } = 0f;
    public float StyleIntensity { get; set; } = 0.75f;

    public float SkinSmooth { get; set; } = 0f;
    public float Denoise { get; set; } = 0f;
    public float StudioLight { get; set; } = 0f;
    public float Sharpen { get; set; } = 0f;
    public float SoftGlow { get; set; } = 0f;
    public float AutoWhiteBalance { get; set; } = 0f;

    public float Exposure { get; set; } = 0f;
    public float Brightness { get; set; } = 0f;
    public float Contrast { get; set; } = 1.0f;
    public float Highlights { get; set; } = 0f;
    public float Shadows { get; set; } = 0f;
    public float GammaVal { get; set; } = 1.0f;
    public float Temperature { get; set; } = 0f;
    public float Tint { get; set; } = 0f;
    public float Saturation { get; set; } = 1.0f;
    public float Vibrance { get; set; } = 0f;
    public float Vignette { get; set; } = 0f;

    public ColorBeautySettings Clone() => (ColorBeautySettings)MemberwiseClone();

    public void Clamp()
    {
        BgMode = Math.Clamp(BgMode, 0, 5);
        ArFilterMode = Math.Clamp(ArFilterMode, 0, 5);
        StyleFilterMode = Math.Clamp(StyleFilterMode, 0, 5);
        BgBlurStrength = Math.Clamp(BgBlurStrength, 0f, 1f);
        FaceSlim = Math.Clamp(FaceSlim, 0f, 1f);
        EyeEnlarge = Math.Clamp(EyeEnlarge, 0f, 1f);
        EyeBrighten = Math.Clamp(EyeBrighten, 0f, 1f);
        TeethWhiten = Math.Clamp(TeethWhiten, 0f, 1f);
        StyleIntensity = Math.Clamp(StyleIntensity, 0f, 1f);

        SkinSmooth = Math.Clamp(SkinSmooth, 0f, 1f);
        Denoise = Math.Clamp(Denoise, 0f, 1f);
        StudioLight = Math.Clamp(StudioLight, 0f, 1f);
        Sharpen = Math.Clamp(Sharpen, 0f, 1f);
        SoftGlow = Math.Clamp(SoftGlow, 0f, 1f);
        AutoWhiteBalance = Math.Clamp(AutoWhiteBalance, 0f, 1f);
        Exposure = Math.Clamp(Exposure, -2f, 2f);
        Brightness = Math.Clamp(Brightness, -0.5f, 0.5f);
        Contrast = Math.Clamp(Contrast, 0.5f, 1.8f);
        Highlights = Math.Clamp(Highlights, -1f, 1f);
        Shadows = Math.Clamp(Shadows, -1f, 1f);
        GammaVal = Math.Clamp(GammaVal, 0.6f, 1.6f);
        Temperature = Math.Clamp(Temperature, -1f, 1f);
        Tint = Math.Clamp(Tint, -1f, 1f);
        Saturation = Math.Clamp(Saturation, 0f, 2f);
        Vibrance = Math.Clamp(Vibrance, -1f, 1f);
        Vignette = Math.Clamp(Vignette, 0f, 1f);
    }
}

public sealed class FilterPreset
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N");
    public string NameAr { get; set; } = "";
    public string NameEn { get; set; } = "";
    public bool IsBuiltIn { get; set; }
    public ColorBeautySettings Settings { get; set; } = new();
    public string DisplayLabel => NameAr == NameEn ? NameAr : $"{NameAr}  •  {NameEn}";
}

public static class PresetCatalog
{
    private static readonly JsonSerializerOptions JsonOpts = new() { WriteIndented = true };

    public static string DefaultUserPresetsPath() =>
        Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "PhoneCamStudio",
            "presets.json");

    public static IReadOnlyList<FilterPreset> BuiltInPresets { get; } = new List<FilterPreset>
    {
        new()
        {
            Id = "natural", NameAr = "طبيعي (أصلي)", NameEn = "Natural", IsBuiltIn = true,
            Settings = new ColorBeautySettings()
        },
        new()
        {
            Id = "dslr_portrait", NameAr = "عزل خلفية DSLR بالذكاء الاصطناعي", NameEn = "AI DSLR Portrait", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                BgMode = 1, BgBlurStrength = 0.72f, SkinSmooth = 0.35f, StudioLight = 0.42f,
                EyeBrighten = 0.30f, Sharpen = 0.30f, Contrast = 1.06f, Vibrance = 0.16f
            }
        },
        new()
        {
            Id = "ai_office", NameAr = "خلفية مكتب استوديو (AI)", NameEn = "AI Studio Office", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                BgMode = 2, SkinSmooth = 0.25f, StudioLight = 0.38f, AutoWhiteBalance = 0.55f,
                EyeBrighten = 0.20f, Sharpen = 0.32f, Contrast = 1.06f
            }
        },
        new()
        {
            Id = "bokeh_orbs", NameAr = "أضواء بوكيه ليلية (AI)", NameEn = "AI Bokeh Lights", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                BgMode = 3, SkinSmooth = 0.30f, StudioLight = 0.32f, SoftGlow = 0.22f, Contrast = 1.05f
            }
        },
        new()
        {
            Id = "cyber_studio", NameAr = "استوديو سايبر نيون (AI)", NameEn = "AI Cyber Studio", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                BgMode = 4, Contrast = 1.12f, Vibrance = 0.22f, Sharpen = 0.28f
            }
        },
        new()
        {
            Id = "anime_cel", NameAr = "أنمي / كرتون حقيقي", NameEn = "Anime / Cartoon", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                StyleFilterMode = 1, StyleIntensity = 0.90f
            }
        },
        new()
        {
            Id = "pencil_sketch", NameAr = "رسم بقلم رصاص", NameEn = "Pencil Sketch", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                StyleFilterMode = 2, StyleIntensity = 0.92f
            }
        },
        new()
        {
            Id = "retro_vhs", NameAr = "شريط كاسيت VHS 1995", NameEn = "Retro VHS 1995", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                StyleFilterMode = 3, StyleIntensity = 0.85f, Contrast = 1.08f
            }
        },
        new()
        {
            Id = "cyber_neon_edge", NameAr = "حواف نيون متوهجة", NameEn = "Cyber Neon Edges", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                StyleFilterMode = 4, StyleIntensity = 0.88f
            }
        },
        new()
        {
            Id = "cinema_halation", NameAr = "سينما فيلم هاليشن 35mm", NameEn = "Cinema Halation 35mm", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                StyleFilterMode = 5, StyleIntensity = 0.78f, Contrast = 1.14f, Vibrance = 0.20f, Vignette = 0.28f
            }
        },
        new()
        {
            Id = "ar_aviator", NameAr = "نظارة شمسية AR بالذكاء الاصطناعي", NameEn = "AR Aviator Sunglasses", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                ArFilterMode = 1, StudioLight = 0.30f, Contrast = 1.08f
            }
        },
        new()
        {
            Id = "soft_beauty", NameAr = "تجميل حقيقي + نحت وجه (AI)", NameEn = "Beauty & Face Sculpt", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                SkinSmooth = 0.58f, StudioLight = 0.40f, FaceSlim = 0.28f, EyeEnlarge = 0.18f,
                EyeBrighten = 0.35f, TeethWhiten = 0.38f, SoftGlow = 0.22f, Vibrance = 0.15f
            }
        },
        new()
        {
            Id = "studio_pro", NameAr = "استوديو احترافي", NameEn = "Studio Pro", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                SkinSmooth = 0.35f, StudioLight = 0.42f, Denoise = 0.25f, Sharpen = 0.35f,
                AutoWhiteBalance = 0.45f, Contrast = 1.08f, Shadows = 0.15f, Highlights = -0.10f,
                Vibrance = 0.20f, Vignette = 0.18f
            }
        },
        new()
        {
            Id = "warm_golden", NameAr = "دافئ ذهبي", NameEn = "Warm Golden", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                SkinSmooth = 0.25f, StudioLight = 0.25f, Temperature = 0.32f, Tint = 0.05f,
                Exposure = 0.12f, Contrast = 1.06f, Vibrance = 0.25f, SoftGlow = 0.20f
            }
        },
        new()
        {
            Id = "low_light_boost", NameAr = "إضاءة ضعيفة", NameEn = "Low-Light Boost", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                Denoise = 0.65f, StudioLight = 0.60f, SkinSmooth = 0.25f, Exposure = 0.55f,
                Shadows = 0.45f, GammaVal = 1.14f, AutoWhiteBalance = 0.50f
            }
        },
        new()
        {
            Id = "crisp_bw", NameAr = "أبيض وأسود سينمائي", NameEn = "Crisp B&W", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                Saturation = 0f, Contrast = 1.26f, Sharpen = 0.40f, Highlights = 0.10f,
                Shadows = -0.08f, Vignette = 0.28f
            }
        }
    };

    public static List<FilterPreset> LoadCustomPresets(string? path = null)
    {
        string target = path ?? DefaultUserPresetsPath();
        try
        {
            if (!File.Exists(target)) return new List<FilterPreset>();
            string json = File.ReadAllText(target);
            var list = JsonSerializer.Deserialize<List<FilterPreset>>(json, JsonOpts) ?? new List<FilterPreset>();
            foreach (var p in list)
            {
                p.IsBuiltIn = false;
                p.Settings.Clamp();
            }
            return list;
        }
        catch
        {
            return new List<FilterPreset>();
        }
    }

    public static void SaveCustomPresets(IEnumerable<FilterPreset> presets, string? path = null)
    {
        string target = path ?? DefaultUserPresetsPath();
        string? dir = Path.GetDirectoryName(target);
        if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
        var customOnly = new List<FilterPreset>();
        foreach (var p in presets)
        {
            if (!p.IsBuiltIn)
            {
                p.Settings.Clamp();
                customOnly.Add(p);
            }
        }
        File.WriteAllText(target, JsonSerializer.Serialize(customOnly, JsonOpts));
    }
}
