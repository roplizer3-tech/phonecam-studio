@echo off
setlocal EnableExtensions
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting Administrator privileges to unregister PhoneCam Studio Virtual Camera...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b %errorlevel%
)

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

echo Removing Persistent System Virtual Camera...
if exist "%APP_DIR%\PhoneCamVcamSetup.exe" "%APP_DIR%\PhoneCamVcamSetup.exe" --uninstall

echo Unregistering PhoneCamMFSource.dll...
if exist "%APP_DIR%\PhoneCamMFSource.dll" regsvr32 /u /s "%APP_DIR%\PhoneCamMFSource.dll"

echo Unregistering PhoneCamDSFilter.dll...
if exist "%APP_DIR%\PhoneCamDSFilter.dll" regsvr32 /u /s "%APP_DIR%\PhoneCamDSFilter.dll"

echo Removing Windows Firewall rules...
netsh advfirewall firewall delete rule name="PhoneCam Studio TCP" >nul 2>&1
netsh advfirewall firewall delete rule name="PhoneCam Studio UDP Discovery" >nul 2>&1
netsh advfirewall firewall delete rule name="PhoneCam Studio Outbound" >nul 2>&1

echo PhoneCam Studio Virtual Camera unregistered.
pause
