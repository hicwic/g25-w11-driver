; SPDX-License-Identifier: GPL-2.0-only
#define AppName "G25 Windows 11 Driver"
#define AppPublisher "hicwic"
#define AppVersion GetEnv("G25_VERSION")
#if AppVersion == ""
  #define AppVersion "0.1.0-local"
#endif
#define PackageDir GetEnv("G25_PACKAGE_DIR")
#if PackageDir == ""
  #define PackageDir "..\dist\g25-w11-driver-" + AppVersion
#endif

[Setup]
AppId={{9D54B48E-CB5D-42B5-B214-4C66B2D6F41D}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={localappdata}\g25ff
DefaultGroupName=G25 Windows 11 Driver
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=g25-w11-driver-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64
UninstallDisplayIcon={app}\bin\g25tray.exe
WizardStyle=modern

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#PackageDir}\bin\x64\g25ff.dll"; DestDir: "{app}\bin\x64"; Flags: ignoreversion
Source: "{#PackageDir}\bin\x64\g25tool.exe"; DestDir: "{app}\bin\x64"; Flags: ignoreversion
Source: "{#PackageDir}\bin\x86\g25ff.dll"; DestDir: "{app}\bin\x86"; Flags: ignoreversion
Source: "{#PackageDir}\bin\x86\g25tool.exe"; DestDir: "{app}\bin\x86"; Flags: ignoreversion
Source: "{#PackageDir}\bin\g25tray.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#PackageDir}\scripts\Register-G25FF.ps1"; DestDir: "{app}\scripts"; Flags: ignoreversion
Source: "{#PackageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\manifest.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackageDir}\assets\g25-control.ico"; DestDir: "{app}\assets"; Flags: ignoreversion

[Icons]
Name: "{group}\G25 Control"; Filename: "{app}\bin\g25tray.exe"
Name: "{group}\Documentation"; Filename: "{app}\README.md"

[Run]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\scripts\Register-G25FF.ps1"" -Action Install -Dll64 ""{app}\bin\x64\g25ff.dll"" -Dll32 ""{app}\bin\x86\g25ff.dll"" -Tray ""{app}\bin\g25tray.exe"""; Flags: runhidden waituntilterminated

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\scripts\Register-G25FF.ps1"" -Action Uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterG25FF"
