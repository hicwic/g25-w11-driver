; SPDX-License-Identifier: GPL-2.0-only
; Optional Virtual G29 bridge component. Elevated: it installs the HIDMaestro
; UMDF driver and registers the g25vg29 service. The core driver installer
; (g25-w11-driver.iss) stays per-user and is unaffected.

#define AppName "G25 Virtual G29"
#define AppPublisher "hicwic"
#define AppVersion GetEnv("G25_VERSION")
#if AppVersion == ""
  #define AppVersion "0.0.0-local"
#endif
#define PayloadDir GetEnv("G25_VG29_PAYLOAD_DIR")
#if PayloadDir == ""
  #define PayloadDir "..\virtual-g29\dist"
#endif

[Setup]
AppId={{1D9C4A17-8E52-4B3F-A6D1-2F7B0C954E88}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\G25 Virtual G29
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=g25-virtual-g29-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayIcon={app}\g25vg29.exe
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel2=This installs the optional Virtual G29 wheel bridge.%n%nIt adds the HIDMaestro virtual HID driver (UMDF, self-signed test certificate) and an on-demand Windows service. The core G25 driver is not changed. Enable the bridge from the G25 Control tray menu once installed.

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
; Install the HIDMaestro driver up front so the first service start is quick.
Filename: "{app}\g25-virtual-g29.exe"; Parameters: "install-driver"; \
  StatusMsg: "Installing the HIDMaestro virtual HID driver..."; Flags: runhidden waituntilterminated
; Register the on-demand service (SDDL lets the tray start/stop it).
Filename: "{app}\g25vg29.exe"; Parameters: "install"; \
  StatusMsg: "Registering the Virtual G29 bridge service..."; Flags: runhidden waituntilterminated

[UninstallRun]
Filename: "{app}\g25vg29.exe"; Parameters: "uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveService"
Filename: "{app}\g25-virtual-g29.exe"; Parameters: "cleanup"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveVirtualG29"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
begin
  { Advisory: G HUB's logi_win_usb.inf WinUSB-claims the G25 in compat mode. }
  if (CurStep = ssPostInstall) and DirExists(ExpandConstant('{commonpf}\LGHUB')) then
    MsgBox('Logitech G HUB looks installed. Its WinUSB driver can take over the G25 in compatibility mode and stop the wheel working.'
      + #13#10#13#10 + 'If the G25 stops responding, see docs\ghub-coexistence.md (block-ghub-winusb.ps1).',
      mbInformation, MB_OK);
end;
