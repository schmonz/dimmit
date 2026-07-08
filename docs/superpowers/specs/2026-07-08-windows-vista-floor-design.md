# Lowering the Windows floor to Vista — design

**Date:** 2026-07-08
**Status:** approved design
**Topic:** Make dimmit build and run on older Windows down to **Vista SP2**, by
extracting the control channel into a platform abstraction (named pipe on
Windows, AF_UNIX elsewhere), switching the x86/x64 toolchain to MSVCRT MinGW,
adding a 32-bit build, and shipping a single installer that runs on the whole
range. This is the first of two sub-projects; **Windows XP is a separate spec**
(gated on a DDC/CI feasibility spike — no Monitor Configuration API before Vista).

## Goal

dimmit already builds and runs on modern Windows (native PE via MinGW-w64,
Inno Setup installer, brightness-key capture). Extend its reach the way the macOS
work reaches Mavericks: run everywhere the user goes. The binding constraint is
DDC — the dxva2 Monitor Configuration API used by `src/platform/ddc/windows.c`
exists only on Vista and later — so **Vista SP2** is the floor for this pass.
Everything else (named pipes, Raw Input, MSVCRT) reaches lower; XP is deferred
because its DDC story is unsolved.

## Decisions (settled during brainstorming)

- **Floor:** Windows Vista SP2 → present. Architectures: **x86_64, i686, arm64.**
- **Transport:** a **named pipe** (`\\.\pipe\dimmit`) on Windows, replacing
  AF_UNIX. Works back to NT/2000/XP (so it also serves the future XP tier), gives
  real per-user security via the pipe DACL, and opens no listening TCP port.
- **No platform `#ifdef`s in `dimmitd.c` / `dimmit.c`.** The listen/accept/read
  and connect/send code moves behind a new **`ipc` platform abstraction**
  (`src/platform/ipc/`), like `ddc`/`access-control`/`logging`/`input`.
- **C runtime / toolchain:** build x86_64 and i686 with **MSVCRT MinGW** (MSYS2
  `MINGW64` + `MINGW32`, gcc, `-static`) so binaries import only `msvcrt.dll` +
  core OS DLLs present on every Windows — no UCRT redistributable. **arm64 stays
  CLANGARM64/UCRT** (arm64 Windows is Win10-only, so it never needs old-runtime
  support).
- **Installer:** one **Inno Setup 5.6.1** installer for the whole range,
  replacing the Inno 6 script. Native-payload selection via `GetNativeSystemInfo`.
- **Unchanged:** `ddc/windows.c` (dxva2, Vista+) and the Raw Input key-capture
  backend (XP+) already cover the new range.

## Architecture

### The `ipc` control-channel abstraction (the core refactor)

Today `dimmitd.c` holds the AF_UNIX `socket`/`bind`/`listen`/`select`/`accept`
loop inline behind `#ifdef _WIN32`, with a `src/platform/compat/net.h` compat
shim; `dimmit.c` holds `connect`/send similarly. Replace all of it with a new
subsystem exposing an OS-agnostic interface the core calls with **zero
`#ifdef`s**:

```c
/* src/platform/ipc/ipc.h (illustrative; finalize in the plan) */
typedef struct ipc_listener ipc_listener;   /* opaque */
typedef struct ipc_client   ipc_client;     /* opaque */

/* Server (daemon) side. endpoint = socket path (Unix) or pipe name (Windows). */
ipc_listener *ipc_listen(const char *endpoint);
ipc_client   *ipc_accept(ipc_listener *l);   /* blocks until a client or shutdown */
int           ipc_read_line(ipc_client *c, char *buf, int len);  /* raw bytes */
void          ipc_client_close(ipc_client *c);
void          ipc_close(ipc_listener *l);     /* wakes ipc_accept, releases endpoint */

/* Client side. */
int           ipc_send_line(const char *endpoint, const char *line);
```

Backends (selected at build time; the Unix ones share one implementation since
AF_UNIX is identical across them):

- **POSIX / AF_UNIX backend** — the socket + `select`/`accept` logic lifted
  *verbatim* from `dimmitd.c`/`dimmit.c`, behavior-identical. Used by
  `linux`/`darwin`/`netbsd`. `command.c` stays the pure, unit-tested parser, fed
  the bytes `ipc_read_line` returns.
- **Windows named-pipe backend** — `CreateNamedPipe` (one instance per client) /
  `ConnectNamedPipe` / `ReadFile`; the pipe is created with a DACL restricting it
  to the current user; `ipc_close` wakes a blocked `ipc_accept` (overlapped I/O +
  an event, or `CancelIoEx`). No Winsock, so no `WSAStartup`.

