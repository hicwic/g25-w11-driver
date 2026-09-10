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
; HidHide setup - fetched into the payload dir by CI or
; virtual-g29\scripts\Fetch-HidHide.ps1 (not committed to git).
#define HidHideSetup "HidHide_1.5.230_x64.exe"

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
WelcomeLabel2=This installs the optional Virtual G29 wheel bridge.%n%nIt adds the HIDMaestro virtual HID driver (UMDF, self-signed test certificate), the HidHide device-hiding driver, and an on-demand Windows service. The core G25 driver is not changed. Enable the bridge from the G25 Control tray menu once installed.%n%nGeForce NOW works on its own. Force feedback in local games also needs the core g25-driver installed.%n%nHidHide may ask for a reboot; the bridge works fully after it.

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Excludes: "{#HidHideSetup}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Bundled but not kept on disk - only used to install HidHide during setup.
Source: "{#PayloadDir}\{#HidHideSetup}"; DestDir: "{tmp}"; Flags: deleteafterinstall
; Per-user OEM / force-feedback registration for the virtual G29 (local games).
Source: "..\virtual-g29\scripts\Register-G29FF.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Run]
; Hide the physical G25 from local DirectInput games (does nothing to GeForce NOW).
Filename: "{tmp}\{#HidHideSetup}"; Parameters: "/qn /norestart"; \
  StatusMsg: "Installing HidHide (hides the G25 from local games)..."; \
  Flags: runhidden waituntilterminated; Check: not HidHideInstalled
; Install the HIDMaestro driver up front so the first service start is quick.
Filename: "{app}\g25-virtual-g29.exe"; Parameters: "install-driver"; \
  StatusMsg: "Installing the HIDMaestro virtual HID driver..."; Flags: runhidden waituntilterminated
; Register the on-demand service (SDDL lets the tray start/stop it).
Filename: "{app}\g25vg29.exe"; Parameters: "install"; \
  StatusMsg: "Registering the Virtual G29 bridge service..."; Flags: runhidden waituntilterminated
; Per-user OEM / FFB metadata for the virtual G29 (no-op if the core driver is
; absent - GeForce NOW does not need it).
Filename: "powershell.exe"; \
  Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\Register-G29FF.ps1"" -Action Install"; \
  StatusMsg: "Registering the virtual G29 for local games..."; \
  Flags: runhidden waituntilterminated runasoriginaluser

[UninstallRun]
; Note: the per-user C24F OEM key stays (uninstall is elevated, can't touch the
; user's HKCU). It is inert once the virtual G29 is gone; run
; Register-G29FF.ps1 -Action Uninstall as the user to remove it.
Filename: "{app}\g25vg29.exe"; Parameters: "uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveService"
Filename: "{app}\g25-virtual-g29.exe"; Parameters: "cleanup"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveVirtualG29"

[Code]
function HidHideInstalled: Boolean;
begin
  Result := RegKeyExists(HKLM, 'SOFTWARE\Nefarius Software Solutions e.U.\HidHide') or
            RegKeyExists(HKLM64, 'SOFTWARE\Nefarius Software Solutions e.U.\HidHide');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  { Advisory: G HUB's logi_win_usb.inf WinUSB-claims the G25 in compat mode. }
  if (CurStep = ssPostInstall) and DirExists(ExpandConstant('{commonpf}\LGHUB')) then
    MsgBox('Logitech G HUB looks installed. Its WinUSB driver can take over the G25 in compatibility mode and stop the wheel working.'
      + #13#10#13#10 + 'If the G25 stops responding, open Terminal (Admin) and run:'
      + #13#10 + 'powershell -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\g25ff\scripts\Block-GHubWinUsb.ps1"'
      + #13#10#13#10 + 'Details in docs\ghub-coexistence.md.',
      mbInformation, MB_OK);
end;
