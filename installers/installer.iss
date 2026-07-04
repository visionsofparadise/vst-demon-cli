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
; The system environment registry key. Single source of truth so the PATH add-guard and the
; uninstall RemovePath can never desync on a typo.
#define EnvKey "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"

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
; Append the install dir to the system PATH, guarded so re-install never double-adds. ValueData is
; built in code so an empty existing Path yields "<dir>" rather than a leading-semicolon ";<dir>".
Root: HKLM; Subkey: "{#EnvKey}"; \
    ValueType: expandsz; ValueName: "Path"; ValueData: "{code:BuildNewPath}"; \
    Check: NeedsAddPath(ExpandConstant('{app}'))

[Code]
{ True when the install dir is not already a PATH segment (case-insensitive,
  tolerant of surrounding semicolons), so the [Registry] append runs at most once. }
function NeedsAddPath(Param: string): Boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKLM, '{#EnvKey}', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Uppercase(Param) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;

{ The new PATH value for the [Registry] append: existing PATH with the install dir appended.
  Avoids a leading semicolon when the existing PATH is empty (";<dir>" would be a stray empty
  segment). Guarded by NeedsAddPath, so it only runs when the dir is not already present. }
function BuildNewPath(Param: string): string;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKLM, '{#EnvKey}', 'Path', OrigPath) then
    OrigPath := '';
  if OrigPath = '' then
    Result := ExpandConstant('{app}')
  else
    Result := OrigPath + ';' + ExpandConstant('{app}');
end;

{ Remove the install dir from the system PATH on uninstall. Exact-segment match (leading + trailing
  semicolon) so a prefix collision — "VST Demon" vs "VST Demon Pro" — is never removed by mistake. }
procedure RemovePath(Dir: string);
var
  OrigPath: string;
  NewPath: string;
  P: Integer;
begin
  if not RegQueryStringValue(HKLM, '{#EnvKey}', 'Path', OrigPath) then
    exit;
  { Match the segment framed by semicolons, tolerant of case. }
  NewPath := ';' + OrigPath + ';';
  P := Pos(';' + Uppercase(Dir) + ';', Uppercase(NewPath));
  if P = 0 then
    exit;
  { Delete the segment and its trailing semicolon, collapsing ";<dir>;" to ";" so removing a
    middle entry never leaves an empty ";;" segment behind. }
  Delete(NewPath, P, Length(Dir) + 1);
  { Strip the sentinel semicolons we framed with. }
  if (Length(NewPath) > 0) and (NewPath[1] = ';') then
    Delete(NewPath, 1, 1);
  if (Length(NewPath) > 0) and (NewPath[Length(NewPath)] = ';') then
    Delete(NewPath, Length(NewPath), 1);
  RegWriteExpandStringValue(HKLM, '{#EnvKey}', 'Path', NewPath);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemovePath(ExpandConstant('{app}'));
end;
