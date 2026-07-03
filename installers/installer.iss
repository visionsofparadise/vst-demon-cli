; Inno Setup script for vst-demon.
; Version is injected by CI: ISCC /DAppVersion=0.1.0 installer.iss
; The fallback below lets the script compile standalone for inspection.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#define AppName "VST Demon"
#define AppExeName "vst-demon.exe"
#define AppPublisherName "ZCROSS"
#define InstallSubDir "ZCROSS\VST Demon"

[Setup]
AppId={{B7E4C2A1-9F3D-4A6E-8C1B-6D2E5A9F4C10}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisherName}
DefaultDirName={commonpf}\{#InstallSubDir}
DisableProgramGroupPage=yes
DisableDirPage=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
; PATH is a system environment change; declare it so Explorer is notified.
ChangesEnvironment=yes
; Writing to Program Files and the system PATH requires elevation.
PrivilegesRequired=admin
OutputDir=..\dist
OutputBaseFilename=vst-demon-setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}

[Files]
; The Release build must exist before ISCC runs (CI builds it first).
; Ninja single-config puts the exe at build\ (not build\Release\).
Source: "..\build\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
; Append the install dir to the system PATH, guarded so re-install never double-adds.
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
    ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; \
    Check: NeedsAddPath(ExpandConstant('{app}'))

[Code]
{ True when the install dir is not already a PATH segment (case-insensitive,
  tolerant of surrounding semicolons), so the [Registry] append runs at most once. }
function NeedsAddPath(Param: string): Boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKLM,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Uppercase(Param) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;

{ Remove the install dir from the system PATH on uninstall. }
procedure RemovePath(Dir: string);
var
  OrigPath: string;
  NewPath: string;
  P: Integer;
begin
  if not RegQueryStringValue(HKLM,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path', OrigPath) then
    exit;
  { Match the segment with a leading semicolon, tolerant of case. }
  NewPath := ';' + OrigPath + ';';
  P := Pos(';' + Uppercase(Dir) + ';', Uppercase(NewPath));
  if P = 0 then
    exit;
  Delete(NewPath, P, Length(Dir) + 1);
  { Strip the sentinel semicolons we added. }
  if (Length(NewPath) > 0) and (NewPath[1] = ';') then
    Delete(NewPath, 1, 1);
  if (Length(NewPath) > 0) and (NewPath[Length(NewPath)] = ';') then
    Delete(NewPath, Length(NewPath), 1);
  RegWriteExpandStringValue(HKLM,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path', NewPath);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemovePath(ExpandConstant('{app}'));
end;
