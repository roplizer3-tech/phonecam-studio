#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif

[Setup]
AppId={{9F2A8C14-4E5D-4B21-A3E8-71C9D4F85201}
AppName=PhoneCam Studio
AppVersion={#MyAppVersion}
AppPublisher=roplizer3-tech
AppPublisherURL=https://github.com/roplizer3-tech/phonecam-studio
DefaultDirName={autopf}\PhoneCamStudio
DefaultGroupName=PhoneCam Studio
AllowNoIcons=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\dist
OutputBaseFilename=PhoneCamStudio-Setup-x64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\..\dist\PhoneCamStudio\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Dirs]
Name: "{commonappdata}\PhoneCamStudio"

[Icons]
Name: "{group}\PhoneCam Studio"; Filename: "{app}\PhoneCamStudio.exe"
Name: "{group}\{cm:UninstallProgram,PhoneCam Studio}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\PhoneCam Studio"; Filename: "{app}\PhoneCamStudio.exe"; Tasks: desktopicon

[Run]
Filename: "icacls.exe"; Parameters: """{commonappdata}\PhoneCamStudio"" /grant ""*S-1-5-19:(OI)(CI)(RX)"" /grant ""*S-1-15-2-1:(OI)(CI)(RX)"" /grant ""*S-1-5-32-545:(OI)(CI)(M)"" /T /C"; Flags: runhidden waituntilterminated
Filename: "icacls.exe"; Parameters: """{app}"" /grant ""*S-1-5-19:(OI)(CI)(RX)"" /grant ""*S-1-15-2-1:(OI)(CI)(RX)"" /T /C"; Flags: runhidden waituntilterminated
Filename: "regsvr32.exe"; Parameters: "/s ""{app}\PhoneCamMFSource.dll"""; Flags: runhidden waituntilterminated
Filename: "regsvr32.exe"; Parameters: "/s ""{app}\PhoneCamDSFilter.dll"""; Flags: runhidden waituntilterminated
Filename: "{app}\PhoneCamStudio.exe"; Description: "{cm:LaunchProgram,PhoneCam Studio}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
Filename: "regsvr32.exe"; Parameters: "/u /s ""{app}\PhoneCamMFSource.dll"""; Flags: runhidden waituntilterminated; RunOnceId: "UnregPhoneCamMF"
Filename: "regsvr32.exe"; Parameters: "/u /s ""{app}\PhoneCamDSFilter.dll"""; Flags: runhidden waituntilterminated; RunOnceId: "UnregPhoneCamDS"
