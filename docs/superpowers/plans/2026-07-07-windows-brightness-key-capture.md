# Windows Brightness-Key Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the no-op Windows input backend with a Raw Input backend that observes the HID Consumer brightness usages and drives the same brightness step the socket clients drive — so the brightness keys dim external displays out of the box on Windows.

**Architecture:** `src/platform/input/windows.c` gains a dedicated thread that creates a message-only window, registers for Raw Input on the HID Consumer Control collection with `RIDEV_INPUTSINK`, and on `WM_INPUT` decodes usages `0x6F`/`0x70` via `HidP_GetUsages`, calling `on_adjust(±1/16)`. This mirrors the macOS `darwin.c` IOKit-HID backend and honors the existing `input.h` contract, so `dimmitd.c` is unchanged.

**Tech Stack:** C11, Win32 Raw Input (`user32`), HID parser (`hid` / `HidP_GetUsages`), winpthreads (already linked), MinGW-w64 UCRT64, CMake/Ninja.

## Global Constraints

- **Observe-only.** Raw Input does not suppress the keys; do not add suppression.
- **Usages:** Consumer page `0x0C`, top-level Consumer Control usage `0x01`; brightness increment `0x6F` → `+1.0/16.0`, decrement `0x70` → `-1.0/16.0`. The `1/16` step lives in the backend (as in `darwin.c`).
- **Leading-edge:** act on the report where the usage is present; the release/empty report yields no usages, so no extra debounce is needed.
- **Contract (`src/platform/input/input.h`):** `int input_start(input_adjust_fn on_adjust)` returns `0` when capturing, `<0` when unsupported/failed (non-fatal — the socket clients remain the input); `void input_stop(void)`. Backend runs its own thread.
- **Background daemon:** the capture window must be a **message-only window** (`HWND_MESSAGE`) and registration must use **`RIDEV_INPUTSINK`** (receive input with no focus).
- **No changes** to `dimmitd.c`, the clients, `test_dimmit`, or the socket path.
- **Link libs:** add `hid` and `user32` to `dimmitd` on Windows (ship with MinGW-w64; no new SDK). Static link must still import only OS DLLs (`hid.dll`, `user32.dll` are OS DLLs — the CI DLL guard stays happy).
- **Build off the WSL/UNC share:** build out-of-source with the build dir on **local NTFS** (an in-place build on `\\wsl.localhost\...` fails at link — cmd.exe rejects a UNC working dir).

---

### Task 1: Raw Input backend + CMake link libs

**Files:**
- Modify (replace whole file): `src/platform/input/windows.c`
- Modify: `CMakeLists.txt` (the `elseif (WIN32)` branch that links `dimmitd`)

**Interfaces:**
- Consumes: `input.h` — `typedef void (*input_adjust_fn)(double fraction);`, `int input_start(input_adjust_fn)`, `void input_stop(void)`.
- Produces: a working Windows `input_start`/`input_stop`. No new public symbols; `dimmitd.c` already calls both.

- [ ] **Step 1: Replace `src/platform/input/windows.c` with the Raw Input backend**

Write exactly this file:

