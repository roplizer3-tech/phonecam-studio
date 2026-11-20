# Development & Build Guide

## Prerequisites
- **Android:** JDK 17, Android SDK (Compile/Target API 35, Min API 26), Gradle 8.10.2.
- **Windows Native:** Visual Studio 2022 (MSVC v143 / C++20), CMake >= 3.22, Windows 11 SDK (`>= 10.0.22000.0`).
- **Windows App:** .NET 8 SDK (`net8.0-windows`, `win-x64`).
- **Installer:** Inno Setup 6 (`ISCC.exe`).

## Local / CI Commands

### Android
```bash
cd android
gradle testDebugUnitTest lintDebug assembleDebug assembleRelease
```

### Windows Native C++ & Tests
```powershell
# Visual Studio 2026 ("Visual Studio 18 2026") or Visual Studio 2022 ("Visual Studio 17 2022")
cmake -S windows/native -B windows/native/build -G "Visual Studio 18 2026" -A x64 -DCMAKE_SYSTEM_VERSION=10.0.26100.0
cmake --build windows/native/build --config Release --parallel
ctest --test-dir windows/native/build -C Release --output-on-failure
```

### Windows WPF & Tests
```powershell
dotnet test windows/app/PhoneCamStudio.Tests/PhoneCamStudio.Tests.csproj -c Release
dotnet publish windows/app/PhoneCamStudio/PhoneCamStudio.csproj -c Release -r win-x64 --self-contained true -o dist/PhoneCamStudio
```

### Installer
```powershell
iscc /DMyAppVersion=1.0.0 windows/installer/PhoneCamStudio.iss
```
