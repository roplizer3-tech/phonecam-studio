using System;
using System.Runtime.InteropServices;
using System.Text;

namespace PhoneCamStudio.Interop;

[StructLayout(LayoutKind.Sequential)]
public struct PC_VideoParams
{
    public int FlipH;
    public int FlipV;
    public int ManualRotationDeg;
    public int AutoRotateFromPhone;
    public int FramingMode;
    public int OutputWidth;
    public int OutputHeight;
    public int OutputFps;
    public int PreviewMirrorLocal;
    public int MirrorCheckOverlay;
    public int BypassEffects;
    public int TestPatternMode;

    public float Zoom;
    public float PanX;
    public float PanY;
    public float SkinSmooth;
    public float Denoise;
    public float StudioLight;
    public float Exposure;
    public float Brightness;
    public float Contrast;
    public float Highlights;
    public float Shadows;
    public float GammaVal;
    public float Temperature;
    public float Tint;
    public float Saturation;
    public float Vibrance;
    public float Sharpen;
    public float SoftGlow;
    public float Vignette;
    public float AutoWhiteBalance;
    public float SplitComparePos;
}

[UnmanagedFunctionPointer(CallingConvention.StdCall)]
public delegate void PreviewFrameCallback(
    IntPtr bgraData,
    int width,
    int height,
    int strideBytes,
    IntPtr userData);

internal static class NativeMethods
{
    private const string DllName = "PhoneCamEngine.dll";

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
    internal static extern IntPtr pc_create();

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
    internal static extern void pc_destroy(IntPtr handle);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
    internal static extern int pc_start_discovery(IntPtr handle);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
    internal static extern int pc_get_discovered_devices_json(IntPtr handle, StringBuilder outBuf, int bufSize);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    internal static extern int pc_connect(IntPtr handle, string ipUtf8, int port, string pinUtf8);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    internal static extern int pc_connect_usb(IntPtr handle, string pinUtf8);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
    internal static extern void pc_disconnect(IntPtr handle);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
    internal static extern int pc_get_status_json(IntPtr handle, StringBuilder outBuf, int bufSize);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    internal static extern int pc_send_control_json(IntPtr handle, string jsonUtf8);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
    internal static extern void pc_set_video_params(IntPtr handle, ref PC_VideoParams param);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
    internal static extern int pc_start_virtual_cam(IntPtr handle);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
    internal static extern void pc_stop_virtual_cam(IntPtr handle);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
    internal static extern int pc_get_vcam_diagnostics_json(IntPtr handle, StringBuilder outBuf, int bufSize);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
    internal static extern void pc_set_preview_callback(IntPtr handle, PreviewFrameCallback? cb, IntPtr userData);

    [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
    internal static extern int pc_push_test_frame(IntPtr handle);
}

public sealed class EngineSession : IDisposable
{
    private IntPtr _handle;
    private PreviewFrameCallback? _pinnedCallback;

    public bool IsNativeLoaded => _handle != IntPtr.Zero;
    public string? LoadError { get; private set; }

    public EngineSession()
    {
        try
        {
            _handle = NativeMethods.pc_create();
        }
        catch (Exception ex) when (ex is DllNotFoundException or BadImageFormatException)
        {
            _handle = IntPtr.Zero;
            LoadError = ex.Message;
        }
    }

    public bool StartDiscovery() =>
        _handle != IntPtr.Zero && NativeMethods.pc_start_discovery(_handle) != 0;

    public string GetDiscoveredDevicesJson()
    {
        if (_handle == IntPtr.Zero) return "[]";
        var sb = new StringBuilder(8192);
        NativeMethods.pc_get_discovered_devices_json(_handle, sb, sb.Capacity);
        return sb.ToString();
    }

    public bool Connect(string ip, int port, string pin) =>
        _handle != IntPtr.Zero && NativeMethods.pc_connect(_handle, ip, port, pin) != 0;

    public bool ConnectUsb(string pin) =>
        _handle != IntPtr.Zero && NativeMethods.pc_connect_usb(_handle, pin) != 0;

    public void Disconnect()
    {
        if (_handle != IntPtr.Zero) NativeMethods.pc_disconnect(_handle);
    }

    public string GetStatusJson()
    {
        if (_handle == IntPtr.Zero) return "{}";
        var sb = new StringBuilder(8192);
        NativeMethods.pc_get_status_json(_handle, sb, sb.Capacity);
        return sb.ToString();
    }

    public bool SendControlJson(string json) =>
        _handle != IntPtr.Zero && NativeMethods.pc_send_control_json(_handle, json) != 0;

    public void SetVideoParams(PC_VideoParams p)
    {
        if (_handle != IntPtr.Zero) NativeMethods.pc_set_video_params(_handle, ref p);
    }

    public bool StartVirtualCam() =>
        _handle != IntPtr.Zero && NativeMethods.pc_start_virtual_cam(_handle) != 0;

    public void StopVirtualCam()
    {
        if (_handle != IntPtr.Zero) NativeMethods.pc_stop_virtual_cam(_handle);
    }

    public string GetVCamDiagnosticsJson()
    {
        if (_handle == IntPtr.Zero) return "{}";
        var sb = new StringBuilder(4096);
        NativeMethods.pc_get_vcam_diagnostics_json(_handle, sb, sb.Capacity);
        return sb.ToString();
    }

    public void SetPreviewCallback(PreviewFrameCallback? cb)
    {
        if (_handle == IntPtr.Zero) return;
        _pinnedCallback = cb;
        NativeMethods.pc_set_preview_callback(_handle, cb, IntPtr.Zero);
    }

    public bool PushTestFrame() =>
        _handle != IntPtr.Zero && NativeMethods.pc_push_test_frame(_handle) != 0;

    public void Dispose()
    {
        if (_handle != IntPtr.Zero)
        {
            NativeMethods.pc_set_preview_callback(_handle, null, IntPtr.Zero);
            _pinnedCallback = null;
            NativeMethods.pc_destroy(_handle);
            _handle = IntPtr.Zero;
        }
    }
}