```c
#include "platform/input/input.h"

#include <windows.h>
#include <hidusage.h>
#include <hidpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* Windows surfaces the brightness keys as HID Consumer-page usages (like macOS).
 * We observe them with the Raw Input API on a dedicated thread: a message-only
 * window registered for the Consumer Control collection with RIDEV_INPUTSINK
 * (so a background daemon receives the keys with no focus). Observe-only: the OS
 * doesn't drive external monitors from these keys, so there's nothing to consume.
 * The step convention (1/16 of range) lives here, matching darwin.c.
 *
 * Best-effort: on many laptops the keys are handled by firmware and never reach
 * user space -- registration still succeeds, but no WM_INPUT arrives. The
 * dimmit-up/down clients remain the input in that case. */

#define CONSUMER_PAGE          0x0C
#define CONSUMER_CTRL_USAGE    0x01
#define BRIGHTNESS_UP_USAGE    0x6F   /* Consumer DisplayBrightnessIncrement */
#define BRIGHTNESS_DOWN_USAGE  0x70   /* Consumer DisplayBrightnessDecrement */

static input_adjust_fn g_on_adjust = NULL;
static pthread_t       g_thread;
static int             g_thread_started = 0;
static HWND            g_hwnd = NULL;
static DWORD           g_thread_id = 0;
static HANDLE          g_ready = NULL;   /* signalled once the thread is up/failed */
static volatile LONG   g_start_ok = 0;

/* Decode one WM_INPUT: pull the raw HID report(s), then use the device's
 * preparsed data to list the active Consumer-page usages and act on 0x6F/0x70. */
static void handle_raw_input(HRAWINPUT hri) {
    UINT size = 0;
    if (GetRawInputData(hri, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0)
        return;
    BYTE stackbuf[256];
    BYTE *buf = (size <= sizeof(stackbuf)) ? stackbuf : (BYTE *)malloc(size);
    if (!buf) return;
    if (GetRawInputData(hri, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != size)
        goto out_buf;

    RAWINPUT *ri = (RAWINPUT *)buf;
    if (ri->header.dwType != RIM_TYPEHID) goto out_buf;

    UINT ppsize = 0;
    if (GetRawInputDeviceInfo(ri->header.hDevice, RIDI_PREPARSEDDATA, NULL, &ppsize) != 0 || ppsize == 0)
        goto out_buf;
    PHIDP_PREPARSED_DATA pp = (PHIDP_PREPARSED_DATA)malloc(ppsize);
    if (!pp) goto out_buf;
    if (GetRawInputDeviceInfo(ri->header.hDevice, RIDI_PREPARSEDDATA, pp, &ppsize) == (UINT)-1)
        goto out_pp;

    ULONG maxus = HidP_MaxUsageListLength(HidP_Input, CONSUMER_PAGE, pp);
    if (maxus == 0) goto out_pp;
    USAGE *usages = (USAGE *)malloc(maxus * sizeof(USAGE));
    if (!usages) goto out_pp;

    DWORD count   = ri->data.hid.dwCount;
    DWORD hidsize = ri->data.hid.dwSizeHid;
    for (DWORD r = 0; r < count; r++) {
        BYTE *report = ri->data.hid.bRawData + (size_t)r * hidsize;
        ULONG n = maxus;
        if (HidP_GetUsages(HidP_Input, CONSUMER_PAGE, 0, usages, &n, pp,
                           (PCHAR)report, hidsize) != HIDP_STATUS_SUCCESS)
            continue;
        for (ULONG i = 0; i < n && g_on_adjust; i++) {
            if (usages[i] == BRIGHTNESS_UP_USAGE)        g_on_adjust(+1.0 / 16.0);
            else if (usages[i] == BRIGHTNESS_DOWN_USAGE) g_on_adjust(-1.0 / 16.0);
        }
    }
    free(usages);
out_pp:
    free(pp);
out_buf:
    if (buf != stackbuf) free(buf);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUT) { handle_raw_input((HRAWINPUT)lp); return 0; }
    return DefWindowProc(h, msg, wp, lp);
}

static void *input_thread(void *arg) {
    (void)arg;
    g_thread_id = GetCurrentThreadId();
    HINSTANCE hinst = GetModuleHandle(NULL);

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = "DimmitInputWindow";
    RegisterClassExA(&wc);   /* harmless if already registered */

    g_hwnd = CreateWindowExA(0, wc.lpszClassName, "dimmit-input",
                             0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hinst, NULL);
    if (!g_hwnd) { InterlockedExchange(&g_start_ok, 0); SetEvent(g_ready); return NULL; }

    RAWINPUTDEVICE rid;
    rid.usUsagePage = CONSUMER_PAGE;
    rid.usUsage     = CONSUMER_CTRL_USAGE;
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = g_hwnd;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        DestroyWindow(g_hwnd); g_hwnd = NULL;
        InterlockedExchange(&g_start_ok, 0); SetEvent(g_ready); return NULL;
    }

    InterlockedExchange(&g_start_ok, 1);
    SetEvent(g_ready);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {   /* WM_QUIT -> returns 0 -> exit */
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = NULL; }
    return NULL;
}

int input_start(input_adjust_fn on_adjust) {
    g_on_adjust = on_adjust;
    g_start_ok = 0;
    g_ready = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ready) return -1;
    if (pthread_create(&g_thread, NULL, input_thread, NULL) != 0) {
        CloseHandle(g_ready); g_ready = NULL; return -1;
    }
    g_thread_started = 1;
    WaitForSingleObject(g_ready, INFINITE);
    CloseHandle(g_ready); g_ready = NULL;
    if (!g_start_ok) {
        pthread_join(g_thread, NULL); g_thread_started = 0; g_thread_id = 0;
        fprintf(stderr, "dimmit: brightness-key capture off (Raw Input registration "
                        "failed). The dimmit-up/down clients still work.\n");
        return -1;
    }
    return 0;
}

void input_stop(void) {
    if (!g_thread_started) return;
    if (g_thread_id) PostThreadMessage(g_thread_id, WM_QUIT, 0, 0);
    pthread_join(g_thread, NULL);
    g_thread_started = 0;
    g_thread_id = 0;
}
```

- [ ] **Step 2: Add `hid` and `user32` to the Windows link libraries**

In `CMakeLists.txt`, find the `dimmitd` Windows link block:
```cmake
elseif (WIN32)
    # dxva2: Monitor Configuration API (GetVCPFeatureAndVCPFeatureReply /
    # SetVCPFeature). ws2_32: Winsock, including AF_UNIX support (Win10 1803+).
    target_link_libraries(dimmitd PRIVATE dxva2 ws2_32)
endif()
```
Replace the `target_link_libraries` line and comment with:
```cmake
elseif (WIN32)
    # dxva2: Monitor Configuration API (GetVCPFeatureAndVCPFeatureReply /
    # SetVCPFeature). ws2_32: Winsock, including AF_UNIX support (Win10 1803+).
    # user32 + hid: Raw Input brightness-key capture (platform/input/windows.c).
    target_link_libraries(dimmitd PRIVATE dxva2 ws2_32 user32 hid)
endif()
```

