# Building dimmit on Windows (native, MinGW-w64 / MSYS2 UCRT64)

This is the verified, reproducible setup for building dimmit natively on Windows 10
(1803+) / 11. It produces native PE `.exe` files (`x86_64-w64-mingw32`). It was
validated on Windows 11 Home 10.0.26200 with gcc 16.1.0, CMake 4.3.4, Ninja 1.13.2
(see `windows-feasibility-result.md` for the run record).

> **Runtime dependency note.** A *default* build links one non-system DLL from the
> toolchain, `libwinpthread-1.dll`, so a dev build has to run with
> `C:\msys64\ucrt64\bin` on `PATH` (as the steps below already do). For a
> **redistributable, fully standalone** binary, link statically
> (`-DCMAKE_EXE_LINKER_FLAGS=-static`): the result imports only OS-provided DLLs
> (`dxva2`, `kernel32`, `user32`, `ws2_32`, and the UCRT `api-ms-win-crt-*`) and
> runs on a stock Windows 10 1803+/11 with no MSYS2 install. That is exactly what
> the GitHub Actions release build ships (see `.github/workflows/release.yml`).

> **MSYS2/MinGW is a native Windows toolchain, not WSL.** Only the package manager
> (`pacman`) runs in MSYS2's bash. The actual build runs from **PowerShell**, using the
> MinGW compiler/cmake/ninja directly off `PATH`.

## 1. Install MSYS2 (once)

From PowerShell:

```powershell
winget install --id MSYS2.MSYS2 -e --accept-source-agreements --accept-package-agreements
```

This installs to `C:\msys64`.

## 2. Install the toolchain (once)

`pacman` needs MSYS2's bash. Drive it from PowerShell without opening the MSYS2 UI — set
`MSYSTEM=UCRT64` so packages land in the UCRT64 environment:

```powershell
$env:MSYSTEM = 'UCRT64'; $env:CHERE_INVOKING = '1'
& 'C:\msys64\usr\bin\bash.exe' -lc "pacman -Syu --noconfirm"
& 'C:\msys64\usr\bin\bash.exe' -lc "pacman -S --needed --noconfirm git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf"
```

(Equivalently: open the **MSYS2 UCRT64** shell from the Start menu and run the same
`pacman` commands. If `pacman -Syu` asks to close the terminal, reopen UCRT64 and re-run it.)

## 3. Verify the toolchain

```powershell
$env:Path = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:Path"
gcc --version; cmake --version; ninja --version
gcc -dumpmachine                       # expect: x86_64-w64-mingw32
# The only Windows-specific headers dimmit needs (bundled with the toolchain):
Get-ChildItem C:\msys64\ucrt64\include\afunix.h, `
  C:\msys64\ucrt64\include\physicalmonitorenumerationapi.h, `
  C:\msys64\ucrt64\include\lowlevelmonitorconfigurationapi.h
```

dxva2's import library ships with MinGW too — no separate Windows SDK install is needed.

## 4. Build

Every new PowerShell session must put the toolchain on `PATH` first:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:Path"
```

**Normal case — repo on a local NTFS path (e.g. `C:\src\dimmit`):**

```powershell
cd C:\src\dimmit
cmake -B build -G Ninja
cmake --build build
```

**Special case — repo on a WSL/network share (`\\wsl.localhost\...` or any UNC path):**

An in-place build fails at the link step, because the toolchain runs link commands via
`cmd.exe`, and cmd.exe cannot use a UNC working directory ("UNC paths are not supported").
Build **out-of-source** with the build dir on local NTFS; the source and git repo stay on
the share (gcc reads UNC source paths fine):

```powershell
cmake -S . -B C:\Users\<you>\dimmit-build -G Ninja
cmake --build C:\Users\<you>\dimmit-build
```

Outputs: `dimmitd.exe`, `dimmit-up.exe`, `dimmit-down.exe`, `test_dimmit.exe`.

## 5. Run the tests

```powershell
cd C:\Users\<you>\dimmit-build      # or .\build for the local-repo case
ctest --output-on-failure
```

Expect `dimmit_unit ... Passed`. `test_command_loop_end_to_end` prints a SKIP notice on
Windows (it uses POSIX `socketpair`); the rest of the suite runs.

## 6. Smoke test (manual)

```powershell
$env:DIMMIT_SOCK = 'C:/Users/Public/dimmit.sock'
.\dimmitd.exe                        # in one shell: binds the AF_UNIX socket and listens
# in a second PowerShell (set Path + DIMMIT_SOCK again):
.\dimmit-down.exe                    # delivers a "down" command; exits 0
```

With no DDC/CI external monitor attached, expect `Warning: couldn't read initial brightness`
on startup and `Failed to set brightness` on a command — both acceptable: they prove the
socket → command → dxva2 DDC path runs. Dimming an actual external monitor is the next
validation step.

### Stopping the daemon — important on Windows

In its **own interactive terminal**, Ctrl-C triggers the clean-shutdown handler
(`SetConsoleCtrlHandler`).

When stopping the daemon **from automation that shares a console** (e.g. an agent/CI shell),
do **not** use a Ctrl-C/Ctrl-Break-based stop: the console control event can propagate to the
controlling process and kill it. Use TerminateProcess instead:

```powershell
taskkill /F /IM dimmitd.exe
```

## Notes

- **Default socket path** on Windows is `C:/Users/Public/dimmit.sock` (AF_UNIX needs a real,
  writable filesystem path; `/tmp` does not exist). Override with `DIMMIT_SOCK`.
- **Shared cross-platform checkout.** If the same working tree is accessed from both Unix
  and Windows (as in this repo over WSL), keep `core.autocrlf=false` and the committed
  `.gitattributes` (`* text=auto eol=lf`) so a Windows git never rewrites the tree to CRLF.
  Set the git identity locally if the Windows git has none:
  `git config user.name "..."; git config user.email "..."`.
- **CI and releases.** `.github/workflows/ci.yml` builds and tests Windows on every push
  (x86_64 via UCRT64/gcc and arm64 via CLANGARM64/clang). `.github/workflows/release.yml`
  ships a per-arch, statically linked `dimmit-<ver>-windows-<arch>.zip`. The arm64 build has
  no x86_64 host that can run it, so the `windows-11-arm` runner is its first real validation.
- **Out of scope for this build:** Windows service / autostart, brightness-key capture,
  installer (MSI/MSIX) + signing, and an MSVC build. See the feasibility plan's backlog.
