@echo off
setlocal EnableExtensions
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting Administrator privileges to register PhoneCam Studio Virtual Camera...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b %errorlevel%
)

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"
set "DATA_DIR=%ProgramData%\PhoneCamStudio"

echo [1/6] Clearing Mark-of-the-Web on binaries...
powershell -NoProfile -Command "Get-ChildItem -LiteralPath '%APP_DIR%' -Include '*.dll','*.exe' -Recurse | Unblock-File -ErrorAction SilentlyContinue"

echo [2/6] Preparing shared frame directory ACLs (%DATA_DIR%)...
if not exist "%DATA_DIR%" mkdir "%DATA_DIR%"
icacls "%DATA_DIR%" /grant "*S-1-5-19:(OI)(CI)(RX)" /grant "*S-1-15-2-1:(OI)(CI)(RX)" /grant "*S-1-5-32-545:(OI)(CI)(M)" /T /C >nul
icacls "%APP_DIR%" /grant "*S-1-5-19:(OI)(CI)(RX)" /grant "*S-1-15-2-1:(OI)(CI)(RX)" /T /C >nul

echo [3/6] Registering Windows 11 Media Foundation Virtual Camera (PhoneCamMFSource.dll)...
regsvr32 /s "%APP_DIR%\PhoneCamMFSource.dll"
if %errorlevel% neq 0 (
    echo [WARN] PhoneCamMFSource.dll registration returned code %errorlevel%.
) else (
    echo [OK] PhoneCamMFSource.dll registered.
)

echo [4/6] Registering Universal DirectShow Virtual Camera (PhoneCamDSFilter.dll)...
regsvr32 /s "%APP_DIR%\PhoneCamDSFilter.dll"
if %errorlevel% neq 0 (
    echo [WARN] PhoneCamDSFilter.dll registration returned code %errorlevel%.
) else (
    echo [OK] PhoneCamDSFilter.dll registered.
)

echo [5/6] Registering Persistent System Virtual Camera on Windows 11...
if exist "%APP_DIR%\PhoneCamVcamSetup.exe" (
    "%APP_DIR%\PhoneCamVcamSetup.exe" --install
)

echo [6/6] Adding Windows Firewall rules for PhoneCam Studio (Wi-Fi / Discovery)...
netsh advfirewall firewall delete rule name="PhoneCam Studio TCP" >nul 2>&1
netsh advfirewall firewall delete rule name="PhoneCam Studio UDP Discovery" >nul 2>&1
netsh advfirewall firewall delete rule name="PhoneCam Studio Outbound" >nul 2>&1
netsh advfirewall firewall add rule name="PhoneCam Studio TCP" dir=in action=allow protocol=TCP localport=54820 program="%APP_DIR%\PhoneCamStudio.exe" enable=yes >nul
netsh advfirewall firewall add rule name="PhoneCam Studio UDP Discovery" dir=in action=allow protocol=UDP localport=54821 program="%APP_DIR%\PhoneCamStudio.exe" enable=yes >nul
netsh advfirewall firewall add rule name="PhoneCam Studio Outbound" dir=out action=allow program="%APP_DIR%\PhoneCamStudio.exe" enable=yes >nul

echo.
echo PhoneCam Studio Virtual Camera & Firewall configuration complete!
pause
