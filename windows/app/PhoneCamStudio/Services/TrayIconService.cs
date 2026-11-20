using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using WinForms = System.Windows.Forms;

namespace PhoneCamStudio.Services;

public sealed class TrayIconService : IDisposable
{
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyIcon(IntPtr hIcon);

    private readonly WinForms.NotifyIcon _notifyIcon;
    private readonly Icon _icon;

    public TrayIconService(
        Action onRestore,
        Action onToggleMirror,
        Action onToggleVirtualCam,
        Action onExit)
    {
        _icon = CreateStudioIcon();
        var menu = new WinForms.ContextMenuStrip();
        menu.Items.Add("Open PhoneCam Studio", null, (_, _) => onRestore());
        menu.Items.Add("Flip Mirror (Ctrl+Shift+M)", null, (_, _) => onToggleMirror());
        menu.Items.Add("Toggle Virtual Camera", null, (_, _) => onToggleVirtualCam());
        menu.Items.Add(new WinForms.ToolStripSeparator());
        menu.Items.Add("Exit", null, (_, _) => onExit());

        _notifyIcon = new WinForms.NotifyIcon
        {
            Icon = _icon,
            Text = "PhoneCam Studio",
            ContextMenuStrip = menu,
            Visible = false
        };
        _notifyIcon.DoubleClick += (_, _) => onRestore();
    }

    public void ShowInTray()
    {
        _notifyIcon.Visible = true;
        _notifyIcon.ShowBalloonTip(
            2000,
            "PhoneCam Studio",
            "Streaming active in background",
            WinForms.ToolTipIcon.Info);
    }

    public void HideTrayIcon()
    {
        _notifyIcon.Visible = false;
    }

    private static Icon CreateStudioIcon()
    {
        using var bmp = new Bitmap(32, 32);
        using var g = Graphics.FromImage(bmp);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Color.Transparent);
        using var bg = new SolidBrush(Color.FromArgb(37, 99, 235));
        g.FillEllipse(bg, 2, 2, 28, 28);
        using var lens = new SolidBrush(Color.FromArgb(16, 185, 129));
        g.FillEllipse(lens, 9, 9, 14, 14);
        using var core = new SolidBrush(Color.White);
        g.FillEllipse(core, 13, 13, 6, 6);
        IntPtr hIcon = bmp.GetHicon();
        try
        {
            return (Icon)Icon.FromHandle(hIcon).Clone();
        }
        finally
        {
            DestroyIcon(hIcon);
        }
    }

    public void Dispose()
    {
        _notifyIcon.Visible = false;
        _notifyIcon.Dispose();
        _icon.Dispose();
    }
}
