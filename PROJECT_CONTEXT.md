# Project Context — PhoneCam Studio

## Goal
Build and ship two coordinated applications via GitHub Actions (`roplizer3-tech/phonecam-studio`, public repo):
1. **PhoneCam Sender (Android)**: Streams Front/Rear/Multi-lens camera feeds at low latency (Camera2 + MediaCodec H.264 / MJPEG fallback) over Wi-Fi or USB (ADB).
2. **PhoneCam Studio (Windows 11/10 x64)**: Receives video, decodes via Windows Media Foundation/WIC, applies real-time Direct3D 11 HLSL GPU filters (beauty/skin smooth, studio lighting, color grading, mirror/rotation/framing fixes), and outputs to system-wide virtual cameras visible in WhatsApp Desktop UWP, Telegram Desktop, Zoom, Teams, Discord, Chrome/Edge, and OBS.

## Target Environment & Constraints
- **Primary Target OS:** Windows 11 x64 (`MFCreateVirtualCamera` via `PhoneCamMFSource.dll`).
- **Secondary Target OS:** Windows 10 x64 & universal DirectShow compatibility (`PhoneCamDSFilter.dll`).
- **Build Environment:** Offloaded to GitHub Actions (`ubuntu-latest` for Android APK, `windows-latest` for C++20/MSVC/Windows 11 SDK + .NET 8 WPF + Inno Setup).
- **Local Container:** Debian 12 (`2.00 GiB` RAM, `2.0 vCPU`) used for authoring, git management, GitHub Actions orchestration, and artifact verification.

## Fixed Cross-Module Contracts
- TCP Stream & Control Port: `54820`
- UDP Discovery Port: `54821`
- Packet Magic: `0x5043414D` (`"PCAM"`), Max Payload: `8 MiB`
- Shared Memory Path: `%ProgramData%\PhoneCamStudio\vcam_ring.bin`
- Shared Memory Magic: `0x50435356` (`"PCSV"`), `3` NV12 slots (`1920x1080` max, `3 MiB` slot stride)
- `CLSID_PhoneCamMFActivator`: `{7A9E4B21-6C3D-4F18-9E2A-51B8C4D730F1}`
- `CLSID_PhoneCamDSFilter`: `{8B1F5C32-7D4E-4A29-8F3B-62C9D5E841A2}`
- Virtual Camera Friendly Names: `PhoneCam Studio` (MF), `PhoneCam Studio (DirectShow)` (DS)
