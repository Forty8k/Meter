; Windows installer for meterDARK Analyser. Built in CI with: iscc analyser\packaging\installer.iss  (expects the files in analyser\package-stage)
#define AppName "meterDARK Analyser"
#define Publisher "Forty8k"
#define Version "1.1.0"

[Setup]
AppId={{8E0B7C4A-5B52-4C0E-9A57-6D4D4B1A2F31}
AppName={#AppName}
AppVersion={#Version}
AppPublisher={#Publisher}
ArchitecturesInstallIn64BitMode=x64
ArchitecturesAllowed=x64
DefaultDirName={commonpf64}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\package-out
OutputBaseFilename=meterDARK-Analyser-Setup
SetupIconFile=icon.ico
UninstallDisplayIcon={app}\meterDARK Analyser.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "..\package-stage\meterDARK Analyser.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\package-stage\meterDARKAnalyserCLI.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\package-stage\HOW-TO.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\package-stage\mdark_library.py"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\meterDARK Analyser.exe"
Name: "{autoprograms}\{#AppName} - how to"; Filename: "{app}\HOW-TO.txt"
Name: "{autoprograms}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\meterDARK Analyser.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\meterDARK Analyser.exe"; Description: "Start {#AppName}"; Flags: nowait postinstall skipifsilent
