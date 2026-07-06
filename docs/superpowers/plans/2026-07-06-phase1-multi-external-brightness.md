# Phase 1 — Generic brightness core + multi-external sync — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make dimmit control *every* connected external display in relative
lockstep instead of a single one, behind a technology-independent brightness
interface that Phase 2's internal-panel backends will plug into unchanged.

**Architecture:** Introduce a generic `brightness_source` (a `{get,set,close}` vtable
+ stable `id`/`label`). The existing `ddc/` subsystem becomes the first *provider*:
`ddc_enumerate_sources()` returns one source per controllable (non-built-in,
readable) DDC display, with VCP packing staying inside `ddc/abstraction.c`. A new
`display_controller` owns a `(brightness_source, dimmer_t)` set and fans a relative
*fraction* step out to all of them. `dimmitd` keeps its single worker thread and
mutex but drives the controller instead of one DDC handle.

**Tech Stack:** C11, CMake, pthreads, ctest with the in-tree `CHECK`-macro harness
(`src/test_dimmit.c`). No new third-party dependencies.

## Global Constraints

- **Language:** C11, `CMAKE_C_EXTENSIONS OFF` (as set in `CMakeLists.txt:144-146`).
- **No new dependencies.** DDC backends stay as-is behind `ddc_implementation_*`.
- **Per-platform backend selection** stays via `dimmit_add_platform_backend()`
  (`CMakeLists.txt:350-362`); do not hand-list platform `.c` files.
- **Tests** use the `CHECK(cond)` macro and are registered by adding a
  `test_<name>()` call in `main()` of `src/test_dimmit.c`. Test build links only
  platform-independent modules + the mock (`CMakeLists.txt:447-450`); it must never
  require real hardware, sockets, threads, or a clock.
- **Relative sync semantics:** each display steps by the same *fraction of its own
  max*; offsets between displays are preserved (never force-equalized).
- **Identity keys are provisional in Phase 1**: synthesized in `abstraction.c` from
  `vendor:product:index`. Per-unit stable keys from the backends are Phase 3 work
  (hotplug). Do not add `id`/`label` fields to the platform backends in this phase.
- **Commit** after every green test cycle.

---

## File structure

- Create `src/brightness.h` — generic `brightness_source` type + registry API.
- Create `src/brightness.c` — `brightness_enumerate()` / `brightness_free()`;
  Phase 1 delegates to the single DDC provider.
- Create `src/display_controller.h` / `src/display_controller.c` — the multi-display
  set, fraction fan-out, write servicing, poll reconcile, test accessors.
- Create `src/platform/ddc/in_memory_mock.h` — test control hooks for the mock.
- Modify `src/platform/ddc/in_memory_mock.c` — N displays + injectable set-failure.
- Modify `src/platform/ddc/abstraction.h` / `abstraction.c` — add
  `ddc_enumerate_sources()`; keep VCP packing here.
- Modify `src/command.h` / `src/command.c` — step becomes a direction (±1/0), not a
  raw `STEP` magnitude.
- Modify `src/dimmitd.c` — drive the controller; convert direction → platform
  fraction.
- Modify `src/test_dimmit.c` — new tests; update the two DDC tests to the new API.
- Modify `CMakeLists.txt` — add `brightness.c` + `display_controller.c` to `dimmitd`
  and `test_dimmit`.

---

## Task 1: Generic brightness interface + DDC source provider

**Files:**
- Create: `src/brightness.h`, `src/brightness.c`
- Create: `src/platform/ddc/in_memory_mock.h`
- Modify: `src/platform/ddc/in_memory_mock.c` (single → N displays + hooks)
- Modify: `src/platform/ddc/abstraction.h` (declare `ddc_enumerate_sources`)
- Modify: `src/platform/ddc/abstraction.c` (implement it; VCP stays here)
- Modify: `CMakeLists.txt` (link `src/brightness.c` into `dimmitd` and `test_dimmit`)
- Test: `src/test_dimmit.c`

**Interfaces:**
- Produces:
  - `typedef struct brightness_source { const brightness_ops *ops; void *ctx; char id[64]; char label[64]; } brightness_source;`
  - `typedef struct { int (*get)(void*,int*,int*); int (*set)(void*,int); void (*close)(void*); } brightness_ops;`
  - `int brightness_enumerate(brightness_source **out, int *count);` (0 ok even if count==0; -1 alloc fail)
  - `void brightness_free(brightness_source *sources, int count);`
  - `int ddc_enumerate_sources(brightness_source **out, int *count);` (same contract; DDC-only)
  - Mock hooks: `void mock_reset(int n, const int *currents, const int *maxes); void mock_set_fail(int index, int fail); int mock_current(int index);`

- [ ] **Step 1: Write `src/brightness.h`**

