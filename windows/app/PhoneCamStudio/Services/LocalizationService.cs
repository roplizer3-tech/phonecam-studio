using System.Collections.Generic;
using System.ComponentModel;
using System.Windows;

namespace PhoneCamStudio.Services;

public sealed class LocalizationService : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    private bool _isArabic = true;

    public bool IsArabic
    {
        get => _isArabic;
        set
        {
            if (_isArabic == value) return;
            _isArabic = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsArabic)));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(FlowDirection)));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ToggleButtonLabel)));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs("Item[]"));
        }
    }

    public System.Windows.FlowDirection FlowDirection =>
        _isArabic ? System.Windows.FlowDirection.RightToLeft : System.Windows.FlowDirection.LeftToRight;

    public string ToggleButtonLabel => _isArabic ? "EN" : "عربي";

    public string this[string key] => Get(key);

    public string Get(string key)
    {
        if (Strings.TryGetValue(key, out var pair))
        {
            return _isArabic ? pair.Ar : pair.En;
        }
        return key;
    }

    public static readonly IReadOnlyDictionary<string, (string Ar, string En)> Strings =
        new Dictionary<string, (string Ar, string En)>
        {
            ["AppTitle"] = ("PhoneCam Studio", "PhoneCam Studio"),
            ["StartVCam"] = ("تشغيل الكاميرا الافتراضية", "Start Virtual Camera"),
            ["StopVCam"] = ("إيقاف الكاميرا الافتراضية", "Stop Virtual Camera"),
            ["VCamLive"] = ("نشط • PhoneCam Studio", "LIVE • PhoneCam Studio"),
            ["VCamOff"] = ("الكاميرا الافتراضية متوقفة", "Virtual Camera Off"),
            ["Diagnostics"] = ("التشخيص والتسجيل", "Diagnostics"),
            ["MinimizeTray"] = ("تصغير للشريط", "To Tray"),
            ["PreviewSelfie"] = ("معاينة مرآة لك (سيلفي)", "Selfie Preview (Mirrored)"),
            ["PreviewRecipient"] = ("كما يراك الطرف الآخر", "Recipient View (Exact)"),
            ["BeforeAfter"] = ("مقارنة قبل / بعد", "Before / After"),
            ["MirrorCheck"] = ("اختبار الانعكاس (5ث)", "Mirror Check (5s)"),
            ["TestPattern"] = ("نمط اختبار", "Test Pattern"),
            ["BypassEffects"] = ("إيقاف الفلاتر مؤقتاً", "Bypass Effects"),

            ["TabConnect"] = ("الاتصال", "Connect"),
            ["TabAiAr"] = ("فلاتر AI/AR", "AI & AR FX"),
            ["TabMirror"] = ("العكس", "Mirror"),
            ["TabBeauty"] = ("التجميل", "Beauty"),
            ["TabColor"] = ("الألوان", "Presets"),

            ["DiscoveredPhones"] = ("الأجهزة المكتشفة على Wi-Fi", "Discovered Wi-Fi Devices"),
            ["NoPhonesYet"] = ("افتح تطبيق PhoneCam Sender على الموبايل واضغط بدء البث...", "Open PhoneCam Sender on Android and tap Start..."),
            ["ConnectUsb"] = ("اتصال فوري عبر USB (ADB مدمج)", "1-Click USB Connect (Bundled ADB)"),
            ["BluetoothTitle"] = ("الاتصال عبر بلوتوث (Bluetooth RFCOMM)", "Bluetooth Classic Connection"),
            ["RefreshBt"] = ("تحديث الأجهزة المقترنة", "Refresh Paired"),
            ["ConnectBt"] = ("اتصال بلوتوث", "Connect BT"),
            ["NoBtDevices"] = ("لا توجد هواتف مقترنة بالبلوتوث بعد. اقترن من إعدادات ويندوز أولاً.", "No paired Bluetooth phones found. Pair phone in Windows Bluetooth settings first."),
            ["BtLowBwNote"] = ("وضع البلوتوث يضبط الجودة تلقائياً على 360p @ 20 FPS لضمان سلاسة البث بدون تقطيع.", "Bluetooth auto-tunes to 360p @ 20 FPS for stable low-bandwidth streaming."),
            ["ManualIp"] = ("اتصال يدوي بالـ IP", "Manual IP Address"),
            ["PinCode"] = ("رمز PIN", "4-Digit PIN"),
            ["Connect"] = ("اتصال", "Connect"),
            ["Disconnect"] = ("قطع الاتصال", "Disconnect"),
            ["RemotePhoneControls"] = ("التحكم بكاميرا الموبايل عن بُعد", "Remote Phone Camera Control"),
            ["SwitchFrontBack"] = ("تبديل أمامية ⇄ خلفية", "Switch Front ↔ Rear"),
            ["ToggleTorch"] = ("تشغيل / إطفاء الفلاش", "Toggle Torch / Flash"),
            ["PhoneLens"] = ("العدسة المختارة", "Selected Lens"),
            ["StreamRes"] = ("دقة البث", "Stream Resolution"),
            ["StreamFps"] = ("معدل الإطارات", "Frame Rate"),
            ["StreamCodec"] = ("نوع الترميز", "Video Codec"),
            ["ZoomControl"] = ("زوم عدسة الموبايل", "Phone Optical/Digital Zoom"),

            ["AiBackground"] = ("عزل وتغيير الخلفية (AI Segmentation)", "AI Background Replacement"),
            ["BgNone"] = ("بدون تغيير (الخلفية الحقيقية)", "None (Real Background)"),
            ["BgDslrBlur"] = ("عزل احترافي DSLR Bokeh Blur", "DSLR Portrait Bokeh Blur"),
            ["BgOffice"] = ("خلفية مكتب / استوديو", "Modern Studio Office"),
            ["BgBokehOrbs"] = ("أضواء بوكيه دافئة", "Warm Bokeh Lights"),
            ["BgCyberStudio"] = ("خلفية سايبر نيون", "Cyber Studio Grid"),
            ["BgCustomImage"] = ("صورة مخصصة من جهازك...", "Custom Background Image..."),
            ["BrowseImage"] = ("اختيار صورة", "Browse Image"),
            ["BgBlurStrength"] = ("قوة عزل الخلفية (Bokeh)", "Background Blur Strength"),
            ["AutoFrameCenterStage"] = ("تتبع تلقائي للوجه (Center Stage AI)", "AI Auto-Framing (Center Stage)"),

            ["ArFaceFilters"] = ("فلاتر الوجه بالواقع المعزز (AR Face Landmarks)", "AI Landmark AR Filters"),
            ["ArNone"] = ("بدون فلتر AR", "None"),
            ["ArAviator"] = ("نظارة شمسية كلاسيك (Aviator)", "Aviator Sunglasses"),
            ["ArRoundGlasses"] = ("نظارة دائرية ذكية (Round Glasses)", "Studio Round Glasses"),
            ["ArCyberVisor"] = ("قناع سايبر بانك متوهج (Cyber Visor)", "Cyberpunk Hologram Visor"),
            ["ArHaloCrown"] = ("هالة ذهبية مضيئة (Golden Halo)", "Golden Halo Crown"),
            ["ArSparkleAura"] = ("توهج نجوم متحرك (Sparkle Aura)", "Animated Sparkle Aura"),

            ["StyleShaders"] = ("فلاتر بصرية حقيقية (GPU Pixel Shaders)", "Real Stylized Shaders"),
            ["StyleNone"] = ("بدون تأثير فني", "None (Natural Lens)"),
            ["StyleAnime"] = ("أنمي / كرتون حقيقي (Cel-Shading + حواف)", "Anime / Cartoon Cel-Shade"),
            ["StyleSketch"] = ("رسم يدوي بقلم رصاص", "Pencil Sketch Ink"),
            ["StyleVhs"] = ("كاميرا فينتاج VHS 1995", "Retro VHS 1995 Tape"),
            ["StyleCyberNeon"] = ("تحديد حواف نيون سايبر", "Cyber Neon Edge Glow"),
            ["StyleHalation"] = ("إضاءة سينمائية 35mm Halation", "Cinema 35mm Film Halation"),
            ["StyleIntensity"] = ("قوة التأثير البصري", "Shader Intensity"),

            ["FaceSculptHeader"] = ("تحسين الوجه وتبييض الأسنان والعيون (AI)", "AI Face Sculpting & Clarity"),
            ["FaceSlim"] = ("نحت وتنحيف الوجه", "Face Slimming"),
            ["EyeEnlarge"] = ("تكبير العيون طبيعياً", "Natural Eye Enlargement"),
            ["EyeBrighten"] = ("تفتيح ولمعان العيون", "Eye Brightening"),
            ["TeethWhiten"] = ("تبييض الأسنان الذكي", "Smart Teeth Whitening"),

            ["PlatformProfiles"] = ("إصلاح العكس حسب التطبيق", "Platform Mirror Fix"),
            ["OutputTransform"] = ("تحويل الخرج الفعلي للكاميرا", "Virtual Camera Output Transform"),
            ["FlipHorizontal"] = ("عكس أفقي للخرج (Output Flip H)", "Output Horizontal Flip"),
            ["FlipVertical"] = ("عكس رأسي (Flip V)", "Vertical Flip"),
            ["Rotate90"] = ("تدوير 90°", "Rotate 90°"),
            ["AutoRotatePhone"] = ("تدوير تلقائي حسب مسكة الموبايل", "Auto-Rotate from Phone Sensor"),
            ["PerCameraMemory"] = ("حفظ إعداد منفصل للأمامية والخلفية", "Separate Front & Rear Camera Memory"),
            ["SmartFraming"] = ("تأطير الموبايل الرأسي في 16:9", "Portrait-to-16:9 Smart Framing"),
            ["FrameFill"] = ("ملء الإطار (قص ذكي)", "Smart Fill (Crop)"),
            ["FrameBlur"] = ("إطار كامل + خلفية ضبابية", "Fit + Blurred Bars"),
            ["FrameBlack"] = ("إطار كامل + حواف داكنة", "Fit + Dark Bars"),
            ["DigitalZoom"] = ("زوم رقمي", "Digital Zoom"),
            ["HotkeyHint"] = ("اختصار سريع أثناء المكالمات: Ctrl+Shift+M لعكس الكاميرا فوراً", "Global hotkey mid-call: Ctrl+Shift+M flips output immediately"),

            ["SkinSmooth"] = ("تنعيم البشرة الذكي (حماية العين والشعر)", "Smart Skin Smooth (Eye-Safe)"),
            ["StudioLight"] = ("إضاءة استوديو للوجه", "Studio Key-Light"),
            ["Denoise"] = ("تقليل تشويش الإضاءة الضعيفة", "Low-Light Denoise"),
            ["EyeClarity"] = ("توضيح الملامح والعيون", "Eye & Detail Clarity"),
            ["SoftGlow"] = ("توهج ناعم (Soft Glow)", "Soft Glow"),
            ["AutoWB"] = ("توازن أبيض تلقائي (AWB)", "Auto White Balance"),

            ["BuiltInPresets"] = ("فلاتر وبريستات احترافية (16)", "Pro AI & Color Presets (16)"),
            ["CustomPresets"] = ("فلاترك المحفوظة", "Saved Custom Presets"),
            ["SavePreset"] = ("حفظ كفلتر جديد", "Save New Preset"),
            ["DeletePreset"] = ("حذف", "Delete"),
            ["PresetNameHint"] = ("اسم الفلتر الجديد...", "New preset name..."),
            ["ResetColors"] = ("إعادة ضبط جميع الفلاتر", "Reset All Filters & Colors"),
            ["Exposure"] = ("التعريض (Exposure)", "Exposure"),
            ["Brightness"] = ("السطوع (Brightness)", "Brightness"),
            ["Contrast"] = ("التباين (Contrast)", "Contrast"),
            ["Highlights"] = ("الإضاءة العالية (Highlights)", "Highlights"),
            ["Shadows"] = ("الظلال (Shadows)", "Shadows"),
            ["Gamma"] = ("جاما (Gamma)", "Gamma"),
            ["Temperature"] = ("حرارة اللون (دافئ / بارد)", "Color Temperature"),
            ["Tint"] = ("الصبغة (Tint)", "Tint"),
            ["Saturation"] = ("التشبع اللوني (Saturation)", "Saturation"),
            ["Vibrance"] = ("الحيوية (حماية البشرة)", "Vibrance (Skin-Safe)"),
            ["Vignette"] = ("تظليل الحواف (Vignette)", "Vignette"),

            ["DiagTitle"] = ("تشخيص الكاميرا الافتراضية على ويندوز 11 / 10", "Virtual Camera Diagnostics (Windows 11 / 10)"),
            ["ReregisterAdmin"] = ("تسجيل / تثبيت الكاميرا الافتراضية بالنظام (Admin)", "Install System Virtual Camera (Admin)"),
            ["Close"] = ("إغلاق", "Close")
        };
}
