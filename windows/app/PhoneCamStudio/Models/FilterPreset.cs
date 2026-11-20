using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace PhoneCamStudio.Models;

public sealed class ColorBeautySettings
{
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
            Id = "soft_beauty", NameAr = "تحسين مظهر ناعم", NameEn = "Soft Beauty", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                SkinSmooth = 0.65f, StudioLight = 0.35f, SoftGlow = 0.32f, Sharpen = 0.25f,
                Exposure = 0.15f, Temperature = 0.08f, Vibrance = 0.15f, Shadows = 0.18f
            }
        },
        new()
        {
            Id = "clean_office", NameAr = "اجتماعات رسمية", NameEn = "Clean Office", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                SkinSmooth = 0.20f, StudioLight = 0.30f, Denoise = 0.20f, Sharpen = 0.30f,
                AutoWhiteBalance = 0.60f, Contrast = 1.05f, Temperature = -0.05f, Saturation = 0.96f
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
            Id = "cool_daylight", NameAr = "نهاري بارد", NameEn = "Cool Daylight", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                Temperature = -0.26f, Tint = -0.04f, Contrast = 1.10f, Highlights = 0.12f,
                Sharpen = 0.28f, Saturation = 1.05f
            }
        },
        new()
        {
            Id = "cinema_teal_orange", NameAr = "سينمائي", NameEn = "Cinema T&O", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                Contrast = 1.18f, Shadows = -0.12f, Highlights = -0.18f, Temperature = 0.16f,
                Tint = -0.10f, Saturation = 0.92f, Vibrance = 0.28f, Vignette = 0.35f, Sharpen = 0.25f
            }
        },
        new()
        {
            Id = "vivid_pop", NameAr = "ألوان زاهية", NameEn = "Vivid Pop", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                Contrast = 1.14f, Saturation = 1.20f, Vibrance = 0.35f, Sharpen = 0.38f,
                Exposure = 0.08f
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
            Id = "matte_vintage", NameAr = "فينتاج كلاسيك", NameEn = "Matte Vintage", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                Brightness = 0.04f, Contrast = 0.90f, Shadows = 0.22f, Temperature = 0.20f,
                Saturation = 0.78f, Vignette = 0.40f
            }
        },
        new()
        {
            Id = "crisp_bw", NameAr = "أبيض وأسود", NameEn = "Crisp B&W", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                Saturation = 0f, Contrast = 1.24f, Sharpen = 0.40f, Highlights = 0.10f,
                Shadows = -0.08f, Vignette = 0.25f
            }
        },
        new()
        {
            Id = "noir_dramatic", NameAr = "درامي نوار", NameEn = "Noir Dramatic", IsBuiltIn = true,
            Settings = new ColorBeautySettings
            {
                Saturation = 0f, Contrast = 1.42f, Exposure = -0.15f, Shadows = -0.25f,
                Highlights = 0.20f, Vignette = 0.55f, Sharpen = 0.35f
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
