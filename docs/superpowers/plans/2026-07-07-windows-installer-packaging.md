# Windows Installer Packaging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Windows per-arch `.zip`s with a single Inno Setup installer that installs the binaries per-user, puts the clients on `PATH`, and autostarts `dimmitd` via a logon Scheduled Task.

**Architecture:** One Inno Setup script (`packaging/windows/dimmit.iss`) bundles both arches' prebuilt `.exe`s and installs only the native-arch set. A per-user logon Scheduled Task (defined by an XML the installer emits with the resolved exe path) runs `dimmitd` in the user's session, where dxva2 can see the monitors. A new CI job compiles the installer from the two per-arch build artifacts and publishes it; the per-arch zips are removed.

**Tech Stack:** Inno Setup 6.3+ (ISCC), PowerShell (smoke test), GitHub Actions, existing MSYS2/MinGW build.

## Global Constraints

- **Installer tool:** Inno Setup **6.3+** (required for `IsArm64` / `IsX64OS`).
- **Privileges:** per-user, no admin — `PrivilegesRequired=lowest`.
- **Install dir:** `{localappdata}\Programs\Dimmit` (i.e. `%LocalAppData%\Programs\Dimmit`).
- **Arch guards:** x64 payload under `Check: IsX64OS`; arm64 payload under `Check: IsArm64`. Never `IsX64Compatible` (true on arm64 emulation).
- **Autostart:** per-user logon **Scheduled Task** named exactly `Dimmit`, `RunLevel=LeastPrivilege`, `LogonType=InteractiveToken` (registers without elevation).
- **Artifact name:** `dimmit-<version>-windows-setup.exe`. It **replaces** the per-arch zips as the sole Windows release asset.
- **Unsigned** — no code signing this pass.
- **Socket path unchanged** — the compiled-in Windows default `C:/Users/Public/dimmit.sock` stays; no source changes.
- **Clients on PATH:** append the install dir to the **user** `Path` (`HKCU\Environment`), removed on uninstall.

---

### Task 1: Inno Setup script that compiles to a setup.exe

**Files:**
- Create: `packaging/windows/dimmit.iss`

**Interfaces:**
- Consumes: three prebuilt exes per arch (`dimmitd.exe`, `dimmit-up.exe`, `dimmit-down.exe`) from payload dirs passed as ISCC defines `X64Dir` / `Arm64Dir`, and `AppVersion`.
- Produces: `packaging/windows/dimmit-<AppVersion>-windows-setup.exe` (OutputDir is the script's own dir). Defines a Scheduled Task named `Dimmit`, an install dir `%LocalAppData%\Programs\Dimmit`, and a user-PATH append.

- [ ] **Step 1: Ensure Inno Setup is installed (local dev)**

Run (PowerShell):
```powershell
winget install -e --id JRSoftware.InnoSetup --accept-source-agreements --accept-package-agreements
Test-Path "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
```
Expected: `True`. (If `winget` is unavailable, `choco install innosetup -y`.)

- [ ] **Step 2: Build local x64 payload to compile against**

The installer needs real exes to bundle. Reuse a UCRT64 build (from the CI-repro earlier or a fresh one) and stage payload dirs. arm64 exes can't be built on an x64 host, so stage **copies of the x64 exes as arm64 stand-ins** — they are never executed on an x64 box (the `IsArm64` set won't install), they only need to exist so ISCC's `[Files]` sources resolve.

Run (MSYS2 UCRT64, then PowerShell):
```powershell
# (UCRT64) build if not already present at C:\Users\<you>\dimmit-bw:
#   cmake -S "\\wsl.localhost\Ubuntu\home\schmonz\code\trees\dimmit" -B C:\Users\$env:USERNAME\dimmit-bw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_EXE_LINKER_FLAGS=-static
#   cmake --build C:\Users\$env:USERNAME\dimmit-bw
$bw = "C:\Users\$env:USERNAME\dimmit-bw"
New-Item -ItemType Directory -Force payload\x64, payload\arm64 | Out-Null
foreach ($e in 'dimmitd.exe','dimmit-up.exe','dimmit-down.exe') {
  Copy-Item "$bw\$e" "payload\x64\$e" -Force
  Copy-Item "$bw\$e" "payload\arm64\$e" -Force   # stand-in; never runs on x64
}
```
Expected: `payload\x64` and `payload\arm64` each contain the three exes.