```c
#ifndef BRIGHTNESS_H
#define BRIGHTNESS_H

/* A generic, technology-independent handle to one controllable display's
 * brightness. Providers (DDC today; sysfs/WMI in Phase 2) each enumerate zero or
 * more of these; the controller treats them uniformly. Brightness is a plain
 * integer in [0, max] -- no VCP here (that lives inside the DDC provider). */

typedef struct {
    int  (*get)(void *ctx, int *current, int *max); /* 0 on success */
    int  (*set)(void *ctx, int value);              /* 0 on success */
    void (*close)(void *ctx);                        /* release ctx */
} brightness_ops;

typedef struct {
    const brightness_ops *ops;
    void *ctx;          /* provider-private per-display state */
    char  id[64];       /* stable-ish key for reconcile (provisional in Phase 1) */
    char  label[64];    /* human-readable, for logs */
} brightness_source;

/* Enumerate every controllable display across all registered providers.
 * Allocates *out (array of *count sources); free with brightness_free.
 * Returns 0 on success (count may be 0), -1 on allocation failure. */
int  brightness_enumerate(brightness_source **out, int *count);

/* Close every source (calls ops->close on each ctx) and free the array. */
void brightness_free(brightness_source *sources, int count);

#endif /* BRIGHTNESS_H */
```

- [ ] **Step 2: Write `src/brightness.c`** (Phase 1: one provider — DDC)

```c
#include "brightness.h"
#include "platform/ddc/abstraction.h"
#include <stdlib.h>

int brightness_enumerate(brightness_source **out, int *count) {
    /* Phase 1 has a single provider. Phase 2 concatenates internal providers. */
    return ddc_enumerate_sources(out, count);
}

void brightness_free(brightness_source *sources, int count) {
    if (!sources) return;
    for (int i = 0; i < count; i++) {
        if (sources[i].ops && sources[i].ops->close) sources[i].ops->close(sources[i].ctx);
    }
    free(sources);
}
```

- [ ] **Step 3: Write `src/platform/ddc/in_memory_mock.h`** (test control hooks)

```c
#ifndef DDC_IN_MEMORY_MOCK_H
#define DDC_IN_MEMORY_MOCK_H

/* Test-only controls for the in-memory mock DDC backend. Let a test stand up an
 * arbitrary set of simulated displays and inject a write failure on one of them. */

#define MOCK_MAX_DISPLAYS 8

/* Configure `n` displays with the given per-display current/max. Clears any
 * previously injected failures. Safe to call repeatedly between tests. */
void mock_reset(int n, const int *currents, const int *maxes);

/* Make ddc_implementation_set_* fail (return DDC_ERROR) for display `index`. */
void mock_set_fail(int index, int fail);

/* Read the simulated current brightness of display `index` (-1 if out of range). */
int  mock_current(int index);

#endif /* DDC_IN_MEMORY_MOCK_H */
```

- [ ] **Step 4: Rewrite `src/platform/ddc/in_memory_mock.c`** for N displays

```c
/* In-memory mock DDC backend for unit tests: a configurable set of simulated
 * external displays. Implements platform/ddc/implementation.h with no hardware. */
#include "platform/ddc/implementation.h"
#include "platform/ddc/abstraction.h"
#include "platform/ddc/in_memory_mock.h"
#include <stdlib.h>

struct DDC_Display_Ref_s    { int index; };
struct DDC_Display_Handle_s { int index; };

static struct DDC_Display_Ref_s g_refs[MOCK_MAX_DISPLAYS];
static struct DDC_Display_Handle_s g_handles[MOCK_MAX_DISPLAYS];
static int g_current[MOCK_MAX_DISPLAYS];
static int g_max[MOCK_MAX_DISPLAYS];
static int g_fail[MOCK_MAX_DISPLAYS];
static int g_count = 1;   /* default: one display, matches historic behavior */

/* Default single display (current=50,max=100) so any test that doesn't call
 * mock_reset() sees the original mock. */
static void ensure_default(void) {
    static int inited = 0;
    if (inited) return;
    inited = 1;
    g_current[0] = 50; g_max[0] = 100; g_fail[0] = 0; g_count = 1;
}

void mock_reset(int n, const int *currents, const int *maxes) {
    if (n < 0) n = 0;
    if (n > MOCK_MAX_DISPLAYS) n = MOCK_MAX_DISPLAYS;
    g_count = n;
    for (int i = 0; i < n; i++) {
        g_current[i] = currents[i];
        g_max[i] = maxes[i];
        g_fail[i] = 0;
    }
}

void mock_set_fail(int index, int fail) {
    if (index >= 0 && index < MOCK_MAX_DISPLAYS) g_fail[index] = fail;
}

int mock_current(int index) {
    if (index < 0 || index >= g_count) return -1;
    return g_current[index];
}

DDC_Status ddc_implementation_get_display_info_list(int flags, DDC_Display_Info_List **list_out) {
    (void)flags;
    ensure_default();
    if (!list_out || g_count == 0) return DDC_ERROR;
    DDC_Display_Info_List *list = (DDC_Display_Info_List*)malloc(sizeof(*list));
    if (!list) return DDC_ERROR;
    list->ct = g_count;
    list->info = (DDC_Display_Info*)calloc((size_t)g_count, sizeof(DDC_Display_Info));
    if (!list->info) { free(list); return DDC_ERROR; }
    for (int i = 0; i < g_count; i++) {
        g_refs[i].index = i;
        list->info[i].dref = (DDC_Display_Ref)&g_refs[i];
        list->info[i].vendor_id = 0x1234;
        list->info[i].product_id = (uint32_t)(0x5678 + i);
        list->info[i].is_builtin = 0;
    }
    *list_out = list;
    return DDC_OK;
}

void ddc_implementation_free_display_info_list(DDC_Display_Info_List *list) {
    if (!list) return;
    free(list->info);
    free(list);
}

DDC_Status ddc_implementation_open_display(DDC_Display_Ref dref, int flags, DDC_Display_Handle *handle_out) {
    (void)flags;
    if (!dref || !handle_out) return DDC_ERROR;
    struct DDC_Display_Ref_s *r = (struct DDC_Display_Ref_s*)dref;
    g_handles[r->index].index = r->index;
    *handle_out = (DDC_Display_Handle)&g_handles[r->index];
    return DDC_OK;
}

DDC_Status ddc_implementation_close_display(DDC_Display_Handle handle) {
    (void)handle;   /* handles are static; nothing to free */
    return DDC_OK;
}

DDC_Status ddc_implementation_get_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, DDC_Non_Table_Vcp_Value *value_out) {
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s*)handle;
    if (!h || !value_out) return DDC_ERROR;
    if (feature_code != VCP_BRIGHTNESS) return DDC_ERROR;
    int i = h->index;
    value_out->mh = (uint8_t)((g_max[i] >> 8) & 0xFF);
    value_out->ml = (uint8_t)(g_max[i] & 0xFF);
    value_out->sh = (uint8_t)((g_current[i] >> 8) & 0xFF);
    value_out->sl = (uint8_t)(g_current[i] & 0xFF);
    return DDC_OK;
}

DDC_Status ddc_implementation_set_non_table_vcp_value(DDC_Display_Handle handle, uint8_t feature_code, uint8_t hi_byte, uint8_t lo_byte) {
    struct DDC_Display_Handle_s *h = (struct DDC_Display_Handle_s*)handle;
    if (!h) return DDC_ERROR;
    if (feature_code != VCP_BRIGHTNESS) return DDC_ERROR;
    if (g_fail[h->index]) return DDC_ERROR;
    g_current[h->index] = (hi_byte << 8) | lo_byte;
    return DDC_OK;
}
```

