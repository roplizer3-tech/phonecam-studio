using System;
using System.IO;
using System.Text.Json;

namespace PhoneCamStudio.Models;

public enum PlatformMirrorPreset
{
    WhatsApp,
    Telegram,
    ZoomMeetTeams,
    RawCustom
}

public sealed class CameraTransformState
{
    public bool FlipH { get; set; }
    public bool FlipV { get; set; }
    public int ManualRotationDeg { get; set; }
    public bool AutoRotateFromPhone { get; set; } = true;
    public int FramingMode { get; set; } // 0=Fill/Crop, 1=Fit+Blur, 2=Fit+Black
    public float Zoom { get; set; } = 1.0f;
    public float PanX { get; set; }
    public float PanY { get; set; }

    public CameraTransformState Clone() => (CameraTransformState)MemberwiseClone();
}

public sealed class MirrorUserSettings
{
    public PlatformMirrorPreset ActivePreset { get; set; } = PlatformMirrorPreset.WhatsApp;
    public bool PreviewMirrorLocal { get; set; } = true;
    public bool PerCameraMemoryEnabled { get; set; } = true;
    public CameraTransformState FrontCamera { get; set; } = new() { FlipH = false };
    public CameraTransformState BackCamera { get; set; } = new() { FlipH = false };
}

public sealed class MirrorSettingsManager
{
    private static readonly JsonSerializerOptions JsonOpts = new() { WriteIndented = true };
    private readonly string _settingsPath;

    public MirrorUserSettings State { get; private set; }
    public bool IsFrontCameraActive { get; private set; } = true;

    public MirrorSettingsManager(string? settingsPath = null)
    {
        _settingsPath = settingsPath ?? DefaultSettingsPath();
        State = Load();
    }

    public static string DefaultSettingsPath() =>
        Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "PhoneCamStudio",
            "mirror_settings.json");

    public CameraTransformState CurrentTransform =>
        State.PerCameraMemoryEnabled
            ? (IsFrontCameraActive ? State.FrontCamera : State.BackCamera)
            : State.FrontCamera;

    public void OnCameraFacingChanged(bool isFront)
    {
        IsFrontCameraActive = isFront;
    }

    public void ApplyPlatformPreset(PlatformMirrorPreset preset)
    {
        State.ActivePreset = preset;
        switch (preset)
        {
            case PlatformMirrorPreset.WhatsApp:
                // WhatsApp mirrors the user's own PIP preview locally, while sending the virtual cam feed unflipped to the recipient.
                // Keeping Output FlipH = false guarantees readable text for the caller; users can toggle Ctrl+Shift+M anytime if desired.
                CurrentTransform.FlipH = false;
                CurrentTransform.FlipV = false;
                CurrentTransform.FramingMode = 0; // Smart Fill 16:9
                State.PreviewMirrorLocal = true;
                break;
            case PlatformMirrorPreset.Telegram:
                CurrentTransform.FlipH = false;
                CurrentTransform.FlipV = false;
                CurrentTransform.FramingMode = 0;
                State.PreviewMirrorLocal = true;
                break;
            case PlatformMirrorPreset.ZoomMeetTeams:
                CurrentTransform.FlipH = false;
                CurrentTransform.FlipV = false;
                CurrentTransform.FramingMode = 0;
                State.PreviewMirrorLocal = true;
                break;
            case PlatformMirrorPreset.RawCustom:
                CurrentTransform.AutoRotateFromPhone = true;
                break;
        }
        Save();
    }

    public bool ToggleOutputFlipH()
    {
        CurrentTransform.FlipH = !CurrentTransform.FlipH;
        State.ActivePreset = PlatformMirrorPreset.RawCustom;
        Save();
        return CurrentTransform.FlipH;
    }

    public int RotateClockwise90()
    {
        CurrentTransform.ManualRotationDeg = (CurrentTransform.ManualRotationDeg + 90) % 360;
        Save();
        return CurrentTransform.ManualRotationDeg;
    }

    public static string GetPresetGuidance(PlatformMirrorPreset preset, bool isArabic)
    {
        return (preset, isArabic) switch
        {
            (PlatformMirrorPreset.WhatsApp, true) =>
                "وضع واتساب: يعرض لك واتساب معاينة مرآة في نافذتك الصغيرة، ويرسل للطرف الآخر الصورة المعتدلة (النصوص مقروءة). استخدم زر 'اختبار الانعكاس' للتأكد أو Ctrl+Shift+M للعكس الفوري.",
            (PlatformMirrorPreset.WhatsApp, false) =>
                "WhatsApp mode: WhatsApp mirrors your local PIP preview while sending an unflipped stream to the caller. Use 'Mirror Check' to verify or press Ctrl+Shift+M mid-call.",
            (PlatformMirrorPreset.Telegram, true) =>
                "وضع تيلجرام: يبث الصورة بإطار 16:9 كامل بدون عكس مزدوج. إذا ظهرت معكوسة في مكالمة تيلجرام اضغط Ctrl+Shift+M مباشرةً.",
            (PlatformMirrorPreset.Telegram, false) =>
                "Telegram mode: Streams 16:9 unflipped output. Press Ctrl+Shift+M anytime mid-call to toggle output mirroring.",
            (PlatformMirrorPreset.ZoomMeetTeams, true) =>
                "وضع Zoom / Google Meet / Teams: متوافق مع معاينة المرآة الذكية في منصات الاجتماعات بحيث يراك الحضور بشكل طبيعي.",
            (PlatformMirrorPreset.ZoomMeetTeams, false) =>
                "Zoom / Meet / Teams mode: Tuned so attendees see unmirrored text and natural orientation.",
            (_, true) =>
                "وضع مخصص: تحكم يدوي كامل بالانعكاس والدوران وحفظ إعداد منفصل لكل من الكاميرا الأمامية والخلفية.",
            (_, false) =>
                "Custom mode: Full manual flip & rotation control with independent Front/Rear camera memory."
        };
    }

    public void Save()
    {
        try
        {
            string? dir = Path.GetDirectoryName(_settingsPath);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            File.WriteAllText(_settingsPath, JsonSerializer.Serialize(State, JsonOpts));
        }
        catch
        {
            // Ignore IO failure on read-only path
        }
    }

    private MirrorUserSettings Load()
    {
        try
        {
            if (File.Exists(_settingsPath))
            {
                var loaded = JsonSerializer.Deserialize<MirrorUserSettings>(File.ReadAllText(_settingsPath), JsonOpts);
                if (loaded != null) return loaded;
            }
        }
        catch
        {
            // Fall back to defaults
        }
        return new MirrorUserSettings();
    }
}
