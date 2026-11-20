using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using PhoneCamStudio.Interop;
using PhoneCamStudio.Models;
using PhoneCamStudio.Services;

namespace PhoneCamStudio.ViewModels;

public sealed class DiscoveredPhoneItem
{
    public string Ip { get; set; } = "";
    public string DeviceName { get; set; } = "";
    public int TcpPort { get; set; } = 54820;
    public bool PinRequired { get; set; }
    public string Facing { get; set; } = "front";
    public bool Streaming { get; set; }
    public string DisplayLabel => $"{DeviceName} ({Ip}) {(PinRequired ? "🔒" : "")}";
}

public sealed class RemoteCameraOption
{
    public string Id { get; set; } = "";
    public string Facing { get; set; } = "back";
    public string Label { get; set; } = "";
    public bool HasFlash { get; set; }
    public float MaxZoom { get; set; } = 4.0f;
    public override string ToString() => $"{Label} ({Facing})";
}

public sealed class MainViewModel : INotifyPropertyChanged, IDisposable
{
    public event PropertyChangedEventHandler? PropertyChanged;

    private readonly Dispatcher _dispatcher;
    private readonly EngineSession _engine;
    private readonly DispatcherTimer _pollTimer;
    private readonly DispatcherTimer _mirrorCheckTimer;

    private WriteableBitmap? _previewBitmap;
    private byte[] _frameScratch = Array.Empty<byte>();
    private readonly object _frameLock = new();
    private volatile bool _uiCopyQueued;
    private int _pendingW;
    private int _pendingH;

    public LocalizationService Loc { get; } = new();
    public MirrorSettingsManager MirrorManager { get; } = new();
    public ColorBeautySettings Color { get; private set; } = new();

    public ObservableCollection<DiscoveredPhoneItem> DiscoveredPhones { get; } = new();
    public ObservableCollection<RemoteCameraOption> PhoneCameras { get; } = new();
    public ObservableCollection<FilterPreset> Presets { get; } = new();

    private string _manualIp = "192.168.1.";
    private string _pinCode = "";
    private bool _isConnected;
    private bool _isConnecting;
    private bool _isUsb;
    private bool _isVirtualCamActive;
    private string _statusMessage = "جاهز";
    private string _telemetrySummary = "1280x720 • 0 FPS • 0 ms";
    private bool _frontCameraActive = true;
    private bool _torchEnabled;
    private float _phoneZoom = 1.0f;
    private string _selectedRes = "720p";
    private int _selectedFps = 30;
    private string _selectedCodec = "h264";
    private RemoteCameraOption? _selectedPhoneCamera;

    private bool _mirrorCheckActive;
    private bool _beforeAfterEnabled;
    private float _splitComparePos = 0.5f;
    private bool _bypassEffects;
    private bool _testPatternMode;
    private FilterPreset? _selectedPreset;
    private string _newPresetName = "";
    private string _diagnosticsText = "";

    public MainViewModel(Dispatcher dispatcher)
    {
        _dispatcher = dispatcher;
        _engine = new EngineSession();
        LoadAllPresets();

        if (_engine.IsNativeLoaded)
        {
            _engine.StartDiscovery();
            _engine.SetPreviewCallback(OnNativePreviewFrame);
            PushVideoParams();
            RefreshDiagnostics();
        }
        else
        {
            _statusMessage = $"Native engine unavailable: {_engine.LoadError}";
        }

        _pollTimer = new DispatcherTimer(TimeSpan.FromMilliseconds(800), DispatcherPriority.Background, (_, _) => PollEngineState(), _dispatcher);
        _pollTimer.Start();

        _mirrorCheckTimer = new DispatcherTimer(TimeSpan.FromSeconds(5), DispatcherPriority.Normal, (_, _) =>
        {
            _mirrorCheckTimer.Stop();
            MirrorCheckActive = false;
        }, _dispatcher);
    }

    public WriteableBitmap? PreviewBitmap
    {
        get => _previewBitmap;
        private set { _previewBitmap = value; OnPropertyChanged(); }
    }

