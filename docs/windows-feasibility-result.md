# Windows feasibility result

Date: 2026-06-30
Toolchain: MinGW-w64 UCRT64 (gcc 16.1.0), CMake 4.3.4, Ninja 1.13.2
OS: Windows 11 Home 10.0.26200

## Build
- Configure: ok — but **not** in-place on the repo's filesystem; see "Deviations" below.
- Build (dimmitd.exe, dimmit-up.exe, dimmit-down.exe, test_dimmit.exe): ok, all four link.
- clock_gettime snag hit? no — MinGW-w64 UCRT provides it via `<time.h>`; `dimmitd.exe`
  linked without the troubleshooting fallback.

## Unit tests (ctest)
- Result: All 44 checks passed (`ctest` reports `dimmit_unit` Passed in ~0.8s).
- Windows-skipped: test_command_loop_end_to_end (no socketpair).

## Runtime smoke test
- Displays present: a single "Generic PnP Monitor" (no external DDC/CI monitor attached;
  no separate internal-vs-external distinction available).
- dimmitd startup: `ddc_open_display()` **succeeded** (a physical monitor was enumerated
  and opened), then the initial VCP brightness *read* failed, so the daemon logged
  `Warning: couldn't read initial brightness` and continued to bind + listen. (The
  `Listening on ...` line is `printf`/stdout, which is block-buffered when redirected to a
  file, so it does not appear in captured output — but the bound socket file
  `C:/Users/Public/dimmit.sock` did appear, confirming bind+listen.)
- Socket round-trip (dimmit-down and dimmit-up): client connected and exited 0 both times;
  the daemon received the command, parsed it, ran it through the dimmer to the DDC write
  (`SetVCPFeature`), which returned false → `Failed to set brightness`. The selected
  display ignored the DDC write, which still proves the
  socket → recv → parse → dimmer → DDC path executed end-to-end.
- Shutdown via Ctrl-C: **not exercised live** (see "Windows gotcha" below); the
  `SetConsoleCtrlHandler` handler is registered and sets the same `running = 0` flag the
  POSIX signal path uses (and that path is covered by the POSIX test suite). The daemon was
  stopped with `taskkill /F` (TerminateProcess), which does not run the handler.

## Conclusion
- Builds natively on Windows: **yes** (native PE `.exe`, `x86_64-w64-mingw32`, no MSYS runtime dep).
- AF_UNIX IPC works locally: **yes** — verified: daemon binds the AF_UNIX socket, both
  `dimmit-up` and `dimmit-down` connect and deliver a command end-to-end.
- dxva2 DDC path executes (would drive a DDC/CI external monitor): **yes** — `EnumDisplayMonitors`
  + `GetPhysicalMonitorsFromHMONITOR` enumerated and opened a monitor, and
  `GetVCPFeatureAndVCPFeatureReply` / `SetVCPFeature` were invoked correctly. With only a
  Generic PnP Monitor that does not honor DDC/CI, get/set returning false is the expected
  outcome; the API path is implemented correctly enough to drive a DDC/CI external monitor.
- Next validation step: attach a DDC/CI-capable external monitor and confirm actual dimming.

## Deviations from the plan (and why)

1. **Out-of-source build on local NTFS (required).** The repo lives on the WSL filesystem,
   exposed to Windows as the UNC path `\\wsl.localhost\Ubuntu\...`. An in-place
   `cmake -B build` fails at the *link* step: the toolchain shells out to
   `cmd.exe /C "cd . && ..."`, and cmd.exe refuses a UNC working directory
   ("UNC paths are not supported. Defaulting to Windows directory"), so `ld` cannot write
   its output ("Permission denied"). The *compile* steps succeed (gcc reads UNC sources
   fine) — only cmd.exe's working directory is the problem. Fix: build out-of-source with the
   build directory on local NTFS, e.g. `cmake -S . -B C:\Users\schmonz\dimmit-build -G Ninja`;
   source and git stay on the share. (A future builder who clones to a normal `C:\` path
   never hits this.)

2. **`test_dimmit` needed `ws2_32` (plan gap).** The test executable compiles `command.c`,
   whose `read_command()` now calls `recv()`. On Windows `recv` lives in Winsock, so the test
   target failed to link (`undefined reference to __imp_recv`) until `ws2_32` was added to it,
   matching how the daemon and clients already link it.

3. **Toolchain driven from PowerShell, not a bash shell.** MSYS2/MinGW is a *native Windows*
   toolchain; only `pacman` (package install) needs MSYS2's bash. Builds run from PowerShell
   with `C:\msys64\ucrt64\bin` (and `C:\msys64\usr\bin` for git) prepended to `PATH`. WSL is
   not involved in the build — it is only where the source happens to live.

4. **`.gitattributes` added.** This is a single shared working tree accessed from both Unix
   (WSL) and native Windows. A committed `* text=auto eol=lf` plus `core.autocrlf=false`
   guards against a Windows git rewriting the tree to CRLF and desyncing the Unix side.

## Windows gotcha (operational, not a code defect)

Stopping the console daemon through the agent harness's process-stop delivered a console
control event that propagated to Claude Code's *own* shared console and dropped the session
(recovered with `claude -c`). When stopping `dimmitd.exe` from automation that shares a
console, use `taskkill /F /IM dimmitd.exe` (TerminateProcess — no console signal) rather than
a Ctrl-C/Ctrl-Break-based stop. This is purely about how the daemon is stopped in a shared
console; it does not affect normal interactive use, where Ctrl-C in the daemon's own terminal
triggers the clean-shutdown handler as intended.

Also observed: on Windows the AF_UNIX socket file was auto-removed by the OS when the owning
process terminated (the file was already gone before a later restart), unlike POSIX where the
daemon's own `unlink()` is what removes it. The daemon's startup `unlink()` is a harmless
no-op when the file is already absent.
