# Windows installer packaging — design

**Date:** 2026-07-07
**Status:** approved design
**Topic:** A proper Windows release artifact: a single Inno Setup installer that
installs the binaries per-user and autostarts the daemon, replacing the current
zip-of-loose-executables.

## Goal

Give Windows the same "download one artifact, it installs and sets itself up"
experience that macOS (`.pkg`) and Linux (`.deb`) already have. Today Windows
ships a per-arch `.zip` of three loose `.exe`s with no installer, no autostart,
and a bundled README as a stand-in for setup instructions. Replace that with a
single installer that lays down the binaries, puts the clients on `PATH`, and
starts `dimmitd` automatically at logon.

## Decisions (settled during brainstorming)

- **Scope:** install + autostart the daemon. Brightness-key capture stays out of
  scope (the user maps keys to the clients themselves, as on Linux).
- **Installer technology:** Inno Setup (single scripted `.exe`; free; installs on
  the CI Windows runners via choco).
- **Architecture:** one universal installer bundling **both** x86_64 and arm64
  binaries, selecting the matching arch at install time (native arm64 on arm64,
  not the emulated x64 build).
- **Autostart:** a per-user **Scheduled Task** (trigger: at logon) that launches
  `dimmitd.exe` in the user's session. A session-0 Windows service is rejected:
  dxva2 (`GetPhysicalMonitorsFromHMONITOR` / `SetMonitorBrightness`) enumerates
  the *calling session's* monitors, which a session-0 service cannot see. The
  Scheduled Task is the Windows-idiomatic equivalent of the macOS LaunchAgent.
- **Install scope:** per-user, no admin (`PrivilegesRequired=lowest`). Coherent
  with the per-user daemon, per-user socket, and per-user logon task. Installs
  for the current user only.
- **Release artifact:** the installer **replaces** the per-arch zips as the sole
  Windows release artifact.

## Definition of done

1. `packaging/windows/dimmit.iss` compiles with `ISCC` into a single
   `dimmit-<ver>-windows-setup.exe` containing both arches' binaries.
2. Running it (per-user, no UAC) installs the three matching-arch executables to
   `%LocalAppData%\Programs\Dimmit`, appends that dir to the user `PATH`, and
   registers + starts a logon Scheduled Task for `dimmitd.exe`.
3. Uninstall stops `dimmitd`, deletes the Scheduled Task, removes the `PATH`
   entry, and deletes the files.
4. CI builds the installer from the two per-arch builds and publishes it as the
   Windows release asset; the per-arch zips are gone.
5. A headless `/VERYSILENT` install→assert→uninstall→assert check passes on the
   Windows runner.

## Components

### `packaging/windows/dimmit.iss` (the installer)

Mirrors the role of `packaging/macos/build_pkg.sh` + `distribution.xml` and
`packaging/linux/build_deb.sh` + scripts: the single source that defines the
Windows package. Key directives:

- **Privileges / location:** `PrivilegesRequired=lowest`; `DefaultDirName={localappdata}\Programs\Dimmit`.
- **Metadata:** `AppId` (stable GUID), `AppName=Dimmit`, `AppVersion` passed in
  from CI (`/DAppVersion=<ver>`), publisher/URL, `ArchitecturesAllowed=x64compatible arm64`.
- **Payload (`[Files]`):** both arch payload dirs are passed in as ISCC defines
  (`/DX64Dir=...`, `/DArm64Dir=...`). Each set of three exes is installed under a
  `Check:` guard keyed on the **native OS arch**: `Check: IsX64OS` for the x64 set
  and `Check: IsArm64` for the arm64 set. These are mutually exclusive on the OS
  architecture, so exactly one set lands. Do **not** guard the x64 set with
  `IsX64Compatible` — that is true on arm64 (x64 emulation) and would wrongly
  install the x64 build there instead of the native arm64 one. (Inno 6.3+
  provides `IsArm64` / `IsX64OS`.)
- **PATH (`[Registry]` + `ChangesEnvironment=yes`):** append the install dir to
  `HKCU\Environment\Path` so `dimmit-up` / `dimmit-down` resolve from a key
  mapping. Removed on uninstall.
- **Autostart (`[Run]` / `[UninstallRun]`):** create the logon task with
  `schtasks /create /tn "Dimmit" /tr "<installdir>\dimmitd.exe" /sc onlogon /f`,
  and start it immediately (`schtasks /run /tn "Dimmit"`). Uninstall runs
  `taskkill /f /im dimmitd.exe` then `schtasks /delete /tn "Dimmit" /f`.
- **Add/Remove Programs:** automatic (per-user ARP entry).

### `release.yml` — new `release-windows-installer` job

- Runs on `windows-latest`.
- `needs: [release-windows]` (both arch matrix legs); downloads the
  `dimmit-windows-x86_64` and `dimmit-windows-arm64` artifacts into two dirs.
- Installs Inno Setup via choco; runs `ISCC packaging/windows/dimmit.iss`
  with `/DAppVersion`, `/DX64Dir`, `/DArm64Dir`.
- Uploads `dimmit-<ver>-windows-setup.exe`.
- Added to `publish`'s `needs:` list and to the release assets. The per-arch
  `release-windows` legs keep building/testing/guarding the exes (that's the
  compile+test+DLL-guard coverage) but no longer produce a zip; their uploaded
  exe artifacts feed this job.

### README

Add a Windows entry under **Install**: download `dimmit-<ver>-windows-setup.exe`,
run it (note the SmartScreen "unknown publisher" click-through, mirroring the
macOS quarantine note), then map your brightness keys to `dimmit-up` /
`dimmit-down` (now on `PATH`). A few lines, matching the existing per-platform
style.

## Socket path (daemon ↔ client agreement)

Keep the compiled-in Windows default `C:/Users/Public/dimmit.sock` (set in
`CMakeLists.txt`). The daemon (launched by the task) and the clients (launched by
the user's key mapping) share the same baked-in default, so they agree with zero
configuration and no code change. **Known limitation:** two users logged in
simultaneously on one machine would collide on that shared socket; a per-user
socket path is deferred future work.

## Non-goals (this pass)

- Brightness-key capture (`input/windows.c` stays a no-op).
- Code signing — the installer ships unsigned, like the current `.pkg`.
  Consequence: SmartScreen "unknown publisher" prompt, documented in the README.
- Windows service, MSVC build, per-user socket paths, multi-user support.

## Testing

- **CI, installer mechanics (no monitor needed):** after ISCC compiles the
  installer, run it `/VERYSILENT`, assert the three exes landed in the install
  dir, the user `PATH` contains the install dir, and the `Dimmit` Scheduled Task
  exists (`schtasks /query /tn Dimmit`); then uninstall `/VERYSILENT` and assert
  the files, PATH entry, and task are gone. This validates install/uninstall
  end-to-end on the runner without hardware or an interactive logon.
- **Existing per-arch coverage retained:** the `release-windows` legs still
  compile, run `ctest`, and run the OS-only-DLL guard on each arch.
- **Manual (documented, not automated):** on a real Windows box — install, log
  off/on, confirm `dimmitd` is running, map keys, and dim an external monitor.