- [ ] **Step 5: Declare `ddc_enumerate_sources` in `src/platform/ddc/abstraction.h`**

Add after the existing `ddc_close_display` declaration (keep the existing
single-display API for now; it is removed in Task 6):

```c
#include "brightness.h"   /* brightness_source */

/* Enumerate every controllable DDC display (non-built-in and answering an initial
 * brightness read) as generic brightness sources. Contract matches
 * brightness_enumerate(). VCP packing lives in abstraction.c. */
int ddc_enumerate_sources(brightness_source **out, int *count);
```

- [ ] **Step 6: Implement `ddc_enumerate_sources` in `src/platform/ddc/abstraction.c`**

Add at the end of the file. This wraps each controllable display; VCP packing (the
`<<8` / `&0xFF`) that today lives in `ddc_get_brightness`/`ddc_set_brightness` is
reused here via the ops:

```c
#include <stdio.h>
#include <string.h>

/* ctx for a DDC-backed brightness source: the opened implementation handle. */
static int ddc_src_get(void *ctx, int *current, int *max) {
    DDC_Display_Handle h = (DDC_Display_Handle)ctx;
    DDC_Non_Table_Vcp_Value v;
    if (ddc_implementation_get_non_table_vcp_value(h, VCP_BRIGHTNESS, &v) != DDC_OK) return -1;
    *current = (v.sh << 8) | v.sl;
    *max     = (v.mh << 8) | v.ml;
    return 0;
}
static int ddc_src_set(void *ctx, int value) {
    DDC_Display_Handle h = (DDC_Display_Handle)ctx;
    uint8_t hi = (uint8_t)((value >> 8) & 0xFF), lo = (uint8_t)(value & 0xFF);
    return ddc_implementation_set_non_table_vcp_value(h, VCP_BRIGHTNESS, hi, lo) == DDC_OK ? 0 : -1;
}
static void ddc_src_close(void *ctx) {
    ddc_implementation_close_display((DDC_Display_Handle)ctx);
}
static const brightness_ops DDC_OPS = { ddc_src_get, ddc_src_set, ddc_src_close };

int ddc_enumerate_sources(brightness_source **out, int *count) {
    *out = NULL; *count = 0;
    DDC_Display_Info_List *dlist = NULL;
    if (ddc_implementation_get_display_info_list(0, &dlist) != DDC_OK || !dlist) return 0;

    brightness_source *arr = (brightness_source*)calloc((size_t)dlist->ct, sizeof(*arr));
    if (!arr) { ddc_implementation_free_display_info_list(dlist); return -1; }

    int n = 0;
    for (int i = 0; i < dlist->ct; i++) {
        if (dlist->info[i].is_builtin) continue;   /* OS owns the internal panel */
        DDC_Display_Handle h = NULL;
        if (ddc_implementation_open_display(dlist->info[i].dref, 0, &h) != DDC_OK) continue;

        /* Controllability rule: keep only displays that answer an initial read. */
        DDC_Non_Table_Vcp_Value probe;
        if (ddc_implementation_get_non_table_vcp_value(h, VCP_BRIGHTNESS, &probe) != DDC_OK) {
            ddc_implementation_close_display(h);
            continue;
        }
        arr[n].ops = &DDC_OPS;
        arr[n].ctx = h;
        snprintf(arr[n].id, sizeof(arr[n].id), "ddc:%04x:%04x:%d",
                 dlist->info[i].vendor_id, dlist->info[i].product_id, i);
        snprintf(arr[n].label, sizeof(arr[n].label), "DDC display %d (%04x:%04x)",
                 i, dlist->info[i].vendor_id, dlist->info[i].product_id);
        n++;
    }
    ddc_implementation_free_display_info_list(dlist);

    if (n == 0) { free(arr); return 0; }
    *out = arr; *count = n;
    return 0;
}
```

