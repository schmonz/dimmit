# Windows brightness-key capture — design

**Date:** 2026-07-07
**Status:** approved design
**Topic:** Replace the no-op Windows input backend with in-process capture of the
brightness keys, so pressing them dims/brightens external displays without the
user having to map keys to the `dimmit-up` / `dimmit-down` clients.

## Goal

On Windows, `dimmitd` should observe the brightness keys and drive the same
`controller_adjust` step the socket clients drive — matching what the macOS
backend already does with IOKit HID. Today `src/platform/input/windows.c` is a
no-op (`input_start` returns `-1`), so the only Windows input is the socket
clients. This closes the gap between "installed and autostarted" (shipped in the
Windows installer) and "the keys just work."

## Approach (settled during brainstorming)

- **Mirror the macOS backend, observe-only.** Watch the HID Consumer usages
  `0x6F` (DisplayBrightnessIncrement) / `0x70` (DisplayBrightnessDecrement) — the
  exact usages `darwin.c` watches — via the Windows **Raw Input API**. Raw Input
  cannot (and will not) suppress the keys; on Windows the OS does not drive
  *external* monitors from these keys, so there is nothing to consume.
- **Rejected alternatives:** a `WH_KEYBOARD_LL` low-level keyboard hook (brightness
  keys usually arrive as HID consumer usages, not standard VK keydowns, so a hook
  often won't see them, and it adds unwanted suppression semantics); a
  user-configurable hotkey (that is "pick a key," not "your brightness keys work").

## Architecture

Replace `src/platform/input/windows.c` with a Raw Input backend honoring the
existing contract in `src/platform/input/input.h`
(`int input_start(input_adjust_fn)`, `void input_stop(void)`), with **no change
to `dimmitd.c`** — it already calls `input_start(adjust_fraction)` at startup and
`input_stop()` at shutdown.

- **`input_start(on_adjust)`** stores the callback and spins a **dedicated thread**
  (via the winpthreads the project already links through `Threads::Threads`).
  The thread:
  1. Creates a **message-only window** (`CreateWindowEx(..., HWND_MESSAGE, ...)`)
     with a small `WndProc`.
  2. Calls `RegisterRawInputDevices` for the **Consumer Control** top-level
     collection (`usUsagePage = 0x0C`, `usUsage = 0x01`) with **`RIDEV_INPUTSINK`**
     (receive input even when no window is focused — required for a background
     daemon) and the message window as the target.
  3. Runs a `GetMessage` / `DispatchMessage` loop until quit.
- **`WM_INPUT` handling** (in the `WndProc`): read the raw input with
  `GetRawInputData`, obtain the device's preparsed data
  (`GetRawInputDeviceInfo(..., RIDI_PREPARSEDDATA, ...)`), and extract the active
  HID usages on page `0x0C` with **`HidP_GetUsages`** (`hid.lib`). If `0x6F` is
  present call `on_adjust(+1.0/16.0)`; if `0x70`, `on_adjust(-1.0/16.0)`. The
  `1/16` step convention lives in the backend, exactly as in `darwin.c`.
- **`input_stop()`** signals the message thread to quit (e.g. `PostMessage` a
  `WM_CLOSE`/custom quit to the window, or `PostThreadMessage(WM_QUIT)`), joins the
  thread, and lets window destruction unregister the Raw Input target.
- **Return contract:** `input_start` returns `0` once the thread is capturing,
  `<0` if window creation or `RegisterRawInputDevices` fails — non-fatal, the
  socket clients remain the input (matches `input.h`). A brief one-line
  stderr/log note on failure, mirroring `darwin.c`'s style, is acceptable.

### Step & repeat behavior

Match macOS exactly for cross-platform consistency: **±1/16 of each display's
range per key event, leading-edge** — act when the usage becomes active in a
report, ignore the release report. Holding the key repeats at whatever rate the
device/OS emits reports; no added debounce (same as `darwin.c`).

## The firmware caveat & fallback

Capture works **only where the brightness keys surface to Windows as HID Consumer
usages** — common on external/USB keyboards and some desktops. On many laptops the
keys are handled by the embedded controller/firmware and never reach user space;
`dimmit` cannot see them. This is not an error path: `RegisterRawInputDevices`
still succeeds, the daemon runs normally, and the **`dimmit-up` / `dimmit-down`
clients remain the fallback** (on `PATH` from the installer; map any keys to them).
We cannot distinguish "firmware ate the key" from "no key pressed" (registration
succeeds; events simply never arrive), so there is no false success signal — the
README sets the expectation. This mirrors the macOS backend's honest posture.

Observe-only also means **no conflict with the internal panel**: if firmware
adjusts the built-in display while `dimmit` adjusts the externals, both happen and
that is the desired split. Internal-panel handling by `dimmit` itself is the
separate Phase 2 internal-backend work, out of scope here.

## Build / CMake

The `WIN32` branch of `CMakeLists.txt` currently links `dxva2 ws2_32` into
`dimmitd`. Add **`hid`** (`HidP_GetUsages`) and **`user32`** (window, message
loop, Raw Input). Both ship with MinGW-w64 — no new SDK. `Threads::Threads`
(winpthreads) already links, so the backend thread needs nothing new. The clients
(`dimmit-up`/`dimmit-down`) and `test_dimmit` are unchanged.

## Testing & definition of done

This backend is thin OS glue that, like `darwin.c`, is not unit-testable without
hardware; the testable logic (fraction → `controller_adjust`) is already covered
by `test_dimmit`.

- **Automated (CI, already in place):** `dimmitd` with the new backend **compiles
  and links** on both Windows arches (the `release-windows` matrix build), and the
  daemon **starts cleanly** with capture active — the installer smoke test already
  launches `dimmitd`, so a crash/hang would surface there.
- **Manual (documented, hardware-gated):** on a keyboard that emits HID brightness
  usages, press brightness up/down and confirm an external DDC monitor
  dims/brightens; where the keys don't surface, confirm the clients still work.
- **README:** add the honest Windows key-capture note and the client fallback.

## Non-goals

- Key **suppression** (Raw Input is observe-only by design).
- **Internal-panel** handling (separate Phase 2 internal backend).
- **Configurable-hotkey** mode.
- **Fine-step** modifier (Shift+Option → 1/64) — a listed macOS follow-on, not this.
- Changes to `dimmitd.c`, the clients, or the socket path.
