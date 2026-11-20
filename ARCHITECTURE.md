# Architecture — PhoneCam Studio

## Component Breakdown

### 1. Android Sender (`android/`)
- **Camera2 Engine (`CameraStreamEngine.kt`)**: Uses `TEMPLATE_RECORD` with dual surfaces (local preview `TextureView` + `MediaCodec` surface for zero-copy H.264 encoding, or `ImageReader` for MJPEG fallback). Enumerates all front/back/multi-lens cameras via `CameraCatalog.kt`.
- **Network & Discovery (`StreamServer.kt`, `DiscoveryBroadcaster.kt`)**:
  - TCP server on port `54820` (`TCP_NODELAY`), SHA-256 PIN challenge-response, bounded queue (drops stale video frames on backpressure; preserves SPS/PPS codec config).
  - UDP discovery beacon on port `54821` every 1.5 seconds across active IPv4 subnets (Wi-Fi + USB tethering).
- **Foreground Service (`CameraForegroundService.kt`)**: Keeps Camera2 + TCP server alive when dimmed or switched to OLED Blackout mode.

### 2. Windows Native Core (`windows/native/`)
- **`common/`**:
  - `Protocol.h`: 16-byte wire header + bounds validation.
  - `SharedFrameBuffer`: File-backed triple-buffered lock-free ring buffer (`%ProgramData%\PhoneCamStudio\vcam_ring.bin`) accessible across Session 1 user UI, Session 0 `FrameServer` (`NT AUTHORITY\LOCAL SERVICE`), and AppContainer browsers (`*S-1-15-2-1`).
  - `ColorConvert`: NV12 ↔ RGB32/RGB24/YUY2 conversions, NV12 scaler, and procedural `StandbyRenderer`.
- **`engine/` (`PhoneCamEngine.dll`)**:
  - `NetworkClient`: Winsock2 TCP/UDP client + Windows CNG SHA-256 auth + latency ping/pong.
  - `AdbHelper`: Locates `adb.exe` and configures `adb forward tcp:54820 tcp:54820`.
  - `VideoDecoder`: `CLSID_MSH264DecoderMFT` (low-latency NV12 output with display aperture cropping) + WIC MJPEG decoder.
  - `GpuProcessor`: Direct3D 11 (`HARDWARE` with `WARP` fallback) executing 4 HLSL passes (`pass1_geom_ps.hlsl` → `pass2_beauty_ps.hlsl` → `pass3_grade_ps.hlsl` → `pass4_nv12_ps.hlsl`).
  - `VirtualCamManager`: Dynamically loads `mfsensorgroup.dll` (`MFCreateVirtualCamera`) so `PhoneCamEngine.dll` remains loadable on Windows 10 while enabling native Win11 MF virtual camera.
  - `EngineApi.h`: Clean C-ABI (`P/Invoke`) exported to WPF.
- **`vcam_mf/` (`PhoneCamMFSource.dll`)**:
  - In-proc COM server implementing `IMFActivate`, `IMFMediaSourceEx`, `IMFMediaStream2`, `IKsControl` registered under `HKLM` for `MFCreateVirtualCamera`.
  - Exposes `MFVideoFormat_NV12` (preferred by WhatsApp/Teams) and `MFVideoFormat_RGB32` using CPU-safe `MFCreateMemoryBuffer` samples.
- **`vcam_dshow/` (`PhoneCamDSFilter.dll`)**:
  - Self-contained DirectShow source filter (`IBaseFilter`, `IPin`, `IAMStreamConfig`, `IKsPropertySet` returning `PIN_CATEGORY_CAPTURE`) registered under `CLSID_VideoInputDeviceCategory`.

### 3. Windows WPF Studio (`windows/app/`)
- `.NET 8` WPF (`x64`, self-contained publish) with Arabic RTL (`FlowDirection.RightToLeft`) default & English LTR toggle.
- Implements Platform Mirror Profiles (`WhatsApp`, `Telegram`, `Zoom/Meet/Teams`, `Raw`), Per-Camera Memory (Front vs Back), Before/After split preview, Mirror Check overlay trigger, Global Hotkeys (`Ctrl+Shift+M`, `Ctrl+Shift+F`), System Tray mode, 12 GPU Filter Presets + custom JSON presets, and Virtual Camera diagnostics.