- [ ] **Step 7: Wire `brightness.c` into the build**

In `CMakeLists.txt`, add `src/brightness.c` to the `dimmitd` sources
(`CMakeLists.txt:310-315`) and to the `test_dimmit` sources
(`CMakeLists.txt:447-450`). Also add `src/platform/ddc/in_memory_mock.c` is already
in the test target; leave it. Result:

```cmake
add_executable(dimmitd
    src/dimmitd.c
    src/dimmer.c
    src/command.c
    src/brightness.c
    src/platform/ddc/abstraction.c
)
```
```cmake
add_executable(test_dimmit
    src/test_dimmit.c src/dimmer.c src/command.c
    src/brightness.c
    src/platform/ddc/abstraction.c src/platform/ddc/in_memory_mock.c
    src/platform/access-control/mock.c)
```

- [ ] **Step 8: Write the failing test** — add to `src/test_dimmit.c`

Add `#include "brightness.h"` and `#include "platform/ddc/in_memory_mock.h"` near
the top, then:

```c
static void test_brightness_enumerate_multi(void) {
    int currents[] = {50, 20, 80};
    int maxes[]    = {100, 100, 255};
    mock_reset(3, currents, maxes);

    brightness_source *s = NULL; int n = -1;
    CHECK(brightness_enumerate(&s, &n) == 0);
    CHECK(n == 3);

    int cur = -1, max = -1;
    CHECK(s[1].ops->get(s[1].ctx, &cur, &max) == 0);
    CHECK(cur == 20); CHECK(max == 100);

    CHECK(s[2].ops->set(s[2].ctx, 100) == 0);
    CHECK(mock_current(2) == 100);

    /* ids are distinct (needed later for reconcile). */
    CHECK(strcmp(s[0].id, s[1].id) != 0);

    brightness_free(s, n);
    mock_reset(1, (int[]){50}, (int[]){100});  /* restore default for other tests */
}
```

Register it: add `test_brightness_enumerate_multi();` in `main()`.

- [ ] **Step 9: Run tests, expect FAIL then PASS**

Run: `cmake -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure`
Expected first: FAIL to compile/link (`brightness.h` new) then, once implemented, `dimmit_unit ... Passed`.

- [ ] **Step 10: Commit**

```bash
git add src/brightness.h src/brightness.c src/platform/ddc/in_memory_mock.h \
        src/platform/ddc/in_memory_mock.c src/platform/ddc/abstraction.h \
        src/platform/ddc/abstraction.c src/test_dimmit.c CMakeLists.txt
git commit -m "feat(brightness): generic source interface + multi-display DDC provider"
```

---

## Task 2: display_controller — the dimmer-set and fraction fan-out

**Files:**
- Create: `src/display_controller.h`, `src/display_controller.c`
- Modify: `CMakeLists.txt` (add `src/display_controller.c` to `dimmitd` and `test_dimmit`)
- Test: `src/test_dimmit.c`

**Interfaces:**
- Consumes: `brightness_enumerate/free`, `brightness_source` (Task 1); `dimmer_t`,
  `dimmer_init/adjust/due/commit/settled`, `dimmer_delta_for_fraction` (`dimmer.h`).
- Produces:
  - `display_controller *controller_open(void);`
  - `int  controller_count(const display_controller *c);`
  - `void controller_adjust(display_controller *c, double fraction);`
  - `int  controller_service(display_controller *c);` (applies all due writes; returns count applied)
  - `int  controller_current(const display_controller *c, int i);` (test accessor; -1 out of range)
  - `void controller_close(display_controller *c);`

- [ ] **Step 1: Write `src/display_controller.h`**

