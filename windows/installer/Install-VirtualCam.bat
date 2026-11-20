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

echo [1/4] Clearing Mark-of-the-Web on DLLs...
powershell -NoProfile -Command "Get-ChildItem -LiteralPath '%APP_DIR%' -Filter '*.dll' | Unblock-File -ErrorAction SilentlyContinue"

echo [2/4] Preparing shared frame directory ACLs (%DATA_DIR%)...
if not exist "%DATA_DIR%" mkdir "%DATA_DIR%"
icacls "%DATA_DIR%" /grant "*S-1-5-19:(OI)(CI)(RX)" /grant "*S-1-15-2-1:(OI)(CI)(RX)" /grant "*S-1-5-32-545:(OI)(CI)(M)" /T /C >nul
icacls "%APP_DIR%" /grant "*S-1-5-19:(OI)(CI)(RX)" /grant "*S-1-15-2-1:(OI)(CI)(RX)" /T /C >nul

echo [3/4] Registering Windows 11 Media Foundation Virtual Camera (PhoneCamMFSource.dll)...
regsvr32 /s "%APP_DIR%\PhoneCamMFSource.dll"
if %errorlevel% neq 0 (
    echo [WARN] PhoneCamMFSource.dll registration returned code %errorlevel%.
) else (
    echo [OK] PhoneCamMFSource.dll registered.
)

echo [4/4] Registering Universal DirectShow Virtual Camera (PhoneCamDSFilter.dll)...
regsvr32 /s "%APP_DIR%\PhoneCamDSFilter.dll"
if %errorlevel% neq 0 (
    echo [WARN] PhoneCamDSFilter.dll registration returned code %errorlevel%.
) else (
    echo [OK] PhoneCamDSFilter.dll registered.
)

echo.
echo PhoneCam Studio Virtual Camera registration complete!
pause
