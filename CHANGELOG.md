# Changelog

## [2.0.0] - 2026-11-20
### Major Performance, Real AI/AR Filters, Transport & Windows 11 System Camera Upgrade
- **60 FPS Low-Latency Streaming & DXVA Hardware Decode**:
  - Android `MediaCodec`: Constrained Baseline Profile, `KEY_OPERATING_RATE`, `KEY_PRIORITY=0`, Android 11+ `KEY_LOW_LATENCY`, `KEY_MAX_B_FRAMES=0`, Camera2 EIS/OIS stabilization-off fast capture mode, GOP-safe bounded frame queue (`GopSafeFrameQueue`), immediate `out.flush()`, and `WIFI_MODE_FULL_LOW_LATENCY`.
  - Windows `NetworkClient` + `VideoDecoder` + `GpuProcessor`: Dedicated GOP-safe video worker thread decoupled from `recv()`, D3D11 DXVA H.264 MFT hardware decoding (`IMFDXGIBuffer` staging fallback to SW MFT), alternating dual staging buffers, asynchronous `D3D11_MAP_FLAG_DO_NOT_WAIT` 480x270 AI frame extraction, and lightweight `<=640x360` 30 Hz UI preview callback.
- **Real ONNX Runtime DirectML AI, Face Landmarks AR & GPU Shaders**:
  - Real `Selfie Segmentation` (ONNX) + `YuNet` 5-landmark face detection (`face_detection_yunet_2023mar.onnx`) accelerated via **DirectML GPU** with CPU fallback.
  - Real GPU **AI Background Replacement**: DSLR Portrait Bokeh Blur, Modern Studio Office, Warm Bokeh Lights, Cyber Studio Neon, and WIC Custom Background Image loader.
  - Real **Landmark-Anchored AR Filters**: Aviator Sunglasses, Round Glasses, Cyber Visor, Golden Halo Crown, Animated Sparkle Aura.
  - Real **Stylized Pixel Shaders**: Anime / Cartoon Cel-Shading + Sobel Ink Edges, Pencil Sketch, Retro VHS 1995 (chromatic aberration, scanlines, tape tracking), Cyber Neon Edges, Cinema 35mm Halation.
  - Real **AI Face Sculpting & Clarity**: Face Slimming, Natural Eye Enlargement, Eye Brightening, Teeth Whitening, and Closed-Loop AI Center Stage Auto-Framing.
- **USB, Wi-Fi & Bluetooth Classic Connectivity**:
  - Official Google `adb.exe` (`platform-tools`) bundled in Windows installer & Portable ZIP (`tools/adb/`), with live ADB device authorization state reporting.
  - Windows Installer & scripts automatically configure Windows Firewall inbound/outbound rules for TCP `54820` & UDP `54821` alongside active UDP `PCAM_PROBE` discovery.
  - Full **Bluetooth Classic RFCOMM** transport (`BluetoothServer.kt` + `BluetoothClient.cpp`) with automatic low-bandwidth adaptive profile (`640x360 @ 20 FPS, 850kbps`).
  - Android 14 / 15 (`API 34+`) native USB UVC Webcam detection hint (`FEATURE_USB_DEVICE_UVC_CAMERA`).
- **Windows 11 Persistent System Virtual Camera**:
  - Upgraded `PhoneCamMFSource.dll` to implement `IMFSampleAllocatorControl`, `MF_DEVICEMFT_SENSORPROFILE_COLLECTION`, `IMFAttributes`, and `IMFVideoSampleAllocatorEx` NV12/RGB32 60 FPS FrameServer contract.
  - Added `PhoneCamVcamSetup.exe` registering `MFVirtualCameraLifetime_System` + `MFVirtualCameraAccess_AllUsers` under `KSCATEGORY_VIDEO_CAMERA` so `PhoneCam Studio` persists across reboots without an EV/WHQL `.sys` kernel driver.

## [1.0.0] - 2026-11-20
### Initial Release
- **Android Sender (`PhoneCam Sender`)**: Camera2 front/rear/multi-lens streaming, low-latency H.264 MediaCodec + MJPEG fallback, Wi-Fi UDP auto-discovery, SHA-256 4-digit PIN auth, USB ADB support, Foreground Service + OLED Blackout mode.
- **Windows Engine & GPU Shaders (`PhoneCamEngine.dll`)**: Media Foundation H.264 HW decoder + WIC MJPEG, Direct3D 11 4-pass HLSL pipeline.
- **Dual Virtual Cameras**: Windows 11 `MFCreateVirtualCamera` COM source (`PhoneCamMFSource.dll`) + DirectShow Filter (`PhoneCamDSFilter.dll`).
- **Windows Studio UI (`PhoneCamStudio.exe`)**: .NET 8 WPF dark UI (Arabic RTL & English LTR), platform mirror profiles, per-camera memory, 12 filter presets.
- **CI/CD**: Automated GitHub Actions builds, tests, Inno Setup installer, Portable ZIP, and GitHub Release packaging.