Consequences: `dimmitd.c` and `dimmit.c` lose all platform `#ifdef`s and their
`sockaddr_un`/Winsock code; `src/platform/compat/net.h` is superseded and removed;
`access-control/windows.c` becomes real (the pipe DACL) rather than a no-op
relying on the socket file's ACL. `DIMMIT_SOCK` on Windows names the pipe
(default `\\.\pipe\dimmit`); the socket-path default is Unix-only.

### Toolchain & runtime

x86_64 and i686 build with MSVCRT MinGW (`MINGW64` + `MINGW32`), `-static`, so the
only dynamic imports are `msvcrt.dll` plus core OS DLLs (`kernel32`, `user32`,
`dxva2`, `hid`, `advapi32` for the pipe DACL) — all present on Vista+. The CI
DLL-import guard is unchanged in spirit (`msvcrt.dll` is an OS DLL; `-static`
keeps `libwinpthread`/`libgcc` out). arm64 continues on CLANGARM64/UCRT.

### Installer (one universal 32-bit x86 Inno 5.6.1 installer)

Inno Setup's setup stub is a 32-bit x86 executable by nature (Inno 5.6.1 is
x86-only). That is the universal common denominator: it launches natively on
32-bit Windows (XP/Vista/7 x86), via WoW64 on x64, and via the built-in x86
emulation on arm64 (present on all Windows-on-ARM). The installer *engine's*
bitness is orthogonal to which payload it installs.

- **Payload:** bundles x86_64, i686, and arm64 binaries.
- **Native-arch selection:** a `[Code]` routine calls `GetNativeSystemInfo`
  (with `IsWow64Process2` where available) to read the *true* hardware
  architecture — which reports correctly even for the x86-under-emulation stub on
  arm64 — and installs the matching native payload (i686 / x86_64 / arm64).
- **Carried over from the Inno 6 script:** per-user, no-admin
  (Inno 5's `PrivilegesRequired=none` + `DefaultDirName={localappdata}\Dimmit`),
  HKCU\...\Run autostart, user-PATH append with the `NeedsAddPath`/`RemovePath`
  `[Code]`, clean uninstall (stop `dimmitd`, remove Run value + PATH entry +
  files). Inno 5.6.1 supports all of these primitives.
- Replaces `packaging/windows/dimmit.iss` (the Inno 6 version).

### Unchanged subsystems

`ddc/windows.c` (dxva2 Monitor Configuration API, Vista+) and
`input/windows.c` (Raw Input, XP+) need no changes; they already span the range.

## CI / release changes

- The Windows build matrix goes from 2 legs (UCRT64 gcc, CLANGARM64 clang) to
  **3**: `MINGW64`/gcc (x86_64), `MINGW32`/gcc (i686), `CLANGARM64`/clang (arm64).
  This touches `.github/actions/build-windows`, `.github/workflows/ci.yml`, and
  `.github/workflows/release.yml`.
- The installer job installs **Inno Setup 5.6.1** (pinned) instead of Inno 6 and
  compiles the new `.iss`; it downloads all three arch payloads.
- The DLL-import guard runs on all three arches (msvcrt + OS DLLs only).

## Testing & definition of done

- **CI (automated):** all three toolchains **compile + link** `dimmitd`,
  `dimmit-up`, `dimmit-down`, `test_dimmit`; the DLL guard passes on each.
  `test_dimmit` (the pure `command.c` parser + dimmer/controller logic) passes.
  Extend the installer smoke test: after silent install, **run `dimmit-down` and
  assert it exits 0** — i.e. it connected to the autostarted `dimmitd` over the
  **named pipe** — exercising the new IPC transport end-to-end on the runner
  (named pipes work on the Win10 runner too). Then silent uninstall + clean check.
- **Manual (documented, gated on old hardware/VMs):** on 32-bit and 64-bit Vista
  and Windows 7 — install (native i686 / x86_64 lands), confirm `dimmitd`
  autostarts, `dimmit-down` connects, and an external DDC monitor dims.
- **README/docs:** update the supported-Windows floor to Vista SP2; note the
  single universal installer and the named-pipe transport.

## Non-goals

- **Windows XP** — the next sub-project; its DDC/CI path (no Monitor Config API)
  needs a feasibility spike, plus the MSVCRT/subsystem-5.01 build details.
- **Any dual-transport scheme** (AF_UNIX on new Windows + fallback) — one named
  pipe transport for all Windows.
- **Any behavior change on macOS / Linux / NetBSD** — the POSIX ipc backend is a
  verbatim lift of today's AF_UNIX code; the refactor must be behavior-neutral
  there (verified by the existing `test_dimmit` and the Linux/macOS CI).
- Internal-panel handling, code signing, MSIX — tracked separately.
