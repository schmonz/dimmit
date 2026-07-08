# Lowering the Windows Floor to Vista — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make dimmit build and run on Windows down to Vista SP2, by extracting the control channel and shutdown handling into platform abstractions (named pipe on Windows), switching x86/x64 to MSVCRT MinGW, adding a 32-bit build, and shipping one universal installer.

**Architecture:** Two new platform subsystems — `ipc` (listen/accept/read/send) and `shutdown` (stop-signal handler) — remove all `#ifdef`s from `dimmitd.c`/`dimmit.c`. The Unix ipc backend is a verbatim lift of today's AF_UNIX code; the Windows backend becomes a named pipe. Then the Windows x86/x64 toolchain moves to MSVCRT MinGW, an i686 build is added, and the installer is rebuilt on Inno Setup 5.6.1.

**Tech Stack:** C11, MinGW-w64 (MSYS2 MINGW64/MINGW32/CLANGARM64), Win32 named pipes, Inno Setup 5.6.1, CMake/Ninja, GitHub Actions.

## Global Constraints

- **Floor:** Windows **Vista SP2** (set by the dxva2 DDC API). Archs: **x86_64, i686, arm64**.
- **No platform `#ifdef`s in `dimmitd.c` or `dimmit.c`** — platform code lives only in `src/platform/*` backends.
- **Behavior-neutral on macOS/Linux/NetBSD:** the POSIX ipc/shutdown backends must preserve today's behavior exactly (guarded by `test_dimmit` + Linux/macOS CI).
- **Transport:** Windows uses a **named pipe** `\\.\pipe\dimmit` (endpoint over(ridable via `DIMMIT_SOCK`); the pipe DACL restricts to the current user.
- **Toolchain:** x86_64 + i686 build with **MSVCRT MinGW** (`MINGW64`/`MINGW32`, gcc, `-static`); arm64 stays **CLANGARM64/UCRT**. Only `msvcrt.dll` + OS DLLs imported.
- **Installer:** one **Inno Setup 5.6.1** 32-bit installer; native payload chosen via `GetNativeSystemInfo`; per-user (`PrivilegesRequired=none`), `{localappdata}\Dimmit`.
- **Build off the WSL/UNC share:** build out-of-source with the build dir on **local NTFS**.

## File Structure

- Create `src/platform/ipc/ipc.h` — the control-channel interface.
- Create `src/platform/ipc/posix.c` — AF_UNIX backend (linux/darwin/netbsd).
- Create `src/platform/ipc/windows.c` — Windows backend (Task 1: Winsock AF_UNIX lift; Task 3: named pipe).
- Create `src/platform/shutdown/shutdown.h` + `posix.c` + `windows.c` — stop-signal handler.
- Modify `src/dimmitd.c`, `src/dimmit.c` — use `ipc`/`shutdown`; no `#ifdef`s.
- Modify `src/command.c` / `command.h` — keep `parse_command`; drop `read_command` (I/O moves to ipc).
- Delete `src/platform/compat/net.h`.
- Modify `src/test_dimmit.c` — adapt the end-to-end test (no socketpair/`read_command`).
- Modify `CMakeLists.txt` — wire the new backends; drop `net.h`.
- Modify `.github/actions/build-windows/action.yml`, `.github/workflows/ci.yml`, `.github/workflows/release.yml` — 3-arch toolchain matrix + Inno 5.6.1.
- Modify `packaging/windows/dimmit.iss` — Inno 5.6.1 universal installer.
- Modify `README.md` — Vista floor, named pipe, universal installer.

---

### Task 1: Extract the `ipc` abstraction (behavior-neutral, all platforms)

**Files:**
- Create: `src/platform/ipc/ipc.h`, `src/platform/ipc/posix.c`, `src/platform/ipc/windows.c`
- Modify: `src/dimmitd.c`, `src/dimmit.c`, `src/command.c`, `src/command.h`, `src/test_dimmit.c`, `CMakeLists.txt`
- Delete: `src/platform/compat/net.h`

**Interfaces:**
- Produces (`ipc.h`):
  - `ipc_listener *ipc_listen(const char *endpoint);`
  - `int ipc_wait(ipc_listener *l, int timeout_ms);` /* >0 ready, 0 idle, <0 error */
  - `ipc_client *ipc_accept(ipc_listener *l);`
  - `int ipc_read_line(ipc_client *c, char *buf, int buflen);` /* bytes, NUL-terminates; 0 = closed/empty */
  - `int ipc_client_authorized(ipc_client *c);`
  - `void ipc_client_close(ipc_client *c);`
  - `void ipc_close(ipc_listener *l);`
  - `int ipc_send(const char *endpoint, const char *line);` /* client side; 0 ok */
- Consumes: `access-control/access-control.h` (`access_control_is_authorized`), `command.h` (`parse_command`).

- [ ] **Step 1: Create `src/platform/ipc/ipc.h`**

```c
#ifndef DIMMIT_PLATFORM_IPC_H
#define DIMMIT_PLATFORM_IPC_H

/* Control channel between the dimmit-up/down clients and dimmitd. The endpoint
 * is an AF_UNIX socket path on POSIX and a named-pipe name on Windows; callers
 * treat it as an opaque string. All platform specifics live in the backends --
 * dimmitd.c and dimmit.c contain no transport code and no #ifdefs. */

typedef struct ipc_listener ipc_listener;
typedef struct ipc_client   ipc_client;

/* Server side. */
ipc_listener *ipc_listen(const char *endpoint);        /* NULL on failure */
int           ipc_wait(ipc_listener *l, int timeout_ms); /* >0 ready, 0 idle, <0 err */
ipc_client   *ipc_accept(ipc_listener *l);             /* NULL on failure */
int           ipc_read_line(ipc_client *c, char *buf, int buflen); /* bytes; NUL-terminates */
int           ipc_client_authorized(ipc_client *c);    /* 1 = allowed */
void          ipc_client_close(ipc_client *c);
void          ipc_close(ipc_listener *l);              /* release endpoint */

/* Client side: connect, send one line (a trailing '\n' is added if absent), close. */
int           ipc_send(const char *endpoint, const char *line); /* 0 on success */

#endif /* DIMMIT_PLATFORM_IPC_H */
```

- [ ] **Step 2: Create `src/platform/ipc/posix.c` (verbatim lift of today's AF_UNIX code)**

```c
#include "platform/ipc/ipc.h"
#include "platform/access-control/access-control.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#define ACCEPT_BACKLOG 5

struct ipc_listener { int fd; char path[108]; };
struct ipc_client   { int fd; };

ipc_listener *ipc_listen(const char *endpoint) {
    ipc_listener *l = calloc(1, sizeof(*l));
    if (!l) return NULL;
    strncpy(l->path, endpoint, sizeof(l->path) - 1);

    unlink(l->path);
    l->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (l->fd < 0) { perror("socket"); free(l); return NULL; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, l->path, sizeof(addr.sun_path) - 1);

    if (bind(l->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(l->fd); free(l); return NULL;
    }
    if (listen(l->fd, ACCEPT_BACKLOG) < 0) {
        perror("listen"); close(l->fd); unlink(l->path); free(l); return NULL;
    }
    return l;
}

int ipc_wait(ipc_listener *l, int timeout_ms) {
    fd_set fds;
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    FD_ZERO(&fds);
    FD_SET(l->fd, &fds);
    int r = select(l->fd + 1, &fds, NULL, NULL, &tv);
    if (r < 0) return (errno == EINTR) ? 0 : -1;   /* EINTR -> treat as idle tick */
    return r;   /* 0 idle, >0 ready */
}

ipc_client *ipc_accept(ipc_listener *l) {
    int fd = accept(l->fd, NULL, NULL);
    if (fd < 0) return NULL;
    ipc_client *c = calloc(1, sizeof(*c));
    if (!c) { close(fd); return NULL; }
    c->fd = fd;
    return c;
}

int ipc_read_line(ipc_client *c, char *buf, int buflen) {
    int n = (int)recv(c->fd, buf, buflen - 1, 0);
    if (n <= 0) { buf[0] = '\0'; return 0; }
    buf[n] = '\0';
    return n;
}

int ipc_client_authorized(ipc_client *c) {
    return access_control_is_authorized(c->fd);
}

void ipc_client_close(ipc_client *c) {
    if (!c) return;
    close(c->fd);
    free(c);
}

void ipc_close(ipc_listener *l) {
    if (!l) return;
    close(l->fd);
    unlink(l->path);
    free(l);
}

int ipc_send(const char *endpoint, const char *line) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, endpoint, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(fd); return -1;
    }
    size_t len = strlen(line);
    if (send(fd, line, len, 0) < 0) perror("send");
    if ((len == 0 || line[len - 1] != '\n') && send(fd, "\n", 1, 0) < 0) perror("send");
    close(fd);
    return 0;
}
```

- [ ] **Step 3: Create `src/platform/ipc/windows.c` (Task 1 = Winsock AF_UNIX lift, keeps current behavior)**

```c
#include "platform/ipc/ipc.h"

#include <winsock2.h>
#include <afunix.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define ACCEPT_BACKLOG 5

struct ipc_listener { SOCKET fd; char path[108]; };
struct ipc_client   { SOCKET fd; };

static int wsa_started = 0;
static int ensure_wsa(void) {
    if (wsa_started) return 0;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    wsa_started = 1;
    return 0;
}

ipc_listener *ipc_listen(const char *endpoint) {
    if (ensure_wsa() != 0) return NULL;
    ipc_listener *l = calloc(1, sizeof(*l));
    if (!l) return NULL;
    strncpy(l->path, endpoint, sizeof(l->path) - 1);

    DeleteFileA(l->path);
    l->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (l->fd == INVALID_SOCKET) { free(l); return NULL; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, l->path, sizeof(addr.sun_path) - 1);

    if (bind(l->fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(l->fd); free(l); return NULL;
    }
    if (listen(l->fd, ACCEPT_BACKLOG) == SOCKET_ERROR) {
        closesocket(l->fd); DeleteFileA(l->path); free(l); return NULL;
    }
    return l;
}

int ipc_wait(ipc_listener *l, int timeout_ms) {
    fd_set fds;
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    FD_ZERO(&fds);
    FD_SET(l->fd, &fds);
    int r = select(0, &fds, NULL, NULL, &tv);   /* nfds ignored on Winsock */
    return r == SOCKET_ERROR ? -1 : r;
}

ipc_client *ipc_accept(ipc_listener *l) {
    SOCKET fd = accept(l->fd, NULL, NULL);
    if (fd == INVALID_SOCKET) return NULL;
    ipc_client *c = calloc(1, sizeof(*c));
    if (!c) { closesocket(fd); return NULL; }
    c->fd = fd;
    return c;
}

int ipc_read_line(ipc_client *c, char *buf, int buflen) {
    int n = recv(c->fd, buf, buflen - 1, 0);
    if (n <= 0) { buf[0] = '\0'; return 0; }
    buf[n] = '\0';
    return n;
}

int ipc_client_authorized(ipc_client *c) { (void)c; return 1; }  /* socket-file ACL today */

void ipc_client_close(ipc_client *c) {
    if (!c) return;
    closesocket(c->fd);
    free(c);
}

void ipc_close(ipc_listener *l) {
    if (!l) return;
    closesocket(l->fd);
    DeleteFileA(l->path);
    free(l);
}

int ipc_send(const char *endpoint, const char *line) {
    if (ensure_wsa() != 0) return -1;
    SOCKET fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, endpoint, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(fd); return -1;
    }
    int len = (int)strlen(line);
    send(fd, line, len, 0);
    if (len == 0 || line[len - 1] != '\n') send(fd, "\n", 1, 0);
    closesocket(fd);
    return 0;
}
```

- [ ] **Step 4: Simplify `src/command.h` and `src/command.c` (drop `read_command`, drop net.h)**

`src/command.h` — replace whole file:
```c
#ifndef COMMAND_H
#define COMMAND_H

/* Map a textual command to a direction: +1 (up), -1 (down), 0 (unrecognized).
 * Pure parsing; transport reads live in the ipc backend (ipc_read_line). */
int parse_command(const char *cmd);

#endif /* COMMAND_H */
```
`src/command.c` — replace whole file:
```c
#include "command.h"

#include <string.h>

int parse_command(const char *cmd) {
    if (strcmp(cmd, "up") == 0) return +1;
    if (strcmp(cmd, "down") == 0) return -1;
    return 0;
}
```

- [ ] **Step 5: Rewire `src/dimmitd.c` to use `ipc` (this removes its socket `#ifdef`s; the signal `#ifdef` is Task 2)**

Replace the top includes block (lines 1–26) with:
```c
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "display_controller.h"
#include "platform/access-control/access-control.h"
#include "platform/logging/logging.h"
#include "platform/input/input.h"
#include "platform/ipc/ipc.h"
#include "command.h"
#include "config.h"
```
Rename `get_sock_path` to `get_endpoint` (same body). Replace the entire `main()` (lines 113–244) with (note: the `#ifdef _WIN32` console/signal block at lines 55–66 and its call at 125–134 remain for Task 2):
```c
int main(void) {
    ipc_listener *lst = NULL;
    pthread_t worker;
    int worker_started = 0;
    const char *endpoint = get_endpoint();

    if (logging_init() < 0)
        fprintf(stderr, "Warning: logging not redirected; continuing\n");

#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
#endif

    if (access_control_before_bind() < 0)
        return 1;
    if (init_monitor() < 0)
        return 1;

    if (pthread_create(&worker, NULL, brightness_worker, NULL) != 0) {
        perror("pthread_create");
        goto cleanup;
    }
    worker_started = 1;

    input_start(adjust_fraction);

    lst = ipc_listen(endpoint);
    if (!lst) {
        fprintf(stderr, "Failed to listen on %s\n", endpoint);
        goto cleanup;
    }

    if (access_control_after_bind(endpoint) < 0)
        fprintf(stderr, "Warning: could not apply access policy\n");

    printf("Listening on %s\n", endpoint);

    while (running) {
        int r = ipc_wait(lst, 1000);
        if (r < 0) break;
        if (r == 0) {   /* idle tick: poll for hotplugged/unplugged displays */
            pthread_mutex_lock(&lock);
            controller_reconcile(ctrl);
            pthread_mutex_unlock(&lock);
            continue;
        }

        ipc_client *c = ipc_accept(lst);
        if (!c)
            continue;
        if (!ipc_client_authorized(c)) {
            fprintf(stderr, "Access denied\n");
            ipc_client_close(c);
            continue;
        }

        char buf[16];
        int n = ipc_read_line(c, buf, sizeof(buf));
        if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = '\0';
        int dir = (n > 0) ? parse_command(buf) : 0;
        if (dir != 0)
            adjust_fraction(dir * DIMMIT_SOCKET_FRACTION);
        else
            fprintf(stderr, "Ignoring empty or unknown command\n");

        ipc_client_close(c);
    }

cleanup:
    running = 0;
    input_stop();
    if (worker_started) {
        pthread_cond_signal(&cond);
        pthread_join(worker, NULL);
    }
    if (lst) ipc_close(lst);
    if (ctrl) controller_close(ctrl);
    return 0;
}
```
Also delete the now-unused `#include "platform/compat/net.h"` usages and the `unlink`/`_unlink` define (the include block above already dropped them). Keep the `#ifdef _WIN32` include for `<windows.h>`/`<signal.h>` that the console/signal handlers need — Task 2 removes it. To keep Task 1 compiling, add right after the include block:
```c
#ifdef _WIN32
  #include <windows.h>
#else
  #include <signal.h>
#endif
```

- [ ] **Step 6: Rewire `src/dimmit.c` to use `ipc_send` (removes its socket code)**

Replace whole file:
```c
#include <stdio.h>
#include <string.h>
#include "platform/ipc/ipc.h"
#include "config.h"

static const char *get_endpoint(void) {
    const char *path = getenv("DIMMIT_SOCK");
    return path ? path : DIMMIT_SOCK_DEFAULT;
}

int main(int argc, char **argv) {
    if (argc != 1) {
        fprintf(stderr, "Usage: %s (no arguments)\n", argv[0]);
        return 1;
    }

    const char *base = argv[0];
    for (const char *p = argv[0]; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    char prog[64];
    strncpy(prog, base, sizeof(prog) - 1);
    prog[sizeof(prog) - 1] = '\0';
    size_t plen = strlen(prog);
    if (plen >= 4 && strcmp(prog + plen - 4, ".exe") == 0) prog[plen - 4] = '\0';

    const char *msg;
    if (strcmp(prog, "dimmit-up") == 0)        msg = "up\n";
    else if (strcmp(prog, "dimmit-down") == 0) msg = "down\n";
    else {
        fprintf(stderr, "%s: unknown invocation name (expected 'dimmit-up' or 'dimmit-down')\n", prog);
        return 1;
    }

    return ipc_send(get_endpoint(), msg) == 0 ? 0 : 1;
}
```
`<stdlib.h>` is needed for `getenv`; add `#include <stdlib.h>` at the top.

- [ ] **Step 7: Adapt `src/test_dimmit.c` (the end-to-end test no longer uses socketpair/`read_command`)**

Replace the whole `test_command_loop_end_to_end` function (lines 161–189) with a transport-free version that keeps the parse→dimmer→mock-display coverage:
```c
/* The socket transport now lives in the ipc backend (platform glue, covered by
 * the CI named-pipe smoke). Here we keep the logic coverage: a parsed direction
 * becomes a fraction step that actually moves the mock display. */
static void test_command_maps_to_display_move(void) {
    CHECK(parse_command("down") == -1);
    CHECK(parse_command("up") == +1);

    mock_reset(1, (int[]){50}, (int[]){100});
    display_controller *c = controller_open();
    controller_adjust(c, parse_command("down") * (1.0/16.0));
    controller_service(c);
    CHECK(controller_current(c, 0) == 44);
    controller_close(c);
    mock_reset(1, (int[]){50}, (int[]){100});
}
```
Remove the now-unused socket includes at the top of `test_dimmit.c` (lines 17–19: the `#ifndef _WIN32` / `<sys/socket.h>` block for socketpair) and the `command.h`/`net.h` transport references it no longer needs (keep `#include "command.h"` for `parse_command`). Update `main()` (line 303) to call `test_command_maps_to_display_move();` instead of `test_command_loop_end_to_end();`.

- [ ] **Step 8: Update `CMakeLists.txt` — add the ipc backend, drop net.h references**

The `dimmit_add_platform_backend` helper expects `src/platform/<subsystem>/<system>.c`, but the ipc Unix impl is shared. Add a dedicated selector right after the four existing `dimmit_add_platform_backend(dimmitd ...)` calls:
```cmake
# ipc: one Windows backend, one shared POSIX backend (AF_UNIX is identical across
# the Unix platforms, so they share posix.c rather than three identical files).
if (WIN32)
    target_sources(dimmitd PRIVATE src/platform/ipc/windows.c)
else()
    target_sources(dimmitd PRIVATE src/platform/ipc/posix.c)
endif()
```
Add the same block for the clients (they need `ipc_send`): after the `add_executable(dimmit-up ...)`/`dimmit-down` lines and their `target_include_directories`, add:
```cmake
foreach(client dimmit-up dimmit-down)
    if (WIN32)
        target_sources(${client} PRIVATE src/platform/ipc/windows.c)
    else()
        target_sources(${client} PRIVATE src/platform/ipc/posix.c)
    endif()
endforeach()
```
`test_dimmit` no longer compiles `command.c`'s transport (it only needs `parse_command`); it already lists `src/command.c` — keep it. Remove nothing else. `net.h` is deleted; confirm no remaining references:
```bash
grep -rn "compat/net.h\|read_command\|net_startup\|net_close\|dimmit_sock_t" src/ | grep -v Binary
```
Expected: no matches.

- [ ] **Step 9: Delete `src/platform/compat/net.h`**

```bash
git rm src/platform/compat/net.h
```

- [ ] **Step 10: Build + test on Linux (behavior-neutral check)**

Run (WSL, gcc — the project builds without cmake there via the test compile used before, but prefer a real configure if cmake is available; otherwise compile test_dimmit directly):
```bash
wsl.exe -d Ubuntu -- bash -lc 'cd ~/code/trees/dimmit && gcc -std=c11 -I. -Isrc \
  src/test_dimmit.c src/dimmer.c src/command.c src/brightness.c src/display_controller.c \
  src/platform/ddc/abstraction.c src/platform/ddc/in_memory_mock.c src/platform/access-control/mock.c \
  -lm -o /tmp/td && /tmp/td'
```
Expected: `All NN checks passed`. (The ipc backend isn't linked into `test_dimmit`; this verifies the parser/controller refactor is intact.)

- [ ] **Step 11: Build on Windows (UCRT64, current toolchain) — confirms the ipc extraction still builds + runs**

```
MSYSTEM=UCRT64 /c/msys64/usr/bin/bash.exe -lc 'cmake -S "//wsl.localhost/Ubuntu/home/schmonz/code/trees/dimmit" -B /c/Users/schmonz/dimmit-bw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_EXE_LINKER_FLAGS="-static" && cmake --build /c/Users/schmonz/dimmit-bw'
```
Expected: builds; `dimmitd.exe`, `dimmit-up.exe`, `dimmit-down.exe`, `test_dimmit.exe` link. Then a round-trip smoke (PowerShell, local cwd):
```powershell
Set-Location $env:USERPROFILE
$env:DIMMIT_SOCK = 'C:/Users/Public/dimmit-t1.sock'
$d = Start-Process 'C:\Users\schmonz\dimmit-bw\dimmitd.exe' -PassThru -WindowStyle Hidden
Start-Sleep 1
$c = Start-Process 'C:\Users\schmonz\dimmit-bw\dimmit-down.exe' -PassThru -Wait -WindowStyle Hidden
"client exit: $($c.ExitCode)"
Stop-Process -Id $d.Id -Force
```
Expected: `client exit: 0` (client connected to the daemon over AF_UNIX, unchanged behavior).

- [ ] **Step 12: Commit**

```bash
git add -A
git commit -m "refactor(ipc): extract control channel behind a platform abstraction"
```

---

### Task 2: Extract the `shutdown` abstraction (removes the last `#ifdef`s from dimmitd.c)

**Files:**
- Create: `src/platform/shutdown/shutdown.h`, `src/platform/shutdown/posix.c`, `src/platform/shutdown/windows.c`
- Modify: `src/dimmitd.c`, `CMakeLists.txt`

**Interfaces:**
- Produces (`shutdown.h`): `void shutdown_install(void (*on_stop)(void));`
- Consumes: nothing.

- [ ] **Step 1: Create `src/platform/shutdown/shutdown.h`**

```c
#ifndef DIMMIT_PLATFORM_SHUTDOWN_H
#define DIMMIT_PLATFORM_SHUTDOWN_H

/* Install a handler for "please stop" signals -- SIGINT/SIGTERM on POSIX,
 * console control events on Windows. on_stop is invoked (possibly on another
 * thread) when the OS asks the daemon to shut down; it should only set a flag. */
void shutdown_install(void (*on_stop)(void));

#endif /* DIMMIT_PLATFORM_SHUTDOWN_H */
```

- [ ] **Step 2: Create `src/platform/shutdown/posix.c`**

```c
#include "platform/shutdown/shutdown.h"
#include <signal.h>

static void (*g_on_stop)(void) = NULL;

static void sighandler(int sig) { (void)sig; if (g_on_stop) g_on_stop(); }

void shutdown_install(void (*on_stop)(void)) {
    g_on_stop = on_stop;
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
}
```

- [ ] **Step 3: Create `src/platform/shutdown/windows.c`**

```c
#include "platform/shutdown/shutdown.h"
#include <windows.h>

static void (*g_on_stop)(void) = NULL;

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    (void)ctrl_type;   /* Ctrl-C/close/logoff/shutdown all mean: stop. */
    if (g_on_stop) g_on_stop();
    return TRUE;
}

void shutdown_install(void (*on_stop)(void)) {
    g_on_stop = on_stop;
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
}
```

- [ ] **Step 4: Rewire `src/dimmitd.c` — drop the signal/console `#ifdef`s entirely**

Delete the `console_ctrl_handler`/`sighandler` block (old lines 55–66) and the `#ifdef _WIN32 #include <windows.h> #else #include <signal.h> #endif` block added in Task 1 Step 5. Add a small stop callback and include the header. After the `static volatile int running = 1;` line add:
```c
static void request_stop(void) { running = 0; }
```
Add to the includes: `#include "platform/shutdown/shutdown.h"`. In `main()`, replace the `#ifdef _WIN32 ... SetConsoleCtrlHandler ... #else ... signal(...) ... #endif` block with a single line:
```c
    shutdown_install(request_stop);
```
Verify `dimmitd.c` now has **zero** `#ifdef`:
```bash
grep -n "ifdef\|ifndef\|_WIN32" src/dimmitd.c
```
Expected: no matches.

- [ ] **Step 5: Wire the shutdown backend in `CMakeLists.txt`**

After the ipc block from Task 1 Step 8, add:
```cmake
dimmit_add_platform_backend(dimmitd shutdown)
```
(`shutdown/<system>.c` — but the POSIX impl is shared. Since `dimmit_add_platform_backend` looks for `<system_lower>.c`, create thin per-system files OR use the same `if (WIN32)` selector. Use the selector for consistency with ipc:)
```cmake
if (WIN32)
    target_sources(dimmitd PRIVATE src/platform/shutdown/windows.c)
else()
    target_sources(dimmitd PRIVATE src/platform/shutdown/posix.c)
endif()
```
(Remove the `dimmit_add_platform_backend(dimmitd shutdown)` line if you added it — use only the selector.)

- [ ] **Step 6: Build + test (Linux compile of test_dimmit unaffected; Windows build)**

Linux: rerun Task 1 Step 10 — expected `All NN checks passed`.
Windows: rerun Task 1 Step 11 build + round-trip smoke — expected `client exit: 0`.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor(shutdown): extract stop-signal handling; dimmitd.c is now ifdef-free"
```

---

### Task 3: Windows named-pipe ipc backend (Vista+ transport)

**Files:**
- Modify (replace whole file): `src/platform/ipc/windows.c`
- Modify: `CMakeLists.txt` (the Windows `dimmitd` link line — add `advapi32` for the DACL)

**Interfaces:** unchanged (same `ipc.h`); the endpoint string is now a pipe name.

- [ ] **Step 1: Change the Windows default endpoint to a pipe name**

In `CMakeLists.txt`, the `WIN32` branch sets `DIMMIT_SOCK_DEFAULT`. Replace:
```cmake
    set(DIMMIT_SOCK_DEFAULT "C:/Users/Public/dimmit.sock" CACHE STRING "Default Unix socket path for dimmit")
```
with:
```cmake
    set(DIMMIT_SOCK_DEFAULT "\\\\.\\pipe\\dimmit" CACHE STRING "Default control-channel endpoint (named pipe) for dimmit")
```

- [ ] **Step 2: Replace `src/platform/ipc/windows.c` with the named-pipe backend**

```c
#include "platform/ipc/ipc.h"

#include <windows.h>
#include <sddl.h>
#include <string.h>
#include <stdlib.h>

/* Windows control channel over a named pipe. One pipe instance per client; a
 * persistent "pending" instance with an overlapped ConnectNamedPipe lets
 * ipc_wait() block with a timeout (so the daemon still polls hotplug + notices
 * shutdown). The pipe's DACL restricts access to the current user, giving real
 * access control (ipc_client_authorized therefore just returns 1). */

#define PIPE_BUFSZ 512

struct ipc_listener {
    char       name[256];
    HANDLE     pending;     /* current instance awaiting a client */
    OVERLAPPED ov;          /* overlapped state for `pending` */
    HANDLE     event;       /* ov.hEvent; signalled when a client connects */
    int        connected;   /* 1 once the current pending op has completed */
    SECURITY_ATTRIBUTES sa;
    PSECURITY_DESCRIPTOR sd;
};
struct ipc_client { HANDLE pipe; };

/* Create a new pipe instance and start an overlapped ConnectNamedPipe on it. */
static int arm_pending(ipc_listener *l) {
    l->pending = CreateNamedPipeA(
        l->name,
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES, 0, PIPE_BUFSZ, 0, &l->sa);
    if (l->pending == INVALID_HANDLE_VALUE) return -1;

    ResetEvent(l->event);
    memset(&l->ov, 0, sizeof(l->ov));
    l->ov.hEvent = l->event;
    l->connected = 0;

    if (ConnectNamedPipe(l->pending, &l->ov)) { l->connected = 1; return 0; }
    DWORD e = GetLastError();
    if (e == ERROR_PIPE_CONNECTED) { l->connected = 1; SetEvent(l->event); return 0; }
    if (e == ERROR_IO_PENDING)     { return 0; }
    CloseHandle(l->pending); l->pending = INVALID_HANDLE_VALUE;
    return -1;
}

ipc_listener *ipc_listen(const char *endpoint) {
    ipc_listener *l = calloc(1, sizeof(*l));
    if (!l) return NULL;
    strncpy(l->name, endpoint, sizeof(l->name) - 1);

    /* DACL: allow only the current user (owner) and SYSTEM. "D:P(A;;GA;;;OW)
     * (A;;GA;;;SY)" -- protected DACL, generic-all to OWNER RIGHTS + SYSTEM. */
    l->sd = NULL;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorA(
            "D:P(A;;GA;;;OW)(A;;GA;;;SY)", SDDL_REVISION_1, &l->sd, NULL)) {
        l->sa.nLength = sizeof(l->sa);
        l->sa.lpSecurityDescriptor = l->sd;
        l->sa.bInheritHandle = FALSE;
    } else {
        l->sa.nLength = sizeof(l->sa);
        l->sa.lpSecurityDescriptor = NULL;   /* fall back to default (still owner-scoped) */
        l->sa.bInheritHandle = FALSE;
    }

    l->event = CreateEvent(NULL, TRUE, FALSE, NULL);   /* manual-reset */
    if (!l->event) { if (l->sd) LocalFree(l->sd); free(l); return NULL; }

    if (arm_pending(l) != 0) {
        CloseHandle(l->event); if (l->sd) LocalFree(l->sd); free(l); return NULL;
    }
    return l;
}

int ipc_wait(ipc_listener *l, int timeout_ms) {
    if (l->connected) return 1;
    DWORD w = WaitForSingleObject(l->event, (DWORD)timeout_ms);
    if (w == WAIT_TIMEOUT) return 0;
    if (w != WAIT_OBJECT_0) return -1;
    DWORD dummy = 0;
    if (!GetOverlappedResult(l->pending, &l->ov, &dummy, FALSE)) {
        /* connect failed; recycle the instance and report idle so the loop retries */
        CloseHandle(l->pending);
        if (arm_pending(l) != 0) return -1;
        return 0;
    }
    l->connected = 1;
    return 1;
}

ipc_client *ipc_accept(ipc_listener *l) {
    if (!l->connected) return NULL;
    ipc_client *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->pipe = l->pending;          /* hand the connected instance to the caller */
    l->pending = INVALID_HANDLE_VALUE;
    l->connected = 0;
    if (arm_pending(l) != 0) {      /* re-arm for the next client */
        /* if re-arm fails, the next ipc_wait returns error; the caller still gets c */
        l->pending = INVALID_HANDLE_VALUE;
    }
    return c;
}

int ipc_read_line(ipc_client *c, char *buf, int buflen) {
    DWORD n = 0;
    if (!ReadFile(c->pipe, buf, (DWORD)(buflen - 1), &n, NULL) || n == 0) {
        buf[0] = '\0';
        return 0;
    }
    buf[n] = '\0';
    return (int)n;
}

int ipc_client_authorized(ipc_client *c) { (void)c; return 1; }  /* pipe DACL gates access */

void ipc_client_close(ipc_client *c) {
    if (!c) return;
    if (c->pipe != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(c->pipe);
        DisconnectNamedPipe(c->pipe);
        CloseHandle(c->pipe);
    }
    free(c);
}

void ipc_close(ipc_listener *l) {
    if (!l) return;
    if (l->pending != INVALID_HANDLE_VALUE) {
        CancelIoEx(l->pending, &l->ov);
        CloseHandle(l->pending);
    }
    if (l->event) CloseHandle(l->event);
    if (l->sd) LocalFree(l->sd);
    free(l);
}

int ipc_send(const char *endpoint, const char *line) {
    /* Wait briefly for an instance, then connect and write one line. */
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int tries = 0; tries < 20; tries++) {
        h = CreateFileA(endpoint, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_PIPE_BUSY) return -1;
        if (!WaitNamedPipeA(endpoint, 200)) { /* keep trying up to the loop bound */ }
    }
    if (h == INVALID_HANDLE_VALUE) return -1;

    size_t len = strlen(line);
    DWORD written = 0;
    BOOL ok = WriteFile(h, line, (DWORD)len, &written, NULL);
    if (ok && (len == 0 || line[len - 1] != '\n')) {
        DWORD w2 = 0;
        WriteFile(h, "\n", 1, &w2, NULL);
    }
    CloseHandle(h);
    return ok ? 0 : -1;
}
```

- [ ] **Step 3: Add `advapi32` to the Windows `dimmitd` link (for the SDDL/DACL calls)**

In `CMakeLists.txt`, the `WIN32` branch links `dimmitd`. Update:
```cmake
    target_link_libraries(dimmitd PRIVATE dxva2 ws2_32 user32 hid advapi32)
```
The clients (`dimmit-up`/`dimmit-down`) now use only `CreateFileA`/`WriteFile` (kernel32) — they no longer need `ws2_32`. Update their Windows link block:
```cmake
if (WIN32)
    target_link_libraries(dimmit-up PRIVATE)
    target_link_libraries(dimmit-down PRIVATE)
endif()
```
(Leaving them with no extra libs is fine; kernel32 links by default. If CMake dislikes an empty `target_link_libraries`, delete these two lines entirely instead.)

- [ ] **Step 4: Build on Windows (UCRT64) and smoke the named-pipe round-trip**

Build per Task 1 Step 11, then:
```powershell
Set-Location $env:USERPROFILE
Remove-Item Env:\DIMMIT_SOCK -ErrorAction SilentlyContinue   # use the default pipe name
$d = Start-Process 'C:\Users\schmonz\dimmit-bw\dimmitd.exe' -PassThru -WindowStyle Hidden
Start-Sleep 1
$c = Start-Process 'C:\Users\schmonz\dimmit-bw\dimmit-down.exe' -PassThru -Wait -WindowStyle Hidden
"client exit: $($c.ExitCode)"
Stop-Process -Id $d.Id -Force
```
Expected: `client exit: 0` — the client connected to the daemon over `\\.\pipe\dimmit` and delivered "down". (Dimming a real monitor is the manual, hardware-gated check.)

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(ipc): Windows named-pipe backend (Vista+, no AF_UNIX)"
```

---

### Task 4: Toolchain — MSVCRT MinGW for x86/x64, add i686 build

**Files:**
- Modify: `.github/actions/build-windows/action.yml`, `.github/workflows/ci.yml`, `.github/workflows/release.yml`

**Interfaces:** none (CI only). Produces per-arch artifacts named `dimmit-windows-{x86_64,i686,arm64}`.

- [ ] **Step 1: Local sanity — the x64 MSVCRT build works**

Confirm the code builds under the MSVCRT toolchain locally before touching CI:
```
MSYSTEM=MINGW64 /c/msys64/usr/bin/bash.exe -lc 'pacman -S --needed --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja >/dev/null 2>&1; cmake -S "//wsl.localhost/Ubuntu/home/schmonz/code/trees/dimmit" -B /c/Users/schmonz/dimmit-mingw64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_EXE_LINKER_FLAGS="-static" && cmake --build /c/Users/schmonz/dimmit-mingw64'
```
Expected: builds + links. Then confirm imports are OS-only (msvcrt allowed):
```
MSYSTEM=MINGW64 /c/msys64/usr/bin/bash.exe -lc 'objdump -p /c/Users/schmonz/dimmit-mingw64/dimmitd.exe | grep -i "DLL Name"'
```
Expected: `msvcrt.dll`, `KERNEL32.dll`, `USER32.dll`, `dxva2.dll`, `hid`/`ADVAPI32`/`WS2_32` — all OS DLLs; **no** `libwinpthread`/`libgcc`/`api-ms-win-crt-*`.

- [ ] **Step 2: Update `.github/actions/build-windows/action.yml` for the MSVCRT/msvcrt guard**

The action already parameterizes `msystem`/`prefix`/`cc`. Its DLL guard forbids `libwinpthread|libgcc_s|libstdc++|libc++|libunwind`. Add `api-ms-win-crt` to the forbidden set (an MSVCRT build must not import UCRT), so the guard also catches an accidental UCRT slip. In `.github/actions/build-windows/action.yml`, the guard step's `grep -Eqi` pattern:
```yaml
          if echo "$deps" | grep -Eqi 'libwinpthread|libgcc_s|libstdc\+\+|libc\+\+|libunwind'; then
```
becomes:
```yaml
          if echo "$deps" | grep -Eqi 'libwinpthread|libgcc_s|libstdc\+\+|libc\+\+|libunwind|api-ms-win-crt'; then
```
(arm64/CLANGARM64 is UCRT-based and *does* import `api-ms-win-crt-*` — that's fine on Win10-only arm64. Guard that leg differently: gate the new token on a non-UCRT input. Add an input `allow_ucrt` defaulting to `false`, set `true` for the arm64 matrix leg, and only append `|api-ms-win-crt` to the pattern when `allow_ucrt` != `true`. Implement with a shell var in the guard step:)
```yaml
        run: |
          OBJDUMP="$(command -v objdump || command -v llvm-objdump || true)"
          if [ -z "$OBJDUMP" ]; then echo "no objdump; skipping guard"; exit 0; fi
          FORBID='libwinpthread|libgcc_s|libstdc\+\+|libc\+\+|libunwind'
          if [ "${{ inputs.allow_ucrt }}" != "true" ]; then FORBID="$FORBID|api-ms-win-crt"; fi
          for exe in build/dimmitd.exe build/dimmit-up.exe build/dimmit-down.exe; do
            deps="$("$OBJDUMP" -p "$exe" || true)"
            echo "== $exe =="; echo "$deps" | grep -i 'DLL Name' || true
            if echo "$deps" | grep -Eqi "$FORBID"; then
              echo "::error::non-system runtime DLL linked into $exe"; exit 1
            fi
          done
```
And add to the action's `inputs:`:
```yaml
  allow_ucrt:
    required: false
    default: "false"
```

- [ ] **Step 3: Update the `windows` matrix in `.github/workflows/ci.yml` to three legs**

Replace the `matrix.include` list under the `windows` job with:
```yaml
        include:
          - arch: x86_64
            runner: windows-latest
            msystem: MINGW64
            prefix: mingw-w64-x86_64
            cc: gcc
            allow_ucrt: "false"
          - arch: i686
            runner: windows-latest
            msystem: MINGW32
            prefix: mingw-w64-i686
            cc: gcc
            allow_ucrt: "false"
          - arch: arm64
            runner: windows-11-arm
            msystem: CLANGARM64
            prefix: mingw-w64-clang-aarch64
            cc: clang
            allow_ucrt: "true"
```
and pass the new input in the step:
```yaml
      - name: Build and test
        uses: ./.github/actions/build-windows
        with:
          msystem: ${{ matrix.msystem }}
          prefix: ${{ matrix.prefix }}
          cc: ${{ matrix.cc }}
          allow_ucrt: ${{ matrix.allow_ucrt }}
```

- [ ] **Step 4: Mirror the matrix into `.github/workflows/release.yml`**

Apply the identical three-leg `matrix.include` and the `allow_ucrt` input to the `release-windows` job. The "Stage binaries" step's `payload-${{ matrix.arch }}` and the upload name `dimmit-windows-${{ matrix.arch }}` already parameterize on `arch`, so x86_64/i686/arm64 artifacts are produced automatically.

- [ ] **Step 5: Validate YAML**

```bash
wsl.exe -d Ubuntu -- bash -lc 'cd ~/code/trees/dimmit && for f in ci release; do python3 -c "import yaml; yaml.safe_load(open(\".github/workflows/$f.yml\")); print(\"$f OK\")"; done'
```
Expected: `ci OK` and `release OK`. Full validation is the next CI run.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "ci(windows): MSVCRT MinGW for x86/x64 + i686 leg; guard against UCRT slip"
```

---

### Task 5: One universal Inno Setup 5.6.1 installer

**Files:**
- Modify (replace whole file): `packaging/windows/dimmit.iss`
- Modify: `.github/workflows/release.yml` (installer job → Inno 5.6.1 + three payloads)

**Interfaces:** produces `dimmit-<ver>-windows-setup.exe` (single, universal).

- [ ] **Step 1: Replace `packaging/windows/dimmit.iss` with an Inno 5.6.1 script**

```
; Inno Setup 5.6.1 script for dimmit. One 32-bit x86 installer that runs on
; every supported Windows (Vista+; the stub runs via WoW64 on x64 and x86
; emulation on arm64) and installs the NATIVE payload, chosen by GetNativeSystemInfo.

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef X64Dir
  #define X64Dir "payload\x64"
#endif
#ifndef X86Dir
  #define X86Dir "payload\x86"
#endif
#ifndef Arm64Dir
  #define Arm64Dir "payload\arm64"
#endif

[Setup]
AppId={{6D2B7C41-1E5A-4B93-9F1C-DEC0DE01D177}
AppName=Dimmit
AppVersion={#AppVersion}
AppPublisher=Amitai Schleier
AppPublisherURL=https://github.com/schmonz/dimmit
DefaultDirName={localappdata}\Dimmit
PrivilegesRequired=none
OutputBaseFilename=dimmit-{#AppVersion}-windows-setup
OutputDir=.
Uninstallable=yes
ChangesEnvironment=yes
DisableProgramGroupPage=yes
DisableDirPage=yes

[Files]
Source: "{#X64Dir}\dimmitd.exe";      DestDir: "{app}"; Check: IsArch('x64');   Flags: ignoreversion
Source: "{#X64Dir}\dimmit-up.exe";    DestDir: "{app}"; Check: IsArch('x64');   Flags: ignoreversion
Source: "{#X64Dir}\dimmit-down.exe";  DestDir: "{app}"; Check: IsArch('x64');   Flags: ignoreversion
Source: "{#X86Dir}\dimmitd.exe";      DestDir: "{app}"; Check: IsArch('x86');   Flags: ignoreversion
Source: "{#X86Dir}\dimmit-up.exe";    DestDir: "{app}"; Check: IsArch('x86');   Flags: ignoreversion
Source: "{#X86Dir}\dimmit-down.exe";  DestDir: "{app}"; Check: IsArch('x86');   Flags: ignoreversion
Source: "{#Arm64Dir}\dimmitd.exe";    DestDir: "{app}"; Check: IsArch('arm64'); Flags: ignoreversion
Source: "{#Arm64Dir}\dimmit-up.exe";  DestDir: "{app}"; Check: IsArch('arm64'); Flags: ignoreversion
Source: "{#Arm64Dir}\dimmit-down.exe";DestDir: "{app}"; Check: IsArch('arm64'); Flags: ignoreversion

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
  ValueData: "{olddata};{app}"; Check: NeedsAddPath(ExpandConstant('{app}'))
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "Dimmit"; ValueData: """{app}\dimmitd.exe"""; \
  Flags: uninsdeletevalue

[Run]
Filename: "{app}\dimmitd.exe"; Flags: nowait runhidden skipifsilent
Filename: "{app}\dimmitd.exe"; Flags: nowait runhidden; Check: WizardSilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im dimmitd.exe"; Flags: runhidden

[Code]
const PROCESSOR_ARCHITECTURE_AMD64 = 9;
const PROCESSOR_ARCHITECTURE_ARM64 = 12;
const PROCESSOR_ARCHITECTURE_INTEL = 0;

type
  TSystemInfo = record
    wProcessorArchitecture: Word;
    wReserved: Word;
    dwPageSize: DWORD;
    lpMinimumApplicationAddress: DWORD;
    lpMaximumApplicationAddress: DWORD;
    dwActiveProcessorMask: DWORD;
    dwNumberOfProcessors: DWORD;
    dwProcessorType: DWORD;
    dwAllocationGranularity: DWORD;
    wProcessorLevel: Word;
    wProcessorRevision: Word;
  end;

procedure GetNativeSystemInfo(var lpSystemInfo: TSystemInfo);
  external 'GetNativeSystemInfo@kernel32.dll stdcall';

function NativeArch(): String;
var si: TSystemInfo;
begin
  GetNativeSystemInfo(si);
  case si.wProcessorArchitecture of
    PROCESSOR_ARCHITECTURE_ARM64: Result := 'arm64';
    PROCESSOR_ARCHITECTURE_AMD64: Result := 'x64';
  else
    Result := 'x86';
  end;
end;

function IsArch(a: String): Boolean;
begin
  Result := (NativeArch() = a);
end;

function NeedsAddPath(Dir: String): Boolean;
var OrigPath: String;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', OrigPath) then OrigPath := '';
  Result := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;

procedure RemovePath(Dir: String);
var OrigPath, Sentinel: String; P: Integer;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', OrigPath) then exit;
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
Notes: Inno 5.6.1 uses `PrivilegesRequired=none` (per-user, no elevation) and lacks `IsX64OS`/`IsArm64`, so arch is decided by the external `GetNativeSystemInfo`. `Run` autostarts `dimmitd` both interactively (`skipifsilent`) and silently (`Check: WizardSilent`) so CI's silent install exercises the same path.

- [ ] **Step 2: Build the installer locally with Inno 5.6.1**

Install Inno 5.6.1 (portable is fine) and stage three payloads (reuse a local build for x64/x86; copy x64 as an arm64 stand-in for a local compile-only check):
```powershell
winget install --id JRSoftware.InnoSetup -v 5.6.1 -e --accept-source-agreements --accept-package-agreements
# stage payload\x64, payload\x86, payload\arm64 each with the three exes, then:
$iscc = "${env:ProgramFiles(x86)}\Inno Setup 5\ISCC.exe"
& $iscc "/DAppVersion=0.0.0-dev" "/DX64Dir=$PWD\payload\x64" "/DX86Dir=$PWD\payload\x86" "/DArm64Dir=$PWD\payload\arm64" "/O$PWD" packaging\windows\dimmit.iss
```
Expected: `Successful compile`; `dimmit-0.0.0-dev-windows-setup.exe` produced.

- [ ] **Step 3: Silent install/uninstall smoke (reuse `smoke_test.ps1`)**

Run the existing `packaging/windows/smoke_test.ps1 -Mode post-install` after a `/VERYSILENT` install and `-Mode post-uninstall` after uninstall (same pattern as the prior installer task; install dir is now `{localappdata}\Dimmit`, so pass `-InstallDir "$env:LOCALAPPDATA\Dimmit"`). Expected: `SMOKE OK` both times. Update `smoke_test.ps1`'s default `$InstallDir` to `Join-Path $env:LOCALAPPDATA 'Dimmit'`.

- [ ] **Step 4: Update the installer job in `.github/workflows/release.yml`**

In `release-windows-installer`: (a) download three artifacts (`dimmit-windows-x86_64` → `payload/x64`, `dimmit-windows-i686` → `payload/x86`, `dimmit-windows-arm64` → `payload/arm64`); (b) install Inno 5.6.1 (`choco install innosetup --version=5.6.1 -y` if available, else download the 5.6.1 QuickStart Pack); (c) call `ISCC` from `C:\Program Files (x86)\Inno Setup 5\ISCC.exe` with `/DX64Dir`, `/DX86Dir`, `/DArm64Dir` (absolute `$PWD\...`); (d) the smoke step is unchanged except `-InstallDir "$env:LOCALAPPDATA\Dimmit"` and the bounded-wait helper from the current job.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(packaging): one universal Inno 5.6.1 installer (Vista+, native payload)"
```

---

### Task 6: Docs — Vista floor, named pipe, universal installer

**Files:** Modify `README.md`

- [ ] **Step 1: Update the README**

- In the intro/support line, state the Windows floor as **Windows Vista SP2 and later** (x86, x64, arm64).
- In the `### Windows` install section, note it's a single installer that runs on any supported Windows and installs the matching build; keep the SmartScreen note and the key-capture/fallback paragraph.
- Add a one-line note that `dimmitd` and the clients talk over a per-user **named pipe** (`\\.\pipe\dimmit`), overridable with `DIMMIT_SOCK`.

Exact edit: replace the current `### Windows` body added earlier with the above (keep the download + SmartScreen + key-capture sentences; adjust the "installer" sentence to "runs on any supported Windows (Vista+) and installs the right build for your CPU").

- [ ] **Step 2: Verify + commit**

```bash
grep -n "Vista" README.md
git add README.md
git commit -m "docs(readme): Windows floor is Vista; universal installer + named pipe"
```

---

## Self-Review

**Spec coverage:**
- ipc abstraction, no `#ifdef`s in dimmitd.c/dimmit.c, POSIX + Windows backends, net.h removed → Tasks 1–2 (+ verified by the `grep` in Task 2 Step 4). ✓
- Named pipe transport, per-user DACL, shutdown wake via timeout → Task 3. ✓
- MSVCRT x86/x64, arm64 stays UCRT, i686 added, DLL guard incl. UCRT-slip → Task 4. ✓
- One Inno 5.6.1 universal installer, GetNativeSystemInfo native-arch selection, per-user → Task 5. ✓
- DDC + key capture unchanged → untouched (no task needed). ✓
- Behavior-neutral on macOS/Linux/NetBSD → Task 1 POSIX backend is a verbatim lift; Task 1 Step 10 + existing CI verify. ✓
- README floor/transport/installer → Task 6. ✓
- Vista floor, archs x86_64/i686/arm64 → Tasks 4–5. ✓

**Placeholder scan:** No TBD/TODO; full code for `ipc.h`, both ipc backends, both shutdown backends, the rewired `dimmitd.c`/`dimmit.c`, the Inno script, and exact CI diffs. ✓

**Type/name consistency:** `ipc_listener`/`ipc_client` and the `ipc_listen`/`ipc_wait`/`ipc_accept`/`ipc_read_line`/`ipc_client_authorized`/`ipc_client_close`/`ipc_close`/`ipc_send` set is used identically in `ipc.h`, both backends, and `dimmitd.c`/`dimmit.c`. `shutdown_install(void(*)(void))` matches across header, backends, and `request_stop`. Endpoint default `\\.\pipe\dimmit` matches between CMake and the Windows backend. Artifact names `dimmit-windows-{x86_64,i686,arm64}` match between build matrix and the installer job's downloads. ✓
