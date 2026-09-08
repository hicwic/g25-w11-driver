; SPDX-License-Identifier: GPL-2.0-only
; Optional GeForce NOW bridge component. Elevated: it installs the HIDMaestro
; UMDF driver and registers the g25gfnbridge service. The core driver installer
; (g25-w11-driver.iss) stays per-user and is unaffected.

#define AppName "G25 GeForce NOW Bridge"
#define AppPublisher "hicwic"
#define AppVersion GetEnv("G25_VERSION")
#if AppVersion == ""
  #define AppVersion "0.0.0-local"
#endif
#define PayloadDir GetEnv("G25_GFN_PAYLOAD_DIR")
#if PayloadDir == ""
  #define PayloadDir "..\gfn-bridge\dist"
#endif

[Setup]
AppId={{7F1B3E92-6A4C-4D8E-9B2A-3C5D7E1F0A6B}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\G25 GeForce NOW Bridge
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=g25-gfn-bridge-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayIcon={app}\g25gfnbridge.exe
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel2=This installs the optional GeForce NOW wheel bridge.%n%nIt adds the HIDMaestro virtual HID driver (UMDF, self-signed test certificate) and an on-demand Windows service. The core G25 driver is not changed. Enable the bridge from the G25 Control tray menu once installed.

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
; Install the HIDMaestro driver up front so the first service start is quick.
Filename: "{app}\g25-gfn-wheel-bridge.exe"; Parameters: "install-driver"; \
  StatusMsg: "Installing the HIDMaestro virtual HID driver..."; Flags: runhidden waituntilterminated
; Register the on-demand service (SDDL lets the tray start/stop it).
Filename: "{app}\g25gfnbridge.exe"; Parameters: "install"; \
  StatusMsg: "Registering the GeForce NOW bridge service..."; Flags: runhidden waituntilterminated

[UninstallRun]
Filename: "{app}\g25gfnbridge.exe"; Parameters: "uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveService"
Filename: "{app}\g25-gfn-wheel-bridge.exe"; Parameters: "cleanup"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveVirtualG29"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
begin
  { Advisory: G HUB's logi_win_usb.inf WinUSB-claims the G25 in compat mode. }
  if (CurStep = ssPostInstall) and DirExists(ExpandConstant('{commonpf}\LGHUB')) then
    MsgBox('Logitech G HUB looks installed. Its WinUSB driver can take over the G25 in compatibility mode and stop the wheel working.'
      + #13#10#13#10 + 'If the G25 stops responding, see docs\ghub-coexistence.md (block-ghub-winusb.ps1).',
      mbInformation, MB_OK);
end;