```c
#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include "brightness.h"

/* Owns the live set of controllable displays, one dimmer per display, and applies
 * a relative fraction step to all of them (each by that fraction of its own max,
 * so offsets between displays are preserved). Thread-agnostic: dimmitd owns the
 * mutex/worker and calls controller_service() to perform the (slow) writes. */

typedef struct display_controller display_controller;

/* Enumerate + open every controllable display and read its current brightness.
 * Returns NULL only on allocation failure; a valid controller with 0 displays is
 * fine (a monitor may appear later). */
display_controller *controller_open(void);

int  controller_count(const display_controller *c);

/* Fan a relative step out to every display: for display d,
 * dimmer_adjust(dimmer_delta_for_fraction(d.max, fraction)). */
void controller_adjust(display_controller *c, double fraction);

/* Apply every due write (dimmer_due -> source set -> dimmer_commit, or
 * dimmer_settled on failure). Returns the number of displays written. */
int  controller_service(display_controller *c);

/* Test accessor: last-applied brightness of display i, or -1 if out of range. */
int  controller_current(const display_controller *c, int i);

void controller_close(display_controller *c);

#endif /* DISPLAY_CONTROLLER_H */
```

- [ ] **Step 2: Write `src/display_controller.c`**

```c
#include "display_controller.h"
#include "dimmer.h"
#include <stdlib.h>

typedef struct { brightness_source src; dimmer_t dim; } managed_display;

struct display_controller {
    brightness_source *sources;   /* owned array from brightness_enumerate */
    int count;
    managed_display *displays;     /* parallel to sources */
};

display_controller *controller_open(void) {
    display_controller *c = (display_controller*)calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (brightness_enumerate(&c->sources, &c->count) != 0) { free(c); return NULL; }
    if (c->count > 0) {
        c->displays = (managed_display*)calloc((size_t)c->count, sizeof(managed_display));
        if (!c->displays) { brightness_free(c->sources, c->count); free(c); return NULL; }
        for (int i = 0; i < c->count; i++) {
            c->displays[i].src = c->sources[i];
            int cur = 0, max = 100;
            if (c->sources[i].ops->get(c->sources[i].ctx, &cur, &max) != 0) { cur = 0; max = 100; }
            dimmer_init(&c->displays[i].dim, cur, max);
        }
    }
    return c;
}

int controller_count(const display_controller *c) { return c ? c->count : 0; }

void controller_adjust(display_controller *c, double fraction) {
    if (!c) return;
    for (int i = 0; i < c->count; i++) {
        int delta = dimmer_delta_for_fraction(dimmer_max(&c->displays[i].dim), fraction);
        dimmer_adjust(&c->displays[i].dim, delta);
    }
}

int controller_service(display_controller *c) {
    if (!c) return 0;
    int applied = 0;
    for (int i = 0; i < c->count; i++) {
        int target = -1;
        if (!dimmer_due(&c->displays[i].dim, &target)) continue;
        if (c->displays[i].src.ops->set(c->displays[i].src.ctx, target) == 0) {
            dimmer_commit(&c->displays[i].dim, target);
            applied++;
        } else {
            dimmer_settled(&c->displays[i].dim);  /* isolate the failure, drop its batch */
        }
    }
    return applied;
}

int controller_current(const display_controller *c, int i) {
    if (!c || i < 0 || i >= c->count) return -1;
    return c->displays[i].dim.current;
}

void controller_close(display_controller *c) {
    if (!c) return;
    brightness_free(c->sources, c->count);  /* calls ops->close on each */
    free(c->displays);
    free(c);
}
```

- [ ] **Step 3: Add `src/display_controller.c` to the build**

In `CMakeLists.txt`, append `src/display_controller.c` to both the `dimmitd` and
`test_dimmit` `add_executable` source lists (alongside `src/brightness.c`).

- [ ] **Step 4: Write the failing tests** — add to `src/test_dimmit.c`

```c
static void test_controller_lockstep_preserves_offset(void) {
    /* Two displays, different starting levels + different max. */
    mock_reset(2, (int[]){50, 20}, (int[]){100, 100});
    display_controller *c = controller_open();
    CHECK(c != NULL);
    CHECK(controller_count(c) == 2);

    controller_adjust(c, -1.0/16.0);      /* -6 on each (fraction of own max=100) */
    CHECK(controller_service(c) == 2);
    CHECK(controller_current(c, 0) == 44);
    CHECK(controller_current(c, 1) == 14);  /* 30-point offset preserved */

    controller_close(c);
    mock_reset(1, (int[]){50}, (int[]){100});
}

static void test_controller_clamps_at_rails(void) {
    mock_reset(2, (int[]){3, 98}, (int[]){100, 100});
    display_controller *c = controller_open();
    controller_adjust(c, -1.0/16.0);      /* both want -6 */
    controller_service(c);
    CHECK(controller_current(c, 0) == 0);   /* clamped at floor */
    CHECK(controller_current(c, 1) == 92);
    controller_close(c);
    mock_reset(1, (int[]){50}, (int[]){100});
}

static void test_controller_partial_failure_isolated(void) {
    mock_reset(2, (int[]){50, 50}, (int[]){100, 100});
    mock_set_fail(0, 1);                    /* display 0 writes fail */
    display_controller *c = controller_open();
    controller_adjust(c, -1.0/16.0);
    CHECK(controller_service(c) == 1);      /* only display 1 written */
    CHECK(mock_current(0) == 50);           /* display 0 unchanged */
    CHECK(mock_current(1) == 44);           /* display 1 moved */
    controller_close(c);
    mock_set_fail(0, 0);
    mock_reset(1, (int[]){50}, (int[]){100});
}
```

