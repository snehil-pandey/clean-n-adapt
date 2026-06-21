#define MyAppName "Clean-n-Adapt"
#define MyAppVersion "2.0.0"
#define MyAppPublisher "snehil-pandey"
#define MyAppExeName "cna.exe"

[Setup]
AppId={{6F6B4D72-78B1-4F34-93F9-C7F620E19D2A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\cleanNadapt
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
PrivilegesRequired=admin
OutputDir=..\installer
OutputBaseFilename=Clean-n-Adapt-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
Name: "addtopath"; Description: "Add Clean-n-Adapt to PATH"; GroupDescription: "Command line integration:"; Flags: checkedonce

[Files]
Source: "..\dist\cna.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "add-to-path.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Dirs]
Name: "{app}\.state"

[Icons]
Name: "{group}\Clean-n-Adapt"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\Clean-n-Adapt"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\cna.exe"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName}"; Flags: uninsdeletekey

[Run]
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\add-to-path.ps1"" -InstallDir ""{app}"" -Scope Machine"; Tasks: addtopath; Flags: runhidden waituntilterminated
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Clean-n-Adapt"; Flags: nowait postinstall skipifsilent
