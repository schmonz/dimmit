# CI/release workflow harmonization — design

**Date:** 2026-07-06
**Status:** approved design

## Problem

`ci.yml` and `release.yml` independently re-implement "checkout, configure,
build, test" per platform, and have drifted:

- **macOS has no CI job at all.** It was added straight to `release.yml`
  (f547f14) when `ci.yml` only had Linux; unlike Windows, which got CI and
  release jobs in the same commit (93784b2), macOS never got backfilled into
  CI. Every push/PR builds zero macOS verification; only a tagged release
  build touches it.
- **Build-flag parity gaps between CI and release**, so CI doesn't always
  verify the exact configuration that ships:
  - Linux CI builds with no `CMAKE_BUILD_TYPE` set; `release-linux` builds
    `Release`.
  - Windows CI omits `-static`; `release-windows` links `-static` (the whole
    point of the Windows distribution — no MSYS2 runtime DLL dependency).
  - `release-macos` itself doesn't set `CMAKE_BUILD_TYPE=Release` either.
- **`release-macos` never runs any tests.** The top-level "universal build"
  path in `CMakeLists.txt` (`cmake -B build` with no
  `CMAKE_OSX_ARCHITECTURES`) hits `return()` before `enable_testing()`/
  `add_test()` are registered — those only exist in the "normal build"
  branch. The tests *do* exist in the per-arch sub-build trees
  (`build/build-arm64`, `build/build-x86_64`) that the universal build
  produces internally via `ExternalProject`, but nothing runs `ctest`
  against them today.
- **`release-linux` never runs tests either** — only `release-windows` has a
  `Test` step today.
- **The Windows DLL-import guard only runs at release time**, so a
  regression that reintroduces a non-system runtime dependency isn't caught
  until someone cuts a tag.

## Goal

One definition of "how each platform is built and tested," shared by CI and
release, so they can't drift again — and close the gaps above along the way.

## Architecture

Extract the configure → build → test (→ guard) steps for each platform into
composite actions under `.github/actions/`. `ci.yml` and `release.yml` both
invoke the *same* composite action for a given platform. `release.yml`'s
jobs add packaging/upload steps afterward; `ci.yml`'s jobs do nothing else.
`actions/checkout` itself stays an explicit step in every calling job (not
inside the composite actions) — both workflows already checkout first, and
leaving it in the caller keeps `submodules: recursive` visible at the call
site. Per-platform matrices (`arch`, `runner`, `msystem`, ... for Windows)
also stay in the calling workflow files, since composite actions can't
declare their own `strategy`.

## Components

- **`.github/actions/determine-version/action.yml`** — the `GITHUB_REF_TYPE`
  bash block currently duplicated three times in `release.yml`. Output:
  `version`.
- **`.github/actions/build-linux/action.yml`** — install apt deps (`cmake
  pkg-config libddcutil-dev`), `cmake -B build -DCMAKE_BUILD_TYPE=Release`,
  build, `ctest --test-dir build`.
- **`.github/actions/build-macos/action.yml`** — `cmake -B build
  -DCMAKE_BUILD_TYPE=Release` (builds the universal binary via the existing
  `ExternalProject` path), `ctest --test-dir build/build-arm64` (native
  execution — the arm64 slice runs natively on the runner; the x86_64/10.9
  slice is not natively runnable there, so it stays guard-only), then the
  10.9 compat guard (`nm` symbol check, moved in verbatim).
- **`.github/actions/build-windows/action.yml`** — inputs: `arch`, `msystem`,
  `prefix`, `cc`. Sets up MSYS2, `cmake -B build -G Ninja
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=<cc>
  -DCMAKE_EXE_LINKER_FLAGS=-static`, build, `ctest --test-dir build`, then
  the DLL-import guard (moved in verbatim, now always-on instead of
  release-only).

## Data flow

- **`ci.yml`**: each job = checkout + the matching `build-*` composite
  action. Nothing else.
- **`release.yml`**: each job = checkout + `determine-version` + the
  matching `build-*` composite action + the existing platform packaging
  step (`build_pkg.sh` / `build_deb.sh` / zip) + `upload-artifact`. The
  `publish` job is untouched — it only consumes uploaded artifacts and
  doesn't care how they were produced.

## What this closes

- macOS gets real CI coverage (universal build + arm64 tests + compat
  guard) on every push/PR, not just at tag time.
- Every job builds `Release`; Windows is `-static` everywhere, in both CI
  and release.
- `release-linux` and `release-macos` gain a real `Test` step for the first
  time.
- The Windows DLL-import guard runs on every push/PR.

## Error handling

No new failure semantics — a failing step inside a composite action fails
the calling job exactly as an inline step would. The `release.yml` `publish`
job's all-or-nothing `needs:` invariant (documented at the top of that file)
is unaffected: any `build-*` composite-action failure still fails its job,
which still blocks `publish`.

## Testing / validation

These are declarative workflow + composite-action YAML files — no unit
tests apply. Validation is: push the new composite actions and updated
`ci.yml`/`release.yml` to a branch and watch real Actions runs for all
three CI platforms plus a `workflow_dispatch` release run, before merging.
Use `actionlint` locally first if available, to catch YAML/expression
mistakes before spending runner time.

## Out of scope

Signing/notarization, the Linux per-distro container matrix, NetBSD
support — tracked separately in `docs/superpowers/backlog/` and
`docs/superpowers/plans/`. This design touches only how existing
Linux/macOS/Windows build+test+package steps are organized across
`ci.yml`/`release.yml`.