Register all three in `main()`.

- [ ] **Step 5: Run tests, expect FAIL then PASS**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: after implementing Steps 1-3, `dimmit_unit ... Passed`.

- [ ] **Step 6: Commit**

```bash
git add src/display_controller.h src/display_controller.c src/test_dimmit.c CMakeLists.txt
git commit -m "feat(controller): multi-display relative fan-out with failure isolation"
```

---

## Task 3: Poll-based reconcile (hotplug placeholder)

**Files:**
- Modify: `src/display_controller.h` / `.c` (add `controller_reconcile`)
- Test: `src/test_dimmit.c`

**Interfaces:**
- Produces: `void controller_reconcile(display_controller *c);` — re-enumerate; keep
  a display whose `id` still matches (preserving its dimmer), add new ones
  (initialized from their own current), drop vanished ones.

- [ ] **Step 1: Declare `controller_reconcile` in the header** (after `controller_service`)

```c
/* Re-enumerate the display set. Displays whose id still matches keep their dimmer
 * (and level); new displays are opened and initialized from their own current;
 * vanished displays are closed and dropped. Phase 1 calls this on a timer; Phase 3
 * replaces the trigger with per-platform display-change events. */
void controller_reconcile(display_controller *c);
```

- [ ] **Step 2: Write the failing test** — add to `src/test_dimmit.c`

```c
static void test_controller_reconcile_add_and_keep(void) {
    mock_reset(1, (int[]){50}, (int[]){100});
    display_controller *c = controller_open();
    controller_adjust(c, -1.0/16.0); controller_service(c);
    CHECK(controller_current(c, 0) == 44);   /* display 0 moved to 44 */

    /* A second monitor is plugged in. */
    mock_reset(2, (int[]){44, 70}, (int[]){100, 100});
    controller_reconcile(c);
    CHECK(controller_count(c) == 2);
    CHECK(controller_current(c, 0) == 44);   /* kept its level */
    CHECK(controller_current(c, 1) == 70);   /* new one initialized from its current */

    /* Unplug the first. */
    mock_reset(1, (int[]){70}, (int[]){100});
    controller_reconcile(c);
    CHECK(controller_count(c) == 1);
    controller_close(c);
    mock_reset(1, (int[]){50}, (int[]){100});
}
```

Note: the mock synthesizes `id` as `ddc:1234:<5678+index>:<index>`. After
`mock_reset(2,...)` display 0 keeps index 0 (same id) so it matches; display 1 is
new. This is the provisional-id behavior; Phase 3 hardens it.

Register `test_controller_reconcile_add_and_keep();` in `main()`.

- [ ] **Step 3: Run test, expect FAIL**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: FAIL (`controller_reconcile` undefined).

- [ ] **Step 4: Implement `controller_reconcile` in `src/display_controller.c`**

```c
void controller_reconcile(display_controller *c) {
    if (!c) return;
    brightness_source *fresh = NULL; int fresh_n = 0;
    if (brightness_enumerate(&fresh, &fresh_n) != 0) return;   /* keep current set on failure */

    managed_display *next = fresh_n > 0
        ? (managed_display*)calloc((size_t)fresh_n, sizeof(managed_display)) : NULL;
    if (fresh_n > 0 && !next) { brightness_free(fresh, fresh_n); return; }

    for (int i = 0; i < fresh_n; i++) {
        int matched = -1;
        for (int j = 0; j < c->count; j++) {
            if (strcmp(fresh[i].id, c->sources[j].id) == 0) { matched = j; break; }
        }
        next[i].src = fresh[i];
        if (matched >= 0) {
            next[i].dim = c->displays[matched].dim;   /* preserve level + pending */
        } else {
            int cur = 0, max = 100;
            if (fresh[i].ops->get(fresh[i].ctx, &cur, &max) != 0) { cur = 0; max = 100; }
            dimmer_init(&next[i].dim, cur, max);
        }
    }

    /* Close only the sources that did NOT survive into the new set. */
    for (int j = 0; j < c->count; j++) {
        int survived = 0;
        for (int i = 0; i < fresh_n; i++) if (strcmp(fresh[i].id, c->sources[j].id) == 0) { survived = 1; break; }
        if (!survived && c->sources[j].ops && c->sources[j].ops->close)
            c->sources[j].ops->close(c->sources[j].ctx);
    }
    free(c->sources);     /* free old array shell; surviving ctx now owned via `fresh` */
    free(c->displays);
    c->sources = fresh;   /* NOTE: fresh[i].ctx for a matched display is a *new* open;
                             the old matched ctx was closed above. */
    c->count = fresh_n;
    c->displays = next;
}
```

Add `#include <string.h>` at the top of `display_controller.c` if not present.

> Implementer note: a matched display is re-opened by `brightness_enumerate` (new
> ctx) and its old ctx is closed above — correct but not free. Phase 3, which adds
> real per-unit ids and event-driven triggers, can optimize to reuse open handles.
> For the poll cadence in Phase 1 this is fine.

