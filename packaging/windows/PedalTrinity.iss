; Pedal Trinity - Copyright (C) 2026 FabioNET - GNU GPL v3
; Installer Windows (Inno Setup 6). Compilazione:
;   iscc /DBuildDir=..\..\build\PedalTrinity_artefacts\Release packaging\windows\PedalTrinity.iss

#ifndef BuildDir
  #define BuildDir "..\..\build\PedalTrinity_artefacts\Release"
#endif
#define AppName "Pedal Trinity"
#define AppVersion "1.1.2-beta"
#define AppPublisher "FabioNET"
#define AppURL "https://github.com/fabionet/Pedal-Trinity"

[Setup]
AppId={{6F1C2B3A-8E7D-4B51-9A44-0D5E2C7F9A11}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppCopyright=Copyright (C) 2026 FabioNET - GNU GPL v3
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
LicenseFile=..\..\LICENSE
OutputDir=..\..\dist
OutputBaseFilename=PedalTrinity-{#AppVersion}-Windows-x64-Setup
SetupIconFile=PedalTrinity.ico
UninstallDisplayIcon={app}\Pedal Trinity.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
DisableProgramGroupPage=yes

[Languages]
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Installazione completa / Full installation"
Name: "custom"; Description: "Personalizzata / Custom"; Flags: iscustom

[Components]
Name: "standalone"; Description: "Applicazione Standalone (driver ASIO / WASAPI)"; Types: full custom
Name: "vst3"; Description: "Plugin VST3"; Types: full custom
Name: "lv2"; Description: "Plugin LV2"; Types: full

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: standalone

[Files]
Source: "{#BuildDir}\Standalone\Pedal Trinity.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#BuildDir}\VST3\Pedal Trinity.vst3\*"; DestDir: "{commoncf64}\VST3\Pedal Trinity.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\LV2\Pedal Trinity.lv2\*"; DestDir: "{commoncf64}\LV2\Pedal Trinity.lv2"; Components: lv2; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\docs\PedalTrinity_Guida.pdf"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\Source\third_party\LICENSE-NAM.txt"; DestDir: "{app}"; DestName: "THIRD-PARTY-NAM.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Pedal Trinity.exe"; Components: standalone
Name: "{group}\Guida di Pedal Trinity (PDF)"; Filename: "{app}\PedalTrinity_Guida.pdf"
Name: "{group}\Licenza GNU GPL v3"; Filename: "{app}\LICENSE.txt"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\Pedal Trinity.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Pedal Trinity.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent; Components: standalone
