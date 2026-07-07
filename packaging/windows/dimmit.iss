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
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
  ValueData: "{olddata};{app}"; Check: NeedsAddPath(ExpandConstant('{app}'))

[Run]
Filename: "{sys}\schtasks.exe"; \
  Parameters: "/create /tn ""Dimmit"" /xml ""{app}\dimmit-task.xml"" /f"; \
  Flags: runhidden; StatusMsg: "Registering logon task..."
Filename: "{sys}\schtasks.exe"; Parameters: "/run /tn ""Dimmit"""; Flags: runhidden

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im dimmitd.exe"; \
  Flags: runhidden; RunOnceId: "KillDaemon"
Filename: "{sys}\schtasks.exe"; Parameters: "/delete /tn ""Dimmit"" /f"; \
  Flags: runhidden; RunOnceId: "DeleteTask"

[Code]
function NeedsAddPath(Dir: string): Boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', OrigPath) then
    OrigPath := '';
  Result := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;

procedure WriteTaskXml;
var
  Xml: string;
begin
  Xml :=
    '<?xml version="1.0" encoding="UTF-8"?>' + #13#10 +
    '<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">' + #13#10 +
    '  <Triggers><LogonTrigger><Enabled>true</Enabled></LogonTrigger></Triggers>' + #13#10 +
    '  <Principals><Principal id="Author">' + #13#10 +
    '    <LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel>' + #13#10 +
    '  </Principal></Principals>' + #13#10 +
    '  <Settings>' + #13#10 +
    '    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>' + #13#10 +
    '    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>' + #13#10 +
    '    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>' + #13#10 +
    '    <StartWhenAvailable>true</StartWhenAvailable>' + #13#10 +
    '    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>' + #13#10 +
    '  </Settings>' + #13#10 +
    '  <Actions Context="Author"><Exec><Command>' +
         ExpandConstant('{app}\dimmitd.exe') + '</Command></Exec></Actions>' + #13#10 +
    '</Task>' + #13#10;
  SaveStringToFile(ExpandConstant('{app}\dimmit-task.xml'), Xml, False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    WriteTaskXml;
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
