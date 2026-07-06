# Multi-display and internal-panel brightness — design

Date: 2026-07-06
Status: approved design; implementation to be planned Phase 1 first.

## Problem

Dimmit today controls exactly one external display. `ddc_open_display()` picks the
first non-built-in display, the daemon holds a single handle and a single
`dimmer_t`, and the socket/HID input drives that one display. Two capabilities are
missing on every platform:

1. **Multiple displays** — a laptop-plus-external, or several externals, should all
   respond to one brightness command.
2. **The internal panel** — where dimmit owns the input path (Linux/Windows), the
   built-in panel should move too, so the user sees *everything* dim together.

This design also fixes a coupling the TODO already calls out: the VCP feature codes
and value packing live in the shared `ddc/abstraction.{c,h}` rather than in the DDC
backend, so the "abstraction" is not actually display-technology-independent and
can't express a non-DDC internal panel.

## Decisions (from the design session)

- **Synchrony = relative, offset-preserving.** Each display steps by the same
  *fraction of its own max* from its current level. Mismatched displays keep their
  offset mid-range and converge only at the 0/100 rails. No startup jump, no "adopt
  which level" problem, degrades gracefully when a display can't be read.
- **Internal panel: seamless, mechanism may differ per platform.** dimmit drives the
  internal panel only where the OS doesn't already do it on the same keypress:
  - macOS observes the brightness keys (does not consume them), so the OS already
    dims the internal; dimmit's 1/16 step keeps the external in lockstep. dimmit
    does **not** drive the internal on macOS.
  - Linux/NetBSD/Windows: dimmit owns the input (remapped keys / socket clients), so
    dimmit drives the internal itself via the platform backend.
- **Hotplug: dynamic re-enumeration.** Plug/unplug is handled live; a newly plugged
  monitor responds without a restart and joins at its own current level.
- **Architecture: generic-brightness backend registry + a display controller**
  (chosen over bolting a separate internal API onto `ddc_*`), delivered in phases.

## Architecture

```
input backends ──┐                       ┌── DDC backend       (external; VCP here)
(HID / socket /  ├─►  display_controller  ├── internal backend  (sysfs / WMI)
 remap) → fraction│    owns the live set, └── mock backend       (N displays, tests)
                  │    hotplug, fan-out
dimmitd = glue ───┘          │
                    one dimmer_t per display (existing dimmer.c, unchanged)
```

`dimmitd` becomes wiring: it converts an input event into a **fraction** and hands
it to the controller. All multi-display concerns live in the controller.

### Layer responsibilities

- **Backends** — each knows one brightness technology. Enumerate the displays it can
  drive, open/get/set/close, speaking *generic brightness only* (current, max,
  value). No VCP in the interface; VCP lives inside the DDC backend.
- **Registry** — the single place platform policy lives: which backends are active on
  this build. This is what encodes "no internal backend on macOS."
- **display_controller** — owns the live set of `(identity, handle, dimmer_t)`,
  applies a relative fraction step to every display, runs the worker that performs
  the (slow) writes, and reconciles the set on hotplug.
- **dimmer.c** — unchanged. Its per-display relative pending-delta state machine is
  exactly the right primitive; we simply keep one per display.

## The generic brightness interface

Replaces the VCP-shaped `ddc/implementation.h`. Names are illustrative.

```c
/* brightness/backend.h */
typedef struct brightness_display_ref    *brightness_display_ref;    /* opaque */
typedef struct brightness_display_handle *brightness_display_handle; /* opaque */

typedef struct {
    brightness_display_ref ref;   /* passed to open() */
    char id[64];                  /* STABLE identity key — see below */
    char label[64];               /* human-readable, for logs */
} brightness_display_info;

typedef struct { int ct; brightness_display_info *info; } brightness_display_list;

typedef struct brightness_backend {
    const char *name;
    int  (*enumerate)(brightness_display_list **out);
    void (*free_list)(brightness_display_list *list);
    int  (*open)(brightness_display_ref ref, brightness_display_handle *out);
    int  (*get)(brightness_display_handle h, int *current, int *max);
    int  (*set)(brightness_display_handle h, int value);
    void (*close)(brightness_display_handle h);
} brightness_backend;

/* registry.c, per platform */
const brightness_backend **brightness_backends(int *count);
```