    public string ManualIp { get => _manualIp; set { _manualIp = value; OnPropertyChanged(); } }
    public string PinCode { get => _pinCode; set { _pinCode = value; OnPropertyChanged(); } }
    public bool IsConnected { get => _isConnected; private set { _isConnected = value; OnPropertyChanged(); } }
    public bool IsConnecting { get => _isConnecting; private set { _isConnecting = value; OnPropertyChanged(); } }
    public bool IsUsb { get => _isUsb; private set { _isUsb = value; OnPropertyChanged(); } }
    public bool IsVirtualCamActive
    {
        get => _isVirtualCamActive;
        private set
        {
            _isVirtualCamActive = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(VCamButtonLabel));
            OnPropertyChanged(nameof(VCamBadgeLabel));
        }
    }
    public string VCamButtonLabel => IsVirtualCamActive ? Loc["StopVCam"] : Loc["StartVCam"];
    public string VCamBadgeLabel => IsVirtualCamActive ? Loc["VCamLive"] : Loc["VCamOff"];

    public string StatusMessage { get => _statusMessage; private set { _statusMessage = value; OnPropertyChanged(); } }
    public string TelemetrySummary { get => _telemetrySummary; private set { _telemetrySummary = value; OnPropertyChanged(); } }
    public bool FrontCameraActive
    {
        get => _frontCameraActive;
        private set
        {
            if (_frontCameraActive != value)
            {
                _frontCameraActive = value;
                MirrorManager.OnCameraFacingChanged(value);
                OnPropertyChanged();
                NotifyMirrorProperties();
                PushVideoParams();
            }
        }
    }
    public string ActiveCameraBadge => FrontCameraActive
        ? (Loc.IsArabic ? "الأمامية (Front)" : "Front Camera")
        : (Loc.IsArabic ? "الخلفية (Rear)" : "Rear Camera");

    public bool TorchEnabled { get => _torchEnabled; set { _torchEnabled = value; OnPropertyChanged(); } }
    public float PhoneZoom
    {
        get => _phoneZoom;
        set
        {
            _phoneZoom = Math.Clamp(value, 1.0f, 6.0f);
            OnPropertyChanged();
            _engine.SendControlJson($"{{\"cmd\":\"set_zoom\",\"ratio\":{_phoneZoom:0.00}}}");
        }
    }

    public string SelectedRes { get => _selectedRes; set { _selectedRes = value; OnPropertyChanged(); PushRemoteStreamConfig(); } }
    public int SelectedFps { get => _selectedFps; set { _selectedFps = value; OnPropertyChanged(); PushRemoteStreamConfig(); PushVideoParams(); } }
    public string SelectedCodec { get => _selectedCodec; set { _selectedCodec = value; OnPropertyChanged(); PushRemoteStreamConfig(); } }

    public RemoteCameraOption? SelectedPhoneCamera
    {
        get => _selectedPhoneCamera;
        set
        {
            if (_selectedPhoneCamera == value) return;
            _selectedPhoneCamera = value;
            OnPropertyChanged();
            if (value != null && IsConnected)
            {
                _engine.SendControlJson($"{{\"cmd\":\"switch_camera\",\"cameraId\":\"{value.Id}\"}}");
            }
        }
    }

    // Mirror & Orientation properties
    public PlatformMirrorPreset ActivePlatformPreset => MirrorManager.State.ActivePreset;
    public string PlatformGuidance => MirrorSettingsManager.GetPresetGuidance(ActivePlatformPreset, Loc.IsArabic);
    public bool PreviewMirrorLocal
    {
        get => MirrorManager.State.PreviewMirrorLocal;
        set { MirrorManager.State.PreviewMirrorLocal = value; MirrorManager.Save(); OnPropertyChanged(); OnPropertyChanged(nameof(PreviewModeLabel)); PushVideoParams(); }
    }
    public string PreviewModeLabel => PreviewMirrorLocal ? Loc["PreviewSelfie"] : Loc["PreviewRecipient"];
    public bool PerCameraMemoryEnabled
    {
        get => MirrorManager.State.PerCameraMemoryEnabled;
        set { MirrorManager.State.PerCameraMemoryEnabled = value; MirrorManager.Save(); OnPropertyChanged(); NotifyMirrorProperties(); PushVideoParams(); }
    }
    public bool OutputFlipH
    {
        get => MirrorManager.CurrentTransform.FlipH;
        set { MirrorManager.CurrentTransform.FlipH = value; MirrorManager.Save(); OnPropertyChanged(); PushVideoParams(); }
    }
    public bool OutputFlipV
    {
        get => MirrorManager.CurrentTransform.FlipV;
        set { MirrorManager.CurrentTransform.FlipV = value; MirrorManager.Save(); OnPropertyChanged(); PushVideoParams(); }
    }
    public int ManualRotationDeg
    {
        get => MirrorManager.CurrentTransform.ManualRotationDeg;
        set { MirrorManager.CurrentTransform.ManualRotationDeg = ((value % 360) + 360) % 360; MirrorManager.Save(); OnPropertyChanged(); PushVideoParams(); }
    }
    public bool AutoRotateFromPhone
    {
        get => MirrorManager.CurrentTransform.AutoRotateFromPhone;
        set { MirrorManager.CurrentTransform.AutoRotateFromPhone = value; MirrorManager.Save(); OnPropertyChanged(); PushVideoParams(); }
    }
    public int FramingMode
    {
        get => MirrorManager.CurrentTransform.FramingMode;
        set { MirrorManager.CurrentTransform.FramingMode = Math.Clamp(value, 0, 2); MirrorManager.Save(); OnPropertyChanged(); PushVideoParams(); }
    }
    public float DigitalZoom
    {
        get => MirrorManager.CurrentTransform.Zoom;
        set { MirrorManager.CurrentTransform.Zoom = Math.Clamp(value, 1.0f, 3.0f); MirrorManager.Save(); OnPropertyChanged(); PushVideoParams(); }
    }
    public float PanX
    {
        get => MirrorManager.CurrentTransform.PanX;
        set { MirrorManager.CurrentTransform.PanX = Math.Clamp(value, -0.45f, 0.45f); MirrorManager.Save(); OnPropertyChanged(); PushVideoParams(); }
    }
    public float PanY
    {
        get => MirrorManager.CurrentTransform.PanY;
        set { MirrorManager.CurrentTransform.PanY = Math.Clamp(value, -0.45f, 0.45f); MirrorManager.Save(); OnPropertyChanged(); PushVideoParams(); }
    }

    public bool MirrorCheckActive
    {
        get => _mirrorCheckActive;
        set { _mirrorCheckActive = value; OnPropertyChanged(); PushVideoParams(); }
    }
    public bool BeforeAfterEnabled
    {
        get => _beforeAfterEnabled;
        set { _beforeAfterEnabled = value; OnPropertyChanged(); PushVideoParams(); }
    }
    public float SplitComparePos
    {
        get => _splitComparePos;
        set { _splitComparePos = Math.Clamp(value, 0.05f, 0.95f); OnPropertyChanged(); PushVideoParams(); }
    }
    public bool BypassEffects
    {
        get => _bypassEffects;
        set { _bypassEffects = value; OnPropertyChanged(); PushVideoParams(); }
    }
    public bool TestPatternMode
    {
        get => _testPatternMode;
        set { _testPatternMode = value; OnPropertyChanged(); PushVideoParams(); }
    }

    // Beauty & Color properties
    public float SkinSmooth { get => Color.SkinSmooth; set { Color.SkinSmooth = value; OnPropertyChanged(); PushVideoParams(); } }
    public float Denoise { get => Color.Denoise; set { Color.Denoise = value; OnPropertyChanged(); PushVideoParams(); } }
    public float StudioLight { get => Color.StudioLight; set { Color.StudioLight = value; OnPropertyChanged(); PushVideoParams(); } }
    public float Sharpen { get => Color.Sharpen; set { Color.Sharpen = value; OnPropertyChanged(); PushVideoParams(); } }
    public float SoftGlow { get => Color.SoftGlow; set { Color.SoftGlow = value; OnPropertyChanged(); PushVideoParams(); } }
    public float AutoWhiteBalance { get => Color.AutoWhiteBalance; set { Color.AutoWhiteBalance = value; OnPropertyChanged(); PushVideoParams(); } }
    public float Exposure { get => Color.Exposure; set { Color.Exposure = value; OnPropertyChanged(); PushVideoParams(); } }
    public float Brightness { get => Color.Brightness; set { Color.Brightness = value; OnPropertyChanged(); PushVideoParams(); } }
    public float Contrast { get => Color.Contrast; set { Color.Contrast = value; OnPropertyChanged(); PushVideoParams(); } }
    public float Highlights { get => Color.Highlights; set { Color.Highlights = value; OnPropertyChanged(); PushVideoParams(); } }
    public float Shadows { get => Color.Shadows; set { Color.Shadows = value; OnPropertyChanged(); PushVideoParams(); } }
    public float GammaVal { get => Color.GammaVal; set { Color.GammaVal = value; OnPropertyChanged(); PushVideoParams(); } }
    public float Temperature { get => Color.Temperature; set { Color.Temperature = value; OnPropertyChanged(); PushVideoParams(); } }
    public float Tint { get => Color.Tint; set { Color.Tint = value; OnPropertyChanged(); PushVideoParams(); } }
    public float Saturation { get => Color.Saturation; set { Color.Saturation = value; OnPropertyChanged(); PushVideoParams(); } }
    public float Vibrance { get => Color.Vibrance; set { Color.Vibrance = value; OnPropertyChanged(); PushVideoParams(); } }
    public float Vignette { get => Color.Vignette; set { Color.Vignette = value; OnPropertyChanged(); PushVideoParams(); } }

    public FilterPreset? SelectedPreset
    {
        get => _selectedPreset;
        set
        {
            _selectedPreset = value;
            OnPropertyChanged();
            if (value != null) ApplyPreset(value);
        }
    }
    public string NewPresetName { get => _newPresetName; set { _newPresetName = value; OnPropertyChanged(); } }
    public string DiagnosticsText { get => _diagnosticsText; private set { _diagnosticsText = value; OnPropertyChanged(); } }

    public void ToggleLanguage()
    {
        Loc.IsArabic = !Loc.IsArabic;
        OnPropertyChanged(nameof(VCamButtonLabel));
        OnPropertyChanged(nameof(VCamBadgeLabel));
        OnPropertyChanged(nameof(PreviewModeLabel));
        OnPropertyChanged(nameof(PlatformGuidance));
        OnPropertyChanged(nameof(ActiveCameraBadge));
    }

    public void ConnectWifi(string? ipOverride = null, int port = 54820)
    {
        string ip = string.IsNullOrWhiteSpace(ipOverride) ? ManualIp.Trim() : ipOverride.Trim();
        if (string.IsNullOrWhiteSpace(ip)) return;
        ManualIp = ip;
        _engine.Connect(ip, port, PinCode.Trim());
        StatusMessage = Loc.IsArabic ? $"جاري الاتصال بـ {ip}..." : $"Connecting to {ip}...";
    }

    public void ConnectUsb()
    {
        if (_engine.ConnectUsb(PinCode.Trim()))
        {
            StatusMessage = Loc.IsArabic ? "جاري الاتصال عبر كابل USB..." : "Connecting via USB ADB...";
        }
        else
        {
            PollEngineState();
        }
    }

    public void Disconnect()
    {
        _engine.Disconnect();
        IsConnected = false;
        StatusMessage = Loc.IsArabic ? "تم قطع الاتصال" : "Disconnected";
    }

    public void ToggleVirtualCam()
    {
        if (IsVirtualCamActive)
        {
            _engine.StopVirtualCam();
            IsVirtualCamActive = false;
        }
        else
        {
            bool ok = _engine.StartVirtualCam();
            IsVirtualCamActive = ok;
            RefreshDiagnostics();
            if (!ok)
            {
                StatusMessage = Loc.IsArabic
                    ? "تعذر تشغيل الكاميرا الافتراضية — راجع نافذة التشخيص لتسجيل DLL كمسؤول."
                    : "Virtual camera failed to start — open Diagnostics to register COM DLLs as Admin.";
            }
        }
    }

    public void SwitchPhoneFrontBack()
    {
        string target = FrontCameraActive ? "back" : "front";
        _engine.SendControlJson($"{{\"cmd\":\"switch_camera\",\"facing\":\"{target}\"}}");
    }

    public void TogglePhoneTorch()
    {
        TorchEnabled = !TorchEnabled;
        _engine.SendControlJson($"{{\"cmd\":\"set_torch\",\"enabled\":{(TorchEnabled ? "true" : "false")}}}");
    }

    public void ApplyPlatformMirrorPreset(PlatformMirrorPreset preset)
    {
        MirrorManager.ApplyPlatformPreset(preset);
        NotifyMirrorProperties();
        PushVideoParams();
    }

    public void ToggleQuickMirrorHotkey()
    {
        MirrorManager.ToggleOutputFlipH();
        NotifyMirrorProperties();
        PushVideoParams();
    }

    public void Rotate90()
    {
        MirrorManager.RotateClockwise90();
        NotifyMirrorProperties();
        PushVideoParams();
    }

    public void TriggerMirrorCheck5s()
    {
        _mirrorCheckTimer.Stop();
        MirrorCheckActive = true;
        _mirrorCheckTimer.Start();
    }

    public void ApplyPreset(FilterPreset preset)
    {
        Color = preset.Settings.Clone();
        Color.Clamp();
        NotifyColorProperties();
        PushVideoParams();
    }

    public void ResetColors()
    {
        Color = new ColorBeautySettings();
        NotifyColorProperties();
        PushVideoParams();
    }

    public void SaveCurrentAsCustomPreset()
    {
        string name = string.IsNullOrWhiteSpace(NewPresetName)
            ? $"Custom {DateTime.Now:HH:mm}"
            : NewPresetName.Trim();
        var preset = new FilterPreset
        {
            NameAr = name,
            NameEn = name,
            IsBuiltIn = false,
            Settings = Color.Clone()
        };
        Presets.Add(preset);
        PresetCatalog.SaveCustomPresets(Presets);
        NewPresetName = "";
    }

    public void DeleteCustomPreset(FilterPreset? preset)
    {
        if (preset == null || preset.IsBuiltIn) return;
        Presets.Remove(preset);
        PresetCatalog.SaveCustomPresets(Presets);
    }

    public void RefreshDiagnostics()
    {
        string json = _engine.GetVCamDiagnosticsJson();
        try
        {
            using var doc = JsonDocument.Parse(json);
            var r = doc.RootElement;
            bool win11 = r.TryGetProperty("isWindows11OrNewer", out var p1) && p1.GetBoolean();
            bool mfApi = r.TryGetProperty("mfApiAvailable", out var p2) && p2.GetBoolean();
            bool mfReg = r.TryGetProperty("mfComRegistered", out var p3) && p3.GetBoolean();
            bool dsReg = r.TryGetProperty("dsComRegistered", out var p4) && p4.GetBoolean();
            bool warp = r.TryGetProperty("gpuWarpFallback", out var p5) && p5.GetBoolean();
            bool shm = r.TryGetProperty("shmOpen", out var p6) && p6.GetBoolean();
            string backend = r.TryGetProperty("activeBackend", out var p7) ? (p7.GetString() ?? "") : "";
            string err = r.TryGetProperty("lastError", out var p8) ? (p8.GetString() ?? "") : "";

            DiagnosticsText =
                $"OS Build: {(win11 ? "Windows 11+ (MF Virtual Camera Supported)" : "Windows 10 / Legacy (DirectShow Mode)")}\n" +
                $"MFCreateVirtualCamera API: {(mfApi ? "Available" : "Unavailable")}\n" +
                $"PhoneCamMFSource.dll COM (HKLM): {(mfReg ? "Registered ✓" : "NOT Registered ✗")}\n" +
                $"PhoneCamDSFilter.dll COM (HKLM): {(dsReg ? "Registered ✓" : "NOT Registered ✗")}\n" +
                $"Shared Ring Buffer: {(shm ? "Active ✓" : "Inactive")}\n" +
                $"GPU Pipeline: {(warp ? "WARP Software Fallback" : "Direct3D 11 Hardware GPU ✓")}\n" +
                $"Active Backend: {(string.IsNullOrEmpty(backend) ? "Idle" : backend)}\n" +
                (string.IsNullOrEmpty(err) ? "" : $"\nError: {err}");
        }
        catch
        {
            DiagnosticsText = json;
        }
    }

    public void RunAdminRegistration()
    {
        string baseDir = AppContext.BaseDirectory;
        string batPath = Path.Combine(baseDir, "Install-VirtualCam.bat");
        try
        {
            if (File.Exists(batPath))
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = batPath,
                    WorkingDirectory = baseDir,
                    UseShellExecute = true,
                    Verb = "runas"
                })?.WaitForExit(15000);
            }
            else
            {
                string mfDll = Path.Combine(baseDir, "PhoneCamMFSource.dll");
                string dsDll = Path.Combine(baseDir, "PhoneCamDSFilter.dll");
                Process.Start(new ProcessStartInfo
                {
                    FileName = "cmd.exe",
                    Arguments = $"/c regsvr32 /s \"{mfDll}\" & regsvr32 /s \"{dsDll}\"",
                    UseShellExecute = true,
                    Verb = "runas"
                })?.WaitForExit(15000);
            }
            RefreshDiagnostics();
        }
        catch (Exception ex)
        {
            StatusMessage = ex.Message;
        }
    }

    private void LoadAllPresets()
    {
        Presets.Clear();
        foreach (var p in PresetCatalog.BuiltInPresets) Presets.Add(p);
        foreach (var p in PresetCatalog.LoadCustomPresets()) Presets.Add(p);
    }

    private void PushRemoteStreamConfig()
    {
        if (!IsConnected) return;
        var (w, h) = SelectedRes switch
        {
            "480p" => (854, 480),
            "1080p" => (1920, 1080),
            _ => (1280, 720)
        };
        int bitrate = SelectedRes switch
        {
            "480p" => 2_500_000,
            "1080p" => 8_000_000,
            _ => 4_500_000
        };
        _engine.SendControlJson(
            $"{{\"cmd\":\"set_config\",\"width\":{w},\"height\":{h},\"fps\":{SelectedFps},\"bitrate\":{bitrate},\"codec\":\"{SelectedCodec}\"}}");
    }

    private void PushVideoParams()
    {
        var t = MirrorManager.CurrentTransform;
        var (outW, outH) = SelectedRes switch
        {
            "1080p" => (1920, 1080),
            _ => (1280, 720)
        };
        var vp = new PC_VideoParams
        {
            FlipH = t.FlipH ? 1 : 0,
            FlipV = t.FlipV ? 1 : 0,
            ManualRotationDeg = t.ManualRotationDeg,
            AutoRotateFromPhone = t.AutoRotateFromPhone ? 1 : 0,
            FramingMode = t.FramingMode,
            OutputWidth = outW,
            OutputHeight = outH,
            OutputFps = SelectedFps,
            PreviewMirrorLocal = PreviewMirrorLocal ? 1 : 0,
            MirrorCheckOverlay = MirrorCheckActive ? 1 : 0,
            BypassEffects = BypassEffects ? 1 : 0,
            TestPatternMode = TestPatternMode ? 1 : 0,
            Zoom = t.Zoom,
            PanX = t.PanX,
            PanY = t.PanY,
            SkinSmooth = Color.SkinSmooth,
            Denoise = Color.Denoise,
            StudioLight = Color.StudioLight,
            Exposure = Color.Exposure,
            Brightness = Color.Brightness,
            Contrast = Color.Contrast,
            Highlights = Color.Highlights,
            Shadows = Color.Shadows,
            GammaVal = Color.GammaVal,
            Temperature = Color.Temperature,
            Tint = Color.Tint,
            Saturation = Color.Saturation,
            Vibrance = Color.Vibrance,
            Sharpen = Color.Sharpen,
            SoftGlow = Color.SoftGlow,
            Vignette = Color.Vignette,
            AutoWhiteBalance = Color.AutoWhiteBalance,
            SplitComparePos = BeforeAfterEnabled ? SplitComparePos : -1.0f
        };
        _engine.SetVideoParams(vp);
    }

    private void PollEngineState()
    {
        if (!_engine.IsNativeLoaded) return;

        // 1. Refresh discovered phones
        try
        {
            var discJson = _engine.GetDiscoveredDevicesJson();
            var list = JsonSerializer.Deserialize<DiscoveredPhoneItem[]>(discJson, new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
            if (list != null)
            {
                DiscoveredPhones.Clear();
                foreach (var d in list) DiscoveredPhones.Add(d);
            }
        }
        catch { }

        // 2. Refresh connection & telemetry status
        try
        {
            using var doc = JsonDocument.Parse(_engine.GetStatusJson());
            var r = doc.RootElement;
            bool conn = r.TryGetProperty("connected", out var cEl) && cEl.GetBoolean();
            bool connecting = r.TryGetProperty("connecting", out var cgEl) && cgEl.GetBoolean();
            bool auth = r.TryGetProperty("authenticated", out var aEl) && aEl.GetBoolean();
            bool usb = r.TryGetProperty("isUsb", out var uEl) && uEl.GetBoolean();
            double fps = r.TryGetProperty("rxFps", out var fEl) ? fEl.GetDouble() : 0.0;
            double lat = r.TryGetProperty("latencyMs", out var lEl) ? lEl.GetDouble() : 0.0;
            string devName = r.TryGetProperty("deviceName", out var dEl) ? (dEl.GetString() ?? "") : "";
            string err = r.TryGetProperty("lastError", out var eEl) ? (eEl.GetString() ?? "") : "";
            bool front = r.TryGetProperty("frontCameraActive", out var frEl) && frEl.GetBoolean();

            IsConnected = conn;
            IsConnecting = connecting;
            IsUsb = usb;
            FrontCameraActive = front;

            if (!string.IsNullOrEmpty(err))
            {
                StatusMessage = err;
            }
            else if (conn && auth)
            {
                string mode = usb ? "USB" : "Wi-Fi";
                StatusMessage = Loc.IsArabic
                    ? $"متصل ({mode}): {devName}"
                    : $"Connected ({mode}): {devName}";
            }
            else if (conn)
            {
                StatusMessage = Loc.IsArabic ? "جاري التحقق من رمز PIN..." : "Verifying PIN...";
            }
            else if (!connecting)
            {
                StatusMessage = Loc.IsArabic ? "في انتظار اتصال الموبايل" : "Waiting for phone connection";
            }

            TelemetrySummary = $"{(SelectedRes == "1080p" ? "1920x1080" : "1280x720")} • {fps:0.0} FPS • {lat:0} ms";

            if (r.TryGetProperty("cameras", out var camsEl) && camsEl.ValueKind == JsonValueKind.Array && PhoneCameras.Count != camsEl.GetArrayLength())
            {
                PhoneCameras.Clear();
                foreach (var c in camsEl.EnumerateArray())
                {
                    PhoneCameras.Add(new RemoteCameraOption
                    {
                        Id = c.GetProperty("id").GetString() ?? "",
                        Facing = c.GetProperty("facing").GetString() ?? "back",
                        Label = c.GetProperty("label").GetString() ?? "",
                        HasFlash = c.TryGetProperty("hasFlash", out var fl) && fl.GetBoolean(),
                        MaxZoom = c.TryGetProperty("maxZoom", out var mz) ? mz.GetSingle() : 4.0f
                    });
                }
            }
        }
        catch { }
    }

    private void OnNativePreviewFrame(IntPtr bgraData, int width, int height, int strideBytes, IntPtr userData)
    {
        if (bgraData == IntPtr.Zero || width <= 0 || height <= 0) return;
        int totalBytes = strideBytes * height;
        lock (_frameLock)
        {
            if (_frameScratch.Length != totalBytes)
            {
                _frameScratch = new byte[totalBytes];
            }
            System.Runtime.InteropServices.Marshal.Copy(bgraData, _frameScratch, 0, totalBytes);
            _pendingW = width;
            _pendingH = height;
        }

        if (_uiCopyQueued) return;
        _uiCopyQueued = true;
        _dispatcher.BeginInvoke(DispatcherPriority.Render, () =>
        {
            _uiCopyQueued = false;
            lock (_frameLock)
            {
                if (_pendingW <= 0 || _pendingH <= 0 || _frameScratch.Length == 0) return;
                if (_previewBitmap == null || _previewBitmap.PixelWidth != _pendingW || _previewBitmap.PixelHeight != _pendingH)
                {
                    PreviewBitmap = new WriteableBitmap(_pendingW, _pendingH, 96, 96, PixelFormats.Bgra32, null);
                }
                _previewBitmap?.WritePixels(
                    new Int32Rect(0, 0, _pendingW, _pendingH),
                    _frameScratch,
                    _pendingW * 4,
                    0);
            }
        });
    }

    private void NotifyMirrorProperties()
    {
        OnPropertyChanged(nameof(ActivePlatformPreset));
        OnPropertyChanged(nameof(PlatformGuidance));
        OnPropertyChanged(nameof(ActiveCameraBadge));
        OnPropertyChanged(nameof(PreviewMirrorLocal));
        OnPropertyChanged(nameof(PreviewModeLabel));
        OnPropertyChanged(nameof(PerCameraMemoryEnabled));
        OnPropertyChanged(nameof(OutputFlipH));
        OnPropertyChanged(nameof(OutputFlipV));
        OnPropertyChanged(nameof(ManualRotationDeg));
        OnPropertyChanged(nameof(AutoRotateFromPhone));
        OnPropertyChanged(nameof(FramingMode));
        OnPropertyChanged(nameof(DigitalZoom));
        OnPropertyChanged(nameof(PanX));
        OnPropertyChanged(nameof(PanY));
    }

    private void NotifyColorProperties()
    {
        OnPropertyChanged(nameof(SkinSmooth));
        OnPropertyChanged(nameof(Denoise));
        OnPropertyChanged(nameof(StudioLight));
        OnPropertyChanged(nameof(Sharpen));
        OnPropertyChanged(nameof(SoftGlow));
        OnPropertyChanged(nameof(AutoWhiteBalance));
        OnPropertyChanged(nameof(Exposure));
        OnPropertyChanged(nameof(Brightness));
        OnPropertyChanged(nameof(Contrast));
        OnPropertyChanged(nameof(Highlights));
        OnPropertyChanged(nameof(Shadows));
        OnPropertyChanged(nameof(GammaVal));
        OnPropertyChanged(nameof(Temperature));
        OnPropertyChanged(nameof(Tint));
        OnPropertyChanged(nameof(Saturation));
        OnPropertyChanged(nameof(Vibrance));
        OnPropertyChanged(nameof(Vignette));
    }

    private void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    public void Dispose()
    {
        _pollTimer.Stop();
        _mirrorCheckTimer.Stop();
        _engine.Dispose();
    }
}