- [ ] **Step 3: Build dimmitd on UCRT64 and verify it compiles + links**

Run (MSYS2 UCRT64; build dir on local NTFS — NOT the UNC share):
```
MSYSTEM=UCRT64 /c/msys64/usr/bin/bash.exe -lc 'cmake -S "//wsl.localhost/Ubuntu/home/schmonz/code/trees/dimmit" -B /c/Users/schmonz/dimmit-bw -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_EXE_LINKER_FLAGS="-static" && cmake --build /c/Users/schmonz/dimmit-bw'
```
Expected: builds to completion; `dimmitd.exe`, `dimmit-up.exe`, `dimmit-down.exe`, `test_dimmit.exe` all link. In particular `CMakeFiles/dimmitd.dir/src/platform/input/windows.c.obj` compiles and `Linking C executable dimmitd.exe` succeeds (this is what proves `hid`/`user32` resolve `HidP_GetUsages`, `RegisterRawInputDevices`, etc.).

- [ ] **Step 4: Verify the daemon starts cleanly with capture active (no crash/hang)**

The backend is thin OS glue (no unit test, like `darwin.c`); the acceptance test is that `dimmitd` starts, registers, and keeps running without crashing or hanging its main loop. Run (PowerShell):
```powershell
$env:DIMMIT_SOCK = 'C:/Users/Public/dimmit-keytest.sock'
$p = Start-Process 'C:\Users\schmonz\dimmit-bw\dimmitd.exe' -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2
"alive after 2s: $(-not $p.HasExited)"
Stop-Process -Id $p.Id -Force
```
Expected: `alive after 2s: True` — the daemon started, `input_start` returned and the socket-accept main loop is running (capture thread up, or gracefully off with the stderr note). If the process exited (`False`), the backend crashed — debug before committing. (Dimming a monitor is the manual, hardware-gated check in Step 6; it is not required to pass this task.)

- [ ] **Step 5: Commit**

```bash
git add src/platform/input/windows.c CMakeLists.txt
git commit -m "feat(input): Windows brightness-key capture via Raw Input"
```

- [ ] **Step 6 (manual, hardware-gated — not a commit gate): confirm keys dim a monitor**

On a keyboard whose brightness keys surface as HID Consumer usages, with an external DDC monitor attached: install/run `dimmitd`, press brightness up/down, and confirm the external monitor brightens/dims by ~1/16 per press. Where the keys are firmware-handled (no `WM_INPUT` arrives), confirm `dimmit-up`/`dimmit-down` still work. Record the outcome; do not block the merge on hardware you don't have.

---

### Task 2: README Windows key-capture note

**Files:**
- Modify: `README.md`

**Interfaces:** none (docs only).

- [ ] **Step 1: Update the README Windows section**

The README's `### Windows` install subsection currently ends with the line about mapping brightness keys to `dimmit-up`/`dimmit-down`. Replace that trailing line:
```markdown
Then map your brightness keys (or any keys) to `dimmit-up` and `dimmit-down` in your keyboard/hotkey tool -- the installer put them on your `PATH`.
```
with:
```markdown
`dimmitd` tries to capture your brightness keys directly (like on macOS). This works where the keys reach Windows as HID "brightness" events -- common on external/USB keyboards. On many laptops the brightness keys are handled by the firmware and never reach `dimmit`; there, map any keys to `dimmit-up` / `dimmit-down` in your keyboard/hotkey tool (the installer put them on your `PATH`).
```

- [ ] **Step 2: Verify the edit**

Run:
```bash
grep -n "tries to capture your brightness keys" README.md
```
Expected: one match, inside the `### Windows` subsection.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs(readme): note Windows brightness-key capture and its fallback"
```

---

## Self-Review

**Spec coverage:**
- Raw Input backend, message-only window, `RIDEV_INPUTSINK`, Consumer Control collection → Task 1 Step 1 (`input_thread`). ✓
- Usages `0x6F`/`0x70` → `±1/16`, leading-edge, step in backend → Task 1 Step 1 (`handle_raw_input`). ✓
- Contract: `0` capturing / `<0` non-fatal with stderr note; own thread; no `dimmitd.c` change → `input_start`/`input_stop`. ✓
- Link `hid` + `user32` → Task 1 Step 2. ✓
- Compile+link + clean-start acceptance (no unit test) → Task 1 Steps 3–4; manual hardware check → Step 6. ✓
- Firmware caveat + client fallback in README → Task 2. ✓
- Non-goals (no suppression, no internal panel, no configurable hotkey, no fine-step, no dimmitd.c/client/socket change) → nothing in the plan adds them. ✓

**Placeholder scan:** No TBD/TODO; the full `windows.c` and the exact CMake/README edits are literal. ✓

**Type/name consistency:** `input_adjust_fn`, `input_start`, `input_stop` match `input.h`; usages `0x6F`/`0x70` and the `1/16` step match `darwin.c`; link targets `hid`/`user32` match the CMake edit; `HidP_GetUsages`/`HidP_MaxUsageListLength`/`RegisterRawInputDevices`/`GetRawInputData`/`GetRawInputDeviceInfo` are used consistently with their Win32 signatures. ✓
