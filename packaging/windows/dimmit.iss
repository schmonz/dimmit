; Inno Setup script for dimmit. Requires Inno Setup 6.3+ (IsArm64 / IsX64OS).
; CI invokes ISCC with /DAppVersion, /DX64Dir, /DArm64Dir (see release.yml).
; Ships BOTH arches; installs only the native-arch set. Per-user, no admin.

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef X64Dir
  #define X64Dir "payload\x64"
#endif
#ifndef Arm64Dir
  #define Arm64Dir "payload\arm64"
#endif

[Setup]
AppId={{6D2B7C41-1E5A-4B93-9F1C-DEC0DE01D177}}
AppName=Dimmit
AppVersion={#AppVersion}
AppPublisher=Amitai Schleier
AppPublisherURL=https://github.com/schmonz/dimmit
DefaultDirName={localappdata}\Programs\Dimmit
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible arm64
OutputBaseFilename=dimmit-{#AppVersion}-windows-setup
OutputDir=.
Uninstallable=yes
ChangesEnvironment=yes
DisableProgramGroupPage=yes
DisableDirPage=yes
WizardStyle=modern

[Files]
Source: "{#X64Dir}\dimmitd.exe";      DestDir: "{app}"; Check: IsX64OS; Flags: ignoreversion
Source: "{#X64Dir}\dimmit-up.exe";    DestDir: "{app}"; Check: IsX64OS; Flags: ignoreversion
Source: "{#X64Dir}\dimmit-down.exe";  DestDir: "{app}"; Check: IsX64OS; Flags: ignoreversion
Source: "{#Arm64Dir}\dimmitd.exe";     DestDir: "{app}"; Check: IsArm64; Flags: ignoreversion
Source: "{#Arm64Dir}\dimmit-up.exe";   DestDir: "{app}"; Check: IsArm64; Flags: ignoreversion
Source: "{#Arm64Dir}\dimmit-down.exe"; DestDir: "{app}"; Check: IsArm64; Flags: ignoreversion

[Registry]
; Append the install dir to the user PATH so dimmit-up / dimmit-down resolve from
; a key mapping. Removed on uninstall by RemovePath (below).
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
  ValueData: "{olddata};{app}"; Check: NeedsAddPath(ExpandConstant('{app}'))
; Autostart the daemon at logon via the per-user Run key. This needs no elevation
; (creating a Scheduled Task does -- "Access is denied" for a standard user),
; matching the per-user, no-admin install. uninsdeletevalue removes it on uninstall.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "Dimmit"; ValueData: """{app}\dimmitd.exe"""; \
  Flags: uninsdeletevalue

[Run]
; Start the daemon now so the user needn't log out/in first. nowait: dimmitd runs
; until stopped, so setup must not wait on it.
Filename: "{app}\dimmitd.exe"; Flags: nowait runhidden

[UninstallRun]
; Stop the running daemon before removing files (so its exe isn't locked).
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im dimmitd.exe"; \
  Flags: runhidden; RunOnceId: "KillDaemon"

[Code]
function NeedsAddPath(Dir: string): Boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', OrigPath) then
    OrigPath := '';
  Result := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;

procedure RemovePath(Dir: string);
var
  OrigPath, Sentinel: string;
  P: Integer;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', OrigPath) then
    exit;
  Sentinel := ';' + OrigPath + ';';
  P := Pos(';' + Uppercase(Dir) + ';', Uppercase(Sentinel));
  if P = 0 then exit;
  Delete(Sentinel, P, Length(Dir) + 1);
  OrigPath := Copy(Sentinel, 2, Length(Sentinel) - 2);
  RegWriteExpandStringValue(HKCU, 'Environment', 'Path', OrigPath);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemovePath(ExpandConstant('{app}'));
end;