- [ ] **Step 3: Write `packaging/windows/dimmit.iss`**

```
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
```

Note: `WriteTaskXml` runs at `ssPostInstall`, which fires before the `[Run]` entries execute — so the XML exists when `schtasks /create /xml` runs. The XML `<Command>` needs no shell quoting even if the path contains spaces. XML content is ASCII (assumes an ASCII install path); a non-ASCII username is an accepted edge-case limitation.

- [ ] **Step 4: Compile and verify a setup.exe is produced**

Run (PowerShell, from repo root):
```powershell
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" `
  "/DAppVersion=0.0.0-dev" "/DX64Dir=payload\x64" "/DArm64Dir=payload\arm64" `
  packaging\windows\dimmit.iss
Get-ChildItem packaging\windows\dimmit-*-windows-setup.exe
```
Expected: ISCC prints `Successful compile` (exit 0) and `dimmit-0.0.0-dev-windows-setup.exe` is listed under `packaging\windows\`.

- [ ] **Step 5: Commit**

```bash
git add packaging/windows/dimmit.iss
git commit -m "feat(packaging): Inno Setup installer script for Windows"
```
(Do not commit `payload/` or the built setup.exe — add them to `.gitignore` if needed, or leave them untracked.)

---

### Task 2: Smoke test script + verified per-user install/uninstall

**Files:**
- Create: `packaging/windows/smoke_test.ps1`

**Interfaces:**
- Consumes: a completed per-user install at `%LocalAppData%\Programs\Dimmit`.
- Produces: `smoke_test.ps1 -Mode post-install|post-uninstall` — exits 0 and prints `SMOKE OK: ...` when the expected state holds, exits 1 with `SMOKE FAIL: ...` otherwise. Used by both local testing and CI (Task 3).

- [ ] **Step 1: Write the smoke test (this is the test)**

Create `packaging/windows/smoke_test.ps1`:
```powershell
[CmdletBinding()]
param(
  [Parameter(Mandatory)][ValidateSet('post-install','post-uninstall')][string]$Mode,
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\Dimmit')
)
$ErrorActionPreference = 'Stop'
function Fail($m) { Write-Error "SMOKE FAIL: $m"; exit 1 }

$exes = 'dimmitd.exe','dimmit-up.exe','dimmit-down.exe'
$userPath = [Environment]::GetEnvironmentVariable('Path','User')
& schtasks /query /tn Dimmit *> $null
$taskExists = ($LASTEXITCODE -eq 0)

