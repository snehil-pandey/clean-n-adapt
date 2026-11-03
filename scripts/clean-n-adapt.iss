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
ChangesEnvironment=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
Name: "addtopath"; Description: "Add Clean-n-Adapt to PATH"; GroupDescription: "Command line integration:"; Flags: checkedonce

[Files]
Source: "..\dist\cna.exe"; DestDir: "{app}"; Flags: ignoreversion

[Dirs]
Name: "{app}\.state"

[Icons]
Name: "{group}\Clean-n-Adapt"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\Clean-n-Adapt"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\cna.exe"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{app};{olddata}"; Check: NeedsAddPath(ExpandConstant('{app}')); Tasks: addtopath

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Clean-n-Adapt"; Flags: nowait postinstall skipifsilent

[Code]
function NormalizePath(Value: String): String;
begin
  Result := Lowercase(RemoveBackslashUnlessRoot(ExpandConstant(Value)));
end;

function NeedsAddPath(InstallPath: String): Boolean;
var
  CurrentPath: String;
  Part: String;
  Index: Integer;
begin
  Result := True;
  if not RegQueryStringValue(HKLM, 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment', 'Path', CurrentPath) then
    exit;

  InstallPath := NormalizePath(InstallPath);
  while CurrentPath <> '' do
  begin
    Index := Pos(';', CurrentPath);
    if Index > 0 then
    begin
      Part := Copy(CurrentPath, 1, Index - 1);
      Delete(CurrentPath, 1, Index);
    end
    else
    begin
      Part := CurrentPath;
      CurrentPath := '';
    end;

    if NormalizePath(Part) = InstallPath then
    begin
      Result := False;
      exit;
    end;
  end;
end;
