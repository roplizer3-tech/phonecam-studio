using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace PhoneCamStudio.Services;

public sealed class GlobalHotkeyService : IDisposable
{
    private const int WM_HOTKEY = 0x0312;
    private const uint MOD_CONTROL = 0x0002;
    private const uint MOD_SHIFT = 0x0004;
    private const uint MOD_NOREPEAT = 0x4000;
    private const int HOTKEY_MIRROR = 54821;
    private const int HOTKEY_BYPASS = 54822;

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool UnregisterHotKey(IntPtr hWnd, int id);

    private HwndSource? _source;
    private Action? _onToggleMirror;
    private Action? _onToggleBypass;

    public void Attach(Window window, Action onToggleMirror, Action onToggleBypass)
    {
        _onToggleMirror = onToggleMirror;
        _onToggleBypass = onToggleBypass;

        var helper = new WindowInteropHelper(window);
        if (helper.Handle == IntPtr.Zero) return;
        _source = HwndSource.FromHwnd(helper.Handle);
        _source?.AddHook(WndProc);

        // Ctrl+Shift+M -> Instant mirror toggle mid-call
        RegisterHotKey(helper.Handle, HOTKEY_MIRROR, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, (uint)'M');
        // Ctrl+Shift+F -> Instant filter bypass toggle
        RegisterHotKey(helper.Handle, HOTKEY_BYPASS, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, (uint)'F');
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == WM_HOTKEY)
        {
            int id = wParam.ToInt32();
            if (id == HOTKEY_MIRROR)
            {
                _onToggleMirror?.Invoke();
                handled = true;
            }
            else if (id == HOTKEY_BYPASS)
            {
                _onToggleBypass?.Invoke();
                handled = true;
            }
        }
        return IntPtr.Zero;
    }

    public void Dispose()
    {
        if (_source != null)
        {
            UnregisterHotKey(_source.Handle, HOTKEY_MIRROR);
            UnregisterHotKey(_source.Handle, HOTKEY_BYPASS);
            _source.RemoveHook(WndProc);
            _source = null;
        }
    }
}