**Where this lives (refined during planning to minimize churn).** The existing
`ddc/abstraction.c` already holds the VCP value packing shared by all five platform
DDC backends. Rather than duplicate that packing into each backend, the entire
`ddc/` subsystem *becomes the first brightness source provider* behind the generic
vtable: `abstraction.c` grows a `ddc_enumerate_sources()` that returns every
controllable (non-built-in, readable) DDC display wrapped as a generic
`brightness_source`, with VCP staying shared inside `abstraction.c` — i.e. *inside*
the DDC provider, which is exactly where the spec wants it (out of the top/controller
layer). Internal backends (Phase 2) are additional providers implementing the same
vtable. Net effect on the platform DDC backends is minimal: each only gains a stable
`id`/`label` per display (data it already computes — `CGDirectDisplayID`, EDID
mfg/model, `\\.\DISPLAYn`, i2c device path). No per-backend VCP duplication, no
signature churn on `ddc_implementation_*`.

### Controllability rule (the unifier)

The controller keeps a display only if, at enumeration, `open()` **and** an initial
`get()` both succeed. This one rule removes the need to guess `is_builtin` on
Linux/Windows:

- On Windows the internal panel enumerates via dxva2 but fails the DDC `get()`
  (verified: `DISPLAY1` brightness read → FAIL, `DISPLAY2` → cur=23/max=100), so the
  DDC backend's display is dropped and the WMI backend claims the internal instead.
- A non-DDC-capable external is likewise dropped rather than silently accepting
  writes that do nothing.

### Identity keys (for hotplug reconcile)

Each backend supplies a key that is **stable across re-enumeration for a physically
unchanged display**, best-effort per platform:

- macOS DDC: `CGDirectDisplayID` (or EDID serial).
- Linux ddcutil: EDID-derived (mfg + model + serial), which the backend already has.
- Windows dxva2: device name (`\\.\DISPLAYn`) + monitor description (EDID via
  registry if cheap).
- Linux sysfs: backlight device name (e.g. `intel_backlight`).
- Windows WMI: monitor instance name.
- mock: fixed ids.

## Backends & per-platform registry

| Platform     | External backend            | Internal backend                | Notes |
|--------------|-----------------------------|---------------------------------|-------|
| macOS        | DDC (CG / IOAVService), skips `is_builtin` | **none** | OS dims internal on the key; 1/16 step keeps external in lockstep |
| Linux/NetBSD | DDC (ddcutil)               | sysfs backlight (`/sys/class/backlight/*`) | dimmit owns remapped keys, so it drives internal |
| Windows      | dxva2                       | WMI (`WmiMonitorBrightnessMethods`) | internal fails DDC → WMI claims it |
| tests        | multi-display mock (generic)| —                               | replaces today's single-display VCP mock |

The registry is the only platform-policy switch. macOS simply registers no internal
backend; nothing else in the system knows or cares.

## Controller, sync semantics & the dimmer-set

- The controller holds a list of `(id, handle, dimmer_t)`.
- A command is a **fraction** (e.g. `−1/16`). Fan-out: for each display,
  `delta = dimmer_delta_for_fraction(display.max, fraction)` then
  `dimmer_adjust(&dimmer, delta)`. Each display steps by the same fraction of *its
  own* max → perceptual lockstep with offsets preserved.
- **One worker thread** iterates the set and services each *due* display
  (`dimmer_due` → write → `dimmer_commit`), reusing the current lock-released-write
  pattern per display. DDC writes are slow (tens of ms) but the display count is
  small, so sequential servicing is fine.