- [ ] **Step 5: Run test, expect PASS**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: `dimmit_unit ... Passed`.

- [ ] **Step 6: Commit**

```bash
git add src/display_controller.h src/display_controller.c src/test_dimmit.c
git commit -m "feat(controller): poll-based reconcile preserving matched displays"
```

---

## Task 4: Step becomes a direction (fraction unification)

**Files:**
- Modify: `src/command.h`, `src/command.c`
- Modify: `src/test_dimmit.c` (update `test_parse_command`, `test_dimmer_*` STEP uses)
- Test: `src/test_dimmit.c`

**Interfaces:**
- Produces: `int parse_command(const char *cmd);` now returns **direction**: `+1`
  (up), `-1` (down), `0` (unrecognized). `read_command` returns the same. `STEP` is
  removed. dimmitd (Task 5) converts direction → a platform fraction.

- [ ] **Step 1: Update `src/command.h`**

Remove `#define STEP 5`. Update the doc comments so "delta" reads "direction (+1
up, -1 down, 0 unrecognized)". Signatures are unchanged.

- [ ] **Step 2: Update `src/command.c`**

```c
int parse_command(const char *cmd) {
    if (strcmp(cmd, "up") == 0) return +1;
    if (strcmp(cmd, "down") == 0) return -1;
    return 0;
}
```
(`read_command` is unchanged — it already returns whatever `parse_command` returns.)

- [ ] **Step 3: Update the tests that referenced `STEP`**

In `src/test_dimmit.c`:
- `test_parse_command`: `CHECK(parse_command("up") == 1); CHECK(parse_command("down") == -1);`
  (bogus/empty stay `== 0`).
- `test_dimmer_accumulates`, `test_dimmer_clamps`, `test_dimmer_due`,
  `test_dimmer_commit_and_settled`, `test_dimmer_coalesces_during_write`: these
  exercise `dimmer_adjust` with a raw magnitude. Replace `STEP` with a local
  `const int STEP = 5;` at the top of each such function so the dimmer math tests
  keep their exact expected numbers (the dimmer still takes an integer delta; only
  the *command* layer stopped using a magnitude).
- `test_command_loop_end_to_end`: it currently asserts `delta == -STEP` and computes
  `target == 45`. Update to the new direction API — this test is rewritten in Task 5
  when dimmitd's conversion lands; for now change `CHECK(delta == -STEP)` to
  `CHECK(delta == -1)` and delete the `dimmer_adjust(&d, delta)`/`target==45` block's
  dependence on STEP by using `dimmer_adjust(&d, delta * 5)` with `target == 45`.

- [ ] **Step 4: Run tests, expect PASS**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: `dimmit_unit ... Passed`.

- [ ] **Step 5: Commit**

```bash
git add src/command.h src/command.c src/test_dimmit.c
git commit -m "refactor(command): up/down parse to a direction, retire raw STEP"
```

---

## Task 5: Rewire dimmitd onto the controller

**Files:**
- Modify: `src/dimmitd.c`
- Modify: `src/test_dimmit.c` (rewrite `test_command_loop_end_to_end` + `test_ddc_roundtrip`
  onto the controller/brightness API)
- Test: `src/test_dimmit.c` + manual smoke

**Interfaces:**
- Consumes: `controller_open/adjust/service/reconcile/close` (Tasks 2-3),
  `parse_command`/`read_command` direction (Task 4).

- [ ] **Step 1: Replace the single-display state in `src/dimmitd.c`**

Currently `dimmitd.c` holds `ddc_handle_t *ddc;` and `dimmer_t dimmer;` and
`init_monitor()`/`do_set_brightness()`/`adjust_brightness()` operate on them
(`src/dimmitd.c:64-143`). Replace with a `display_controller *ctrl;`:

- `#include "display_controller.h"` (drop `#include "platform/ddc/abstraction.h"`
  and the single `dimmer` global).
- `init_monitor()` becomes:

```c
static int init_monitor(void) {
    ctrl = controller_open();
    if (!ctrl) { fprintf(stderr, "Failed to initialize displays\n"); return -1; }
    printf("Controlling %d display(s)\n", controller_count(ctrl));
    return 0;   /* 0 displays is fine; hotplug may add some */
}
```

- The command path: an input event (direction `dir`, an int +1/-1) becomes a
  platform fraction. Keep the existing platform step convention — define once near
  the top of `dimmitd.c`:

```c
/* Socket up/down step. macOS HID already supplies its own 1/16 fraction via
 * input_adjust_fn; this is the fraction for the socket clients (and Linux/Windows,
 * which have no native brightness-key step of their own). */
#define DIMMIT_SOCKET_FRACTION (1.0/16.0)
```

- The worker: where the old `adjust_brightness(delta)` + `do_set_brightness()` ran,
  call `controller_adjust(ctrl, fraction)` then `controller_service(ctrl)` under the
  existing mutex, preserving the lock-released-write structure (service performs the
  slow writes; keep it inside the worker exactly where `do_set_brightness` was).
- The HID `input_adjust_fn(double fraction)` path calls
  `controller_adjust(ctrl, fraction)` and signals the worker (same as today, but via
  the controller).
