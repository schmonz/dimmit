# TODO

## Platform: macOS

- Stop needing `root` (verified: 10.9 DDC needs neither root nor GUI-session
      ownership, so the daemon's root warning is cosmetic on macOS)
- Re-resolve the display on sleep/wake/hotplug — the daemon caches the
      IOServicePort at startup, but 10.9 republishes the IOFramebuffer service on
      sleep/wake/resolution-change/replug, after which writes silently fail;
      register `CGDisplayRegisterReconfigurationCallback` or re-open per command
- Ship `service_darwin.plist` as a per-user LaunchAgent rather than a root
      LaunchDaemon, and confirm it loads on 10.9
- Fall back to ddcctl's `IOFramebufferPortFromCGDisplayID` matcher when
      `CGDisplayIOServicePort` fails (we use only the latter — fine on 10.9, but
      it discards ddcctl's fallback matching)
- Publish `.pkg`
  - Make it uninstallable
- Be able to self-update somehow (no GUI, so not Sparkle)
- Stop needing [Karabiner-Elements](https://karabiner-elements.pqrs.org);
      somehow intercept brightness events directly

## Platform: Linux

- Stop needing `root`
- Vendor [libddcutil.a](https://github.com/rockowitz/ddcutil) for consistency?
- Publish easy-update system packages through OpenBuildService

## Platform: NetBSD

- Test

## Platform: Windows

- Investigate need, feasibility, utility -- from 11 back to XP

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

- Decide and document the (host → target) matrix: which host produces which
      artifact, and how the macOS x86_64-on-10.9 vs arm64 split is resolved.
- Make `cmake -B build && cmake --build build` produce the correct native
      target with no manual flags on each of: Mavericks Intel, Apple Silicon,
      Linux, NetBSD. (macOS now auto-detects its buildable slices via the arm64
      probe; Linux builds natively; NetBSD still untested.)
- Local "build everything" target/script that fans out to the available
      hosts (e.g. over SSH to a NetBSD box and a Linux box, plus the local Mac)
      and collects the artifacts in one place.
- GitHub Actions release workflow: ubuntu runner for Linux; a macOS runner
      for arm64; NetBSD via a VM action (e.g. `vmactions/netbsd-vm`); the 10.9
      x86_64 build via a self-hosted runner on the Mavericks Intel Mac (no
      hosted runner exists for it).
- Collect the per-target artifacts into a single release (ties into the
      `.pkg` and system-package items above).
- Link only the vendored code we actually use, on every platform. Static
      archives link at object-file granularity, so an unused function riding in
      a `.o` we do use still gets pulled in (e.g. ddcctl's matcher/EDID code
      comes in with `DDC.c.o`). Enable dead-code stripping to drop the rest:
      `-Wl,-dead_strip` on macOS (ld64), and `-ffunction-sections
      -fdata-sections` + `-Wl,--gc-sections` on GNU/BSD ld. Avoid
      `-force_load`/`--whole-archive`, which would defeat this.

## Moar goodies

- Handle multiple external displays
- Handle internal displays too
- Adjust multiple displays -- including internal ones -- in synchrony. Two
      architectural consequences for the `ddc/` subsystem:
      1. The abstraction selects a *single* display today (`ddc_open_display`
         picks the first non-builtin), so synchronized control needs it to
         manage a set of displays, not one handle.
      2. The abstraction is not actually DDC-independent yet: the VCP feature
         codes and the non-table VCP value packing live in
         `ddc/abstraction.{c,h}`, not in the backends. Internal panels aren't
         driven over DDC (CoreDisplay/DisplayServices on macOS, sysfs backlight
         on Linux), so adding such a backend means pushing the VCP/DDC specifics
         down into the DDC implementations and having the top layer speak
         generic brightness (current/max, or 0-100) -- at which point it is no
         longer a `ddc_*` API and would be renamed accordingly.
- Translate to Zig?
