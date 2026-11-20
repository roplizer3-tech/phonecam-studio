# Changelog

## [1.0.0] - 2026-11-20
### Initial Release
- **Android Sender (`PhoneCam Sender`)**: Camera2 front/rear/multi-lens streaming, low-latency H.264 MediaCodec + MJPEG fallback, Wi-Fi UDP auto-discovery, SHA-256 4-digit PIN auth, USB ADB support, Foreground Service + OLED Blackout mode.
- **Windows Engine & GPU Shaders (`PhoneCamEngine.dll`)**: Media Foundation H.264 HW decoder + WIC MJPEG, Direct3D 11 4-pass HLSL pipeline (Geometry/Flip/Rotate/Smart Framing, YCbCr Bilateral Skin Smoothing + Studio Key-Light + Denoise, Pro Color Grading + Eye Clarity + Mirror Check Overlay, BT.709 NV12 output).
- **Dual Virtual Cameras**: Windows 11 `MFCreateVirtualCamera` COM source (`PhoneCamMFSource.dll`) for WhatsApp Desktop UWP / Teams / Telegram / Chrome / OBS + DirectShow Filter (`PhoneCamDSFilter.dll`) for Windows 10 & universal fallback.
- **Windows Studio UI (`PhoneCamStudio.exe`)**: .NET 8 WPF dark UI (Arabic RTL & English LTR), platform mirror profiles (WhatsApp, Telegram, Zoom/Meet), per-camera mirror memory, global hotkey `Ctrl+Shift+M`, before/after split compare, 12 filter presets + custom presets, system tray, diagnostics.
- **CI/CD**: Automated GitHub Actions builds, tests, Inno Setup installer, Portable ZIP, and GitHub Release packaging.
