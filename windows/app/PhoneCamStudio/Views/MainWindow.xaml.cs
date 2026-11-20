using System;
using System.Windows;
using System.Windows.Controls;
using PhoneCamStudio.Models;
using PhoneCamStudio.Services;
using PhoneCamStudio.ViewModels;

namespace PhoneCamStudio.Views;

public partial class MainWindow : Window
{
    private readonly MainViewModel _vm;
    private readonly GlobalHotkeyService _hotkeys = new();
    private readonly TrayIconService _tray;
    private bool _reallyExiting;

    public MainWindow()
    {
        InitializeComponent();
        _vm = new MainViewModel(Dispatcher);
        DataContext = _vm;

        _tray = new TrayIconService(
            onRestore: () => Dispatcher.Invoke(RestoreFromTray),
            onToggleMirror: () => Dispatcher.Invoke(_vm.ToggleQuickMirrorHotkey),
            onToggleVirtualCam: () => Dispatcher.Invoke(_vm.ToggleVirtualCam),
            onExit: () => Dispatcher.Invoke(() =>
            {
                _reallyExiting = true;
                Close();
            }));

        SourceInitialized += (_, _) =>
        {
            _hotkeys.Attach(
                this,
                onToggleMirror: _vm.ToggleQuickMirrorHotkey,
                onToggleBypass: () => _vm.BypassEffects = !_vm.BypassEffects);
        };

        Closing += (_, e) =>
        {
            if (!_reallyExiting && _vm.IsVirtualCamActive)
            {
                e.Cancel = true;
                MinimizeToTray();
                return;
            }
            _hotkeys.Dispose();
            _tray.Dispose();
            _vm.Dispose();
        };
    }

    private void MinimizeToTray()
    {
        Hide();
        _tray.ShowInTray();
    }

    private void RestoreFromTray()
    {
        _tray.HideTrayIcon();
        Show();
        WindowState = WindowState.Normal;
        Activate();
    }

    private void OnToggleVirtualCamClick(object sender, RoutedEventArgs e) => _vm.ToggleVirtualCam();
    private void OnToggleLanguageClick(object sender, RoutedEventArgs e) => _vm.ToggleLanguage();
    private void OnMinimizeToTrayClick(object sender, RoutedEventArgs e) => MinimizeToTray();

    private void OnOpenDiagnosticsClick(object sender, RoutedEventArgs e)
    {
        _vm.RefreshDiagnostics();
        DiagnosticsOverlay.Visibility = Visibility.Visible;
    }

    private void OnCloseDiagnosticsClick(object sender, RoutedEventArgs e) =>
        DiagnosticsOverlay.Visibility = Visibility.Collapsed;

    private void OnRunAdminRegClick(object sender, RoutedEventArgs e) => _vm.RunAdminRegistration();

    private void OnConnectDiscoveredClick(object sender, RoutedEventArgs e)
    {
        if (sender is System.Windows.Controls.Button b && b.Tag is DiscoveredPhoneItem item)
        {
            _vm.ConnectWifi(item.Ip, item.TcpPort);
        }
    }

    private void OnConnectUsbClick(object sender, RoutedEventArgs e) => _vm.ConnectUsb();
    private void OnConnectManualClick(object sender, RoutedEventArgs e) => _vm.ConnectWifi();
    private void OnDisconnectClick(object sender, RoutedEventArgs e) => _vm.Disconnect();

    private void OnSwitchCameraClick(object sender, RoutedEventArgs e) => _vm.SwitchPhoneFrontBack();
    private void OnToggleTorchClick(object sender, RoutedEventArgs e) => _vm.TogglePhoneTorch();

    private void OnSetResClick(object sender, RoutedEventArgs e)
    {
        if (sender is System.Windows.Controls.Button b && b.Tag is string res)
            _vm.SelectedRes = res;
    }

    private void OnSetFpsClick(object sender, RoutedEventArgs e)
    {
        if (sender is System.Windows.Controls.Button b && int.TryParse(b.Tag?.ToString(), out int fps))
            _vm.SelectedFps = fps;
    }

    private void OnSetCodecClick(object sender, RoutedEventArgs e)
    {
        if (sender is System.Windows.Controls.Button b && b.Tag is string codec)
            _vm.SelectedCodec = codec;
    }

    private void OnPlatformPresetClick(object sender, RoutedEventArgs e)
    {
        if (sender is System.Windows.Controls.Button b && Enum.TryParse<PlatformMirrorPreset>(b.Tag?.ToString(), out var preset))
            _vm.ApplyPlatformMirrorPreset(preset);
    }

    private void OnFramingModeClick(object sender, RoutedEventArgs e)
    {
        if (sender is System.Windows.Controls.Button b && int.TryParse(b.Tag?.ToString(), out int mode))
            _vm.FramingMode = mode;
    }

    private void OnRotate90Click(object sender, RoutedEventArgs e) => _vm.Rotate90();
    private void OnMirrorCheckClick(object sender, RoutedEventArgs e) => _vm.TriggerMirrorCheck5s();
    private void OnResetColorsClick(object sender, RoutedEventArgs e) => _vm.ResetColors();
    private void OnSavePresetClick(object sender, RoutedEventArgs e) => _vm.SaveCurrentAsCustomPreset();
    private void OnDeletePresetClick(object sender, RoutedEventArgs e) => _vm.DeleteCustomPreset(_vm.SelectedPreset);
}
