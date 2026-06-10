# TODO

## Platform: macOS

- [x] Mavericks support (x86_64 build + DDC read/write verified on a 2013 Mac
      Pro / AMD FirePro D500 / DELL S3422DW)
- [x] Build Universal — the arm64 ExternalProject is gated behind a configure-
      time compiler probe, so a modern macOS host emits a real arm64 + x86_64 fat
      binary (confirmed on Tahoe) and a Mavericks host falls back to a thin
      x86_64 binary; lipo staging into `build/universal/` handles both
- [ ] Stop needing `root` (verified: 10.9 DDC needs neither root nor GUI-session
      ownership, so the daemon's root warning is cosmetic on macOS)
- [ ] Re-resolve the display on sleep/wake/hotplug — the daemon caches the
      IOServicePort at startup, but 10.9 republishes the IOFramebuffer service on
      sleep/wake/resolution-change/replug, after which writes silently fail;
      register `CGDisplayRegisterReconfigurationCallback` or re-open per command
- [ ] Ship `service_darwin.plist` as a per-user LaunchAgent rather than a root
      LaunchDaemon, and confirm it loads on 10.9
- [ ] Fall back to ddcctl's `IOFramebufferPortFromCGDisplayID` matcher when
      `CGDisplayIOServicePort` fails (we use only the latter — fine on 10.9, but
      it discards ddcctl's fallback matching)
- [ ] Publish `.pkg`
  - [ ] Make it uninstallable
  - [ ] Link macports-legacy-support statically (the `.a`) so the binaries don't
        depend on `/opt/pkg` at runtime
- [ ] Be able to self-update somehow (no GUI, so not Sparkle)
- [ ] Stop needing [Karabiner-Elements](https://karabiner-elements.pqrs.org);
      somehow intercept brightness events directly

## Platform: Linux

- [ ] Stop needing `root`
- [ ] Vendor [libddcutil.a](https://github.com/rockowitz/ddcutil) for consistency?
- [ ] Publish easy-update system packages through OpenBuildService

## Platform: NetBSD

- [ ] Test

## Testability

What would have made the current tests easier to write, and how to get there:

- [ ] Split the brightness/debounce state machine out of `dimmitd.c` into a
      standalone module (no sockets, threads, or stdio) that the daemon and the
      tests both link normally — so tests no longer have to `#include
      "dimmitd.c"` under a `DIMMIT_TESTING` guard just to reach static helpers.
- [ ] Inject the clock into the debounce logic (pass `now` in, or take a clock
      function pointer) so timing/debounce is tested deterministically, without
      real sleeps and without spinning up the worker thread.
- [ ] Exercise the accept/command loop end to end against a fake socket plus the
      in-memory `ddc_impl_test.c` display, not just the extracted helpers — the
      `ddc_impl.h` seam already makes the backend swappable; extend that to the
      socket I/O so a request can be driven through to a simulated brightness
      change in a test.

## Build & release automation

Goal: one command (locally) and one workflow (CI) produce binaries for every
target: macOS x86_64 (10.9 Mavericks), macOS arm64, Linux, NetBSD.

Known constraints to design around:
- A Mavericks Intel host builds only a thin x86_64 slice (its Xcode 6 clang
  can't target arm64), but that slice is verified to run on 10.9.
- A modern Apple Silicon host (Tahoe) builds a real universal binary, but its
  x86_64 slice — though it sets a 10.9 deployment target — is compiled against a
  modern SDK and is not verified to run on 10.9. So the artifact actually
  attested on 10.9 still has to come from the Mavericks machine itself.
- GitHub Actions offers no Mavericks-era macOS runner and no native NetBSD
  runner.

- [ ] Decide and document the (host → target) matrix: which host produces which
      artifact, and how the macOS x86_64-on-10.9 vs arm64 split is resolved.
- [ ] Make `cmake -B build && cmake --build build` produce the correct native
      target with no manual flags on each of: Mavericks Intel, Apple Silicon,
      Linux, NetBSD. (macOS now auto-detects its buildable slices via the arm64
      probe; Linux builds natively; NetBSD still untested.)
- [x] Re-enable the arm64 ExternalProject in `CMakeLists.txt` and emit a real
      fat macOS binary where the host SDK allows (Apple Silicon), falling back
      to thin x86_64 on Mavericks. Done via a configure-time `-arch arm64`
      compiler probe, so no manual editing per host.
- [ ] Local "build everything" target/script that fans out to the available
      hosts (e.g. over SSH to a NetBSD box and a Linux box, plus the local Mac)
      and collects the artifacts in one place.
- [ ] GitHub Actions release workflow: ubuntu runner for Linux; a macOS runner
      for arm64; NetBSD via a VM action (e.g. `vmactions/netbsd-vm`); the 10.9
      x86_64 build via a self-hosted runner on the Mavericks Intel Mac (no
      hosted runner exists for it).
- [ ] Collect the per-target artifacts into a single release (ties into the
      `.pkg` and system-package items above).
- [ ] Link only the vendored code we actually use, on every platform. Static
      archives link at object-file granularity, so an unused function riding in
      a `.o` we do use still gets pulled in (e.g. ddcctl's matcher/EDID code
      comes in with `DDC.c.o`). Enable dead-code stripping to drop the rest:
      `-Wl,-dead_strip` on macOS (ld64), and `-ffunction-sections
      -fdata-sections` + `-Wl,--gc-sections` on GNU/BSD ld. Avoid
      `-force_load`/`--whole-archive`, which would defeat this.

## Moar goodies

- [ ] Handle multiple external displays
- [ ] Handle internal displays too
- [ ] Translate to Zig?