if ($Mode -eq 'post-install') {
  foreach ($e in $exes) {
    if (-not (Test-Path (Join-Path $InstallDir $e))) { Fail "$e not installed" }
  }
  if (-not (Test-Path (Join-Path $InstallDir 'dimmit-task.xml'))) { Fail 'task xml missing' }
  if ($userPath -notlike "*$InstallDir*") { Fail 'install dir not on user PATH' }
  if (-not $taskExists) { Fail 'Dimmit scheduled task missing' }
  Write-Host 'SMOKE OK: install verified'
} else {
  if (Test-Path (Join-Path $InstallDir 'dimmitd.exe')) { Fail 'files remain after uninstall' }
  if ($userPath -like "*$InstallDir*") { Fail 'install dir still on user PATH' }
  if ($taskExists) { Fail 'Dimmit scheduled task still present' }
  Write-Host 'SMOKE OK: uninstall verified'
}
```

- [ ] **Step 2: Run install, then smoke post-install (expect OK)**

Run (PowerShell, from repo root; uses the setup.exe from Task 1):
```powershell
$setup = Get-ChildItem packaging\windows\dimmit-*-windows-setup.exe | Select-Object -First 1
Start-Process $setup.FullName -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -Wait
powershell -NoProfile -ExecutionPolicy Bypass -File packaging\windows\smoke_test.ps1 -Mode post-install
```
Expected: `SMOKE OK: install verified`. (This registers a real `Dimmit` task and starts `dimmitd` on your box — harmless; it dims nothing without a DDC monitor and is removed on uninstall.)

- [ ] **Step 3: Run uninstall, then smoke post-uninstall (expect OK)**

Run (PowerShell):
```powershell
$unins = Join-Path $env:LOCALAPPDATA 'Programs\Dimmit\unins000.exe'
Start-Process $unins -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -Wait
Start-Sleep -Seconds 3   # unins relaunches itself to a temp copy; let it finish
powershell -NoProfile -ExecutionPolicy Bypass -File packaging\windows\smoke_test.ps1 -Mode post-uninstall
```
Expected: `SMOKE OK: uninstall verified`. If it fails on a lingering PATH entry or task, that's a real bug in `dimmit.iss` (Task 1 `[UninstallRun]` / `RemovePath`) — fix there and re-run Steps 2–3.

- [ ] **Step 4: Commit**

```bash
git add packaging/windows/smoke_test.ps1
git commit -m "test(packaging): headless install/uninstall smoke test for the Windows installer"
```

---

### Task 3: CI — replace zips with the installer

**Files:**
- Modify: `.github/workflows/release.yml`

**Interfaces:**
- Consumes: the `release-windows` matrix builds (already produce `build/dimmitd.exe` etc. per arch).
- Produces: artifact `dimmit-windows-setup` containing `dimmit-<ver>-windows-setup.exe`, published as the sole Windows release asset. Per-arch artifacts are renamed in content to raw exes: `dimmit-windows-x86_64` / `dimmit-windows-arm64` now hold `*.exe`, not a zip.

- [ ] **Step 1: Replace the `release-windows` "Package zip" + upload with raw-exe staging**

In `.github/workflows/release.yml`, replace the current step (lines ~153–166):
```yaml
      - name: Package zip
        run: |
          VER="${{ steps.ver.outputs.version }}"
          dist="dimmit-$VER-windows-${{ matrix.arch }}"
          mkdir "$dist"
          cp build/dimmitd.exe build/dimmit-up.exe build/dimmit-down.exe "$dist"/
          cp README.md "$dist"/
          cmake -E tar cf "$dist.zip" --format=zip "$dist"

      - name: Upload build artifact (always)
        uses: actions/upload-artifact@v7
        with:
          name: dimmit-windows-${{ matrix.arch }}
          path: dimmit-*-windows-*.zip
