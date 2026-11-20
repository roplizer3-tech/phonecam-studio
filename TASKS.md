# Tasks — PhoneCam Studio v2.0.0

- [x] **Phase 1 (Latency & 60 FPS Overhaul)**: Low-latency H.264 `MediaCodec` + Camera2 stabilization-off + `WIFI_MODE_FULL_LOW_LATENCY` + GOP-safe queues + decoupled socket worker + DXVA D3D11 MFT decode + dual staging buffers + `<=640x360` preview throttling
- [x] **Phase 2 (Real AI/AR/Stylized Filters)**: ONNX Runtime DirectML (`Selfie Segmentation` + `YuNet` 5-landmark detection) + `pass2/pass3` HLSL real AI backgrounds, Custom WIC Image, Landmark AR glasses/visors/auras, Anime/Sketch/VHS/CyberNeon/Halation shaders, Face Slimming, Eye Enlargement, Eye Brightening, Teeth Whitening & AI Center Stage
- [x] **Phase 3 (USB + Wi-Fi + Bluetooth + Android 15)**: Bundled `adb.exe` platform-tools, USB auth diagnostics, Windows Firewall auto-rules + active `PCAM_PROBE`, Bluetooth Classic RFCOMM server/client (`640x360@20fps`), Android 14/15 native USB UVC detection
- [x] **Phase 4 (Persistent Windows 11 System Virtual Camera)**: `IMFSampleAllocatorControl`, `MF_DEVICEMFT_SENSORPROFILE_COLLECTION`, `PhoneCamVcamSetup.exe` (`MFVirtualCameraLifetime_System` + `KSCATEGORY_VIDEO_CAMERA`)
- [x] **Phase 5 (WPF Studio UI & Installer & CI)**: Dedicated AI/AR FX tab, Bluetooth paired picker, 16 built-in AI/color presets, updated CTest + xUnit + Android tests, CI ONNX/DirectML/ADB/Model downloads, Combined Bundle ZIP release workflow
- [ ] **Phase 6**: Push to `roplizer3-tech/phonecam-studio`, verify Android & Windows CI suites pass completely, publish & verify `v2.0.0` GitHub Release assets
