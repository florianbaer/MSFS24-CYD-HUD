; Inno Setup script for MsfsCydHud-Setup.exe.
;
; Built by CI (job "setup" in .github/workflows/ci.yml), or on Windows with:
;   powershell -File installer\make_setup.ps1 -Firmware <merged .bin>
; which stages the files and runs:
;   iscc /DAppVersion=<x.y.z> /DStage=<staging dir> /DOutDir=<dir> installer\MsfsCydHud.iss
;
; Setup itself only copies files (per user, no admin rights) and then starts the
; guided setup, installer\install.ps1, which flashes the bundled firmware,
; builds the sender and registers it with MSFS.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Stage
  #define Stage "..\build\setup-stage"
#endif
#ifndef OutDir
  #define OutDir "..\build"
#endif

#define AppName "MSFS CYD HUD"

[Setup]
AppId={{4F7C2A1E-6B3D-4E0A-9C55-2D8B7E61A9F3}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=MSFS24-CYD-HUD
AppPublisherURL=https://github.com/florianbaer/msfs24-cyd-hud
AppSupportURL=https://github.com/florianbaer/msfs24-cyd-hud/issues
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
OutputDir={#OutDir}
OutputBaseFilename=MsfsCydHud-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\installer\hud.ico
SetupIconFile={#Stage}\installer\hud.ico
WizardSmallImageFile={#Stage}\installer\wizard-small.bmp,{#Stage}\installer\wizard-small-2x.bmp
MinVersion=10.0

[Messages]
FinishedLabel=The files are installed.%n%nThe guided setup now finds your display, flashes it, builds the MSFS sender and registers it with the simulator. Keep the display plugged in.

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\Set up or reconfigure {#AppName}"; Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\installer\install.ps1"""; WorkingDir: "{app}"; IconFilename: "{app}\installer\hud.ico"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\installer\install.ps1"""; WorkingDir: "{app}"; Description: "Run the guided setup now (display and MSFS sender)"; Flags: postinstall nowait skipifsilent

[UninstallRun]
; Removes the sender, its exe.xml entry, shortcuts and downloaded tools
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\installer\install.ps1"" -Uninstall -Yes"; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveHud"