```
with:
```yaml
      - name: Stage binaries
        run: |
          dist="payload-${{ matrix.arch }}"
          mkdir "$dist"
          cp build/dimmitd.exe build/dimmit-up.exe build/dimmit-down.exe "$dist"/

      - name: Upload build artifact (always)
        uses: actions/upload-artifact@v7
        with:
          name: dimmit-windows-${{ matrix.arch }}
          path: payload-${{ matrix.arch }}/*.exe
```

- [ ] **Step 2: Add the `release-windows-installer` job**

Insert this job after the `release-windows` job and before `publish`:
```yaml
  release-windows-installer:
    name: Build Windows installer
    needs: [release-windows]
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v7

      - name: Determine version
        id: ver
        uses: ./.github/actions/determine-version

      - name: Download x86_64 binaries
        uses: actions/download-artifact@v8
        with:
          name: dimmit-windows-x86_64
          path: payload/x64

      - name: Download arm64 binaries
        uses: actions/download-artifact@v8
        with:
          name: dimmit-windows-arm64
          path: payload/arm64

      - name: Install Inno Setup
        shell: pwsh
        run: choco install innosetup --no-progress -y

      - name: Build installer
        shell: pwsh
        run: |
          & "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" `
            "/DAppVersion=${{ steps.ver.outputs.version }}" `
            "/DX64Dir=payload\x64" "/DArm64Dir=payload\arm64" `
            packaging\windows\dimmit.iss

      - name: Smoke test installer (install + uninstall)
        shell: pwsh
        run: |
          $setup = Get-ChildItem packaging\windows\dimmit-*-windows-setup.exe | Select-Object -First 1
          Start-Process $setup.FullName -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -Wait
          & packaging\windows\smoke_test.ps1 -Mode post-install
          $unins = Join-Path $env:LOCALAPPDATA 'Programs\Dimmit\unins000.exe'
          Start-Process $unins -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -Wait
          Start-Sleep -Seconds 3
          & packaging\windows\smoke_test.ps1 -Mode post-uninstall

      - name: Upload installer artifact
        uses: actions/upload-artifact@v7
        with:
          name: dimmit-windows-setup
          path: packaging/windows/dimmit-*-windows-setup.exe
```

- [ ] **Step 3: Update `publish` — needs, downloads, and asset globs**

Replace the `publish` job's `needs:` line and its "Download all build artifacts" step and asset globs:
```yaml
  publish:
    needs: [release-macos, release-linux, release-windows-installer]
    if: github.ref_type == 'tag'
    runs-on: ubuntu-latest
    steps:
      - name: Download macOS artifact
        uses: actions/download-artifact@v8
        with:
          name: dimmit-pkg
          path: dist
      - name: Download Linux artifact
        uses: actions/download-artifact@v8
        with:
          name: dimmit-deb
          path: dist
      - name: Download Windows installer artifact
        uses: actions/download-artifact@v8
        with:
          name: dimmit-windows-setup
          path: dist

      - name: Publish to GitHub Releases
        uses: softprops/action-gh-release@v3
        with:
          files: |
            dist/dimmit-*.pkg
            dist/dimmit_*_amd64.deb
            dist/dimmit-*-windows-setup.exe
            dist/latest.json
```
Rationale: `publish` now `needs` `release-windows-installer` (which transitively needs `release-windows`), so a Windows build or installer failure still blocks the whole release — the all-or-nothing invariant holds. Downloading by explicit artifact name avoids flattening the two per-arch raw-exe artifacts (both contain `dimmitd.exe`) into a colliding `dist/`. `latest.json` rides in the `dimmit-pkg` artifact, so the macOS download still provides it.

- [ ] **Step 4: Validate the workflow YAML**

Run (from repo root, WSL/bash):
```bash
python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/release.yml')); print('YAML OK')"
```
Expected: `YAML OK`. If `actionlint` is available, also run `actionlint .github/workflows/release.yml` and expect no errors. (Full runtime validation happens on the next tag push / `workflow_dispatch` — note this in the commit.)

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci(release): ship a Windows installer instead of per-arch zips"
```

---

### Task 4: README Windows install entry

**Files:**
- Modify: `README.md` (the `## Install` section)

**Interfaces:**
- Consumes: nothing. Produces: user-facing install instructions matching the new artifact.

- [ ] **Step 1: Add a Windows subsection under `## Install`**

After the existing `### Linux` install subsection, add:
```markdown
### Windows

Download `dimmit-<version>-windows-setup.exe` from the [latest release](https://github.com/schmonz/dimmit/releases/latest) and run it.
It installs per-user (no admin), so Windows SmartScreen may warn about an unknown publisher — choose **More info → Run anyway** (the build is not code-signed yet).
The installer starts `dimmitd` at logon automatically.

Then map your brightness keys (or any keys) to `dimmit-up` and `dimmit-down` in your keyboard/hotkey tool — the installer put them on your `PATH`.
```

- [ ] **Step 2: Verify it renders and reads correctly**

Run:
```bash
grep -n "### Windows" README.md
```
Expected: the new subsection is present under `## Install`.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs(readme): Windows install instructions for the setup.exe"
```

---

## Self-Review

**Spec coverage:**
- Inno Setup installer, per-user, `{localappdata}` → Task 1. ✓
- Both arches bundled, `IsX64OS`/`IsArm64` selection → Task 1 `[Files]`. ✓
- Clients on user PATH, removed on uninstall → Task 1 `[Registry]` + `RemovePath`. ✓
- Logon Scheduled Task autostart (LeastPrivilege, no admin) → Task 1 XML + `[Run]`. ✓
- Uninstall stops daemon, deletes task, removes PATH, deletes files → Task 1 `[UninstallRun]` + `CurUninstallStepChanged`. ✓
- Socket path unchanged → no source task (constraint documented). ✓
- Installer replaces zips as release artifact → Task 3 Steps 1–3. ✓
- CI builds installer from both arch builds; all-or-nothing publish preserved → Task 3 Step 2–3. ✓
- Headless install→assert→uninstall→assert on the runner → Task 3 Step 2 + Task 2. ✓
- README Windows install entry with SmartScreen note → Task 4. ✓
- Non-goals (keys/signing/service) → not implemented, correctly. ✓

**Placeholder scan:** No TBD/TODO; all file contents and commands are literal. ✓

**Type/name consistency:** Task name `Dimmit`, install dir `%LocalAppData%\Programs\Dimmit`, artifact `dimmit-<ver>-windows-setup.exe`, artifacts `dimmit-windows-{x86_64,arm64,setup}`, and the `smoke_test.ps1 -Mode post-install|post-uninstall` interface are used identically across Tasks 1–4. ✓