- **Partial failure isolation:** a failed write on one display is logged and leaves
  its value pending for the next cycle; other displays are unaffected.

### Step unification

Today the socket path applies a raw integer `STEP` (`command.h`, `STEP 5`) while the
HID path applies a fraction (`input_adjust_fn(double fraction)`, macOS `1/16`). Raw
units are meaningless across displays with different `max`. The canonical step unit
becomes the **fraction**: socket `up`/`down` map to a platform-default fraction, the
HID path already provides one, and the controller converts per display. `STEP` is
retired.

## Hotplug (Phase 3)

- Re-enumerate on a per-platform signal: `CGDisplayRegisterReconfigurationCallback`
  (macOS), udev/DRM uevent or a poll fallback (Linux), `WM_DISPLAYCHANGE` (Windows).
- Reconcile by identity key: matched → keep the existing `dimmer_t` (preserves its
  level); new → open, `get()` its current, create a dimmer, start stepping from
  there; vanished → `close()` and drop.
- Consistent with relative sync: a new display joins at its own level, no jump.

Phases 1–2 ship with a simple poll-based re-scan as a placeholder; Phase 3 swaps in
the real per-platform events.

## Error handling

- **No controllable displays:** the daemon still binds and listens; commands are
  logged no-ops (a monitor may appear later via hotplug/re-scan).
- **Unreadable display:** dropped by the controllability rule; logged once.
- **Write failure on a live display:** logged; other displays unaffected; value stays
  pending for retry (existing dimmer semantics).

## Testing

- **Multi-display mock backend** (e.g. 3 displays with different `max` and starting
  levels) replaces the single-display VCP mock. Controller unit tests assert:
  relative-step lockstep, offset preservation, clamping at 0/100, and
  partial-failure isolation.
- **Hotplug tests** drive mock add/drop and assert dimmer reconciliation by identity
  (kept displays retain their level; vanished ones are closed; new ones start from
  their own current).
- **Per-backend hardware checks** stay manual and platform-local (the external
  monitor for DDC; a laptop panel for sysfs/WMI), as today. CI builds/tests the
  generic core and mock on every platform runner.

## Phasing (A-via-C delivery)

- **Phase 1 — generic core + multi-external.** Introduce the generic interface, the
  registry, and the controller; convert the DDC backends and the mock to it; make
  `dimmitd` drive the *set* of external displays in sync; unify the step on
  fractions; poll-based re-scan placeholder. **Ships multi-monitor on all
  platforms.** This spec's `writing-plans` output covers Phase 1 only.
- **Phase 2 — internal backends.** Add Linux sysfs-backlight and Windows WMI backends
  behind the same interface; register them per platform (macOS: none). **Ships
  internal-panel handling.**
- **Phase 3 — real hotplug.** Replace the poll with per-platform display-change
  events.

Each phase is its own plan and PR.

## Risks & open items

- **Rename churn.** `ddc_* → brightness_*`/`display_*` touches every backend and the
  daemon; accepted as the point of the refactor. Keep it mechanical and covered by
  the existing tests.
- **Identity stability on Windows** (dxva2 handles are not stable; device name +
  description is the fallback). If it proves flaky for hotplug, revisit EDID lookup.
- **sysfs backlight permissions** on Linux (writing `/sys/class/backlight/*/brightness`
  needs privilege or a udev rule) — a Phase 2 detail, related to the existing
  "stop needing root?" TODO.
- **Step convention parity.** Picking the socket/Linux/Windows default fraction so it
  feels like the native step (macOS is already 1/16).

## Out of scope

- Per-display *addressing* in the protocol (commands stay global `up`/`down`).
- An absolute "set everyone to N%" / "match" command (relative-only for now).
- Brightness-key capture on Linux/Windows, and macOS *consuming* the keys (the
  observe-only model stands).
- Contrast/other VCP features beyond brightness.