- On shutdown, `controller_close(ctrl)`.
- Add a periodic `controller_reconcile(ctrl)` call in the worker/accept loop (e.g.
  every N seconds via the existing loop's timeout) so hotplugged displays appear.
  Keep it simple: reconcile on each socket accept timeout tick.

> Keep the mutex discipline identical to the current file: state mutated only under
> `lock`; the slow writes happen in `controller_service`, which you may call with the
> lock held (writes are serialized anyway) or released — match the current
> `do_set_brightness` placement to avoid changing the concurrency model.

- [ ] **Step 2: Rewrite the two DDC tests in `src/test_dimmit.c`**

Replace `test_ddc_roundtrip` and the DDC half of `test_command_loop_end_to_end`
(the `ddc_open_display`/`ddc_get_brightness`/`ddc_set_brightness` block) with the
controller API:

```c
static void test_controller_roundtrip(void) {
    mock_reset(1, (int[]){50}, (int[]){100});
    display_controller *c = controller_open();
    CHECK(controller_count(c) == 1);
    CHECK(controller_current(c, 0) == 50);
    controller_adjust(c, -1.0/16.0);
    controller_service(c);
    CHECK(controller_current(c, 0) == 44);
    controller_close(c);
}
```

For `test_command_loop_end_to_end`, keep the socketpair + `read_command` half
(asserting `read_command` returns `-1` for "down\n"), then drive that direction
through a controller:

```c
    int dir = read_command(sv[1]);
    CHECK(dir == -1);
    mock_reset(1, (int[]){50}, (int[]){100});
    display_controller *c = controller_open();
    controller_adjust(c, dir * (1.0/16.0));
    controller_service(c);
    CHECK(controller_current(c, 0) == 44);
    controller_close(c);
```

Update `main()` to call `test_controller_roundtrip()` and drop `test_ddc_roundtrip()`.

- [ ] **Step 3: Run tests, expect PASS**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: `dimmit_unit ... Passed` (and, on Windows, the `SKIP` line for the
socketpair half remains).

- [ ] **Step 4: Manual smoke (platform-local, not CI)**

On a machine with a DDC/CI external monitor, build and run `dimmitd`, then
`dimmit-down`/`dimmit-up` and confirm the external actually dims. On Windows, use
the `docs/windows-build.md` smoke recipe. With two externals attached, confirm both
move together.

- [ ] **Step 5: Commit**

```bash
git add src/dimmitd.c src/test_dimmit.c
git commit -m "feat(dimmitd): drive all displays via the controller; fraction steps"
```

---

## Task 6: Remove the dead single-display API and update docs

**Files:**
- Modify: `src/platform/ddc/abstraction.h` / `abstraction.c` (remove
  `ddc_open_display`/`ddc_get_brightness`/`ddc_set_brightness`/`ddc_close_display`
  if no longer referenced)
- Modify: `README.md` (caveat: single external → all externals)
- Test: full `ctest`

- [ ] **Step 1: Confirm the old API is unused**

Run: `grep -rn "ddc_open_display\|ddc_get_brightness\|ddc_set_brightness\|ddc_close_display" src`
Expected: matches only inside `abstraction.c`/`abstraction.h` (their own defs).
If any other file still references them, stop — a prior task missed a call site.

- [ ] **Step 2: Delete the four functions and the `ddc_handle_t` typedef**

Remove them from `abstraction.h` and `abstraction.c`, leaving `ddc_enumerate_sources`
and the VCP macros. Keep `VCP_BRIGHTNESS` (used by the ops and mock).

- [ ] **Step 3: Update `README.md`**

Change the caveat line "A single external display only (not handling multiples or
internals yet)" to reflect that multiple external displays are now driven together;
internal-panel handling remains Phase 2.

- [ ] **Step 4: Run the full suite + a build on each locally available platform**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: `dimmit_unit ... Passed`. Push to exercise CI (Linux + Windows x86_64/arm64).

- [ ] **Step 5: Commit**

```bash
git add src/platform/ddc/abstraction.h src/platform/ddc/abstraction.c README.md
git commit -m "refactor(ddc): drop single-display API; document multi-external"
```

---

## Self-review (done at authoring)

- **Spec coverage:** generic interface (Task 1), per-platform providers via the
  existing DDC subsystem (Task 1, no backend churn per the refined spec), controller
  + relative offset-preserving sync (Task 2), controllability rule (Task 1 Step 6),
  partial-failure isolation (Task 2), dynamic re-enumeration/poll placeholder
  (Task 3), fraction unification (Task 4), dimmitd rewire (Task 5), multi-display
  mock testing (Tasks 1-3). Phases 2-3 (internal backends; event-driven hotplug and
  per-unit ids) are explicitly out of this plan.
- **Placeholder scan:** none — every code step shows complete code; the one
  deferred optimization (reconcile re-open) is called out as intentional Phase 3
  work, not a gap.
- **Type consistency:** `brightness_source`/`brightness_ops` names, `ddc_enumerate_sources`,
  `controller_*` signatures, and mock hook names are used identically across tasks.
```
