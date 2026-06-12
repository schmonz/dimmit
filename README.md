# Dimmit

The brightness keys on your keyboard adjust your display promptly and smoothly -- for _internal_ displays.
Dimmit lets them do the same for external displays.

Tested on Linux amd64, macOS arm64 (Apple Silicon, modern macOS), and macOS
x86_64 down to 10.9 Mavericks (DDC read/write verified on a 2013 Mac Pro / AMD
FirePro D500 driving a DELL S3422DW). Other platforms welcome. (There's untested
code for NetBSD.)

Dimmit is heavily vibe-coded.
I certainly didn't know the first thing about how to control monitor brightness on any platform, and would not have bothered writing Dimmit without LLM help.

## Build

Build-time dependencies:
- CMake
- Linux: `pkg-config` and `libddcutil-dev`
- macOS: a Command Line Tools / Xcode toolchain. No third-party libraries are
  needed — the bits missing on older macOS (`clock_gettime()` before 10.12, and
  `IORegistryEntryCopyPath()` before 10.11) are supplied by small in-tree compat
  shims, so the same source builds on everything from Mavericks to Tahoe.

Run-time dependencies:
- Linux: `libddcutil4`
- macOS: nothing — `dimmitd` captures the brightness keys itself (see
  [Use](#use)). On modern macOS grant it Input Monitoring; on 10.9 Mavericks no
  permission is needed. The binaries are self-contained.

This repository vendors `ddcctl` and `m1ddc` as git submodules. Clone with
`git clone --recurse-submodules`, or initialize them in an existing clone:
```sh
git submodule update --init --recursive
```

Then:
```sh
cmake -B build && cmake --build build
```

On macOS the binaries are staged in `build/universal/`: a real arm64 + x86_64
fat binary where the toolchain can target arm64 (Apple Silicon / modern macOS),
or an x86_64-only thin binary where it can't (e.g. Mavericks' Xcode 6). The
x86_64 slice targets 10.9; the arm64 slice targets 12.0. On Linux and NetBSD the
binaries land in `build/`.

## Use

First start the daemon, then send it a command. `dimmitd` is the daemon;
`dimmit-up` and `dimmit-down` are tiny clients that tell it which way to step.

On Linux the daemon needs `root` for `/dev/i2c-*` access and hands the control
socket to the `i2c` group, so add yourself to that group first:
```sh
sudo usermod -a -G i2c $(whoami)
newgrp i2c
cd build
sudo ./dimmitd >/dev/null &
./dimmit-down
```

On macOS no `root` is needed (DDC access doesn't require it, so `dimmitd`
starts as your normal user):
```sh
cd build/universal
./dimmitd >/dev/null &
./dimmit-down
```

If it works, great!

On **Linux/NetBSD**, map your brightness keys to `dimmit-up` and `dimmit-down`
in your desktop's keyboard settings.

> **macOS:** no mapping needed — `dimmitd` captures the brightness keys directly
> (via IOKit HID). On modern macOS, grant it Input Monitoring (System Settings →
> Privacy & Security → Input Monitoring); on 10.9 Mavericks no permission is
> needed. (If you'd rather it not grab the keys, the `dimmit-up`/`dimmit-down`
> clients can still be mapped by any tool, e.g.
> [Karabiner-Elements](https://karabiner-elements.pqrs.org).)

## Install

### macOS: the released `.pkg` (recommended)

Download `dimmit-<version>.pkg` from the
[Releases page](https://github.com/schmonz/dimmit/releases) and double-click it
(or `sudo installer -pkg dimmit-<version>.pkg -target /`). One fat binary covers
Intel (down to 10.9 Mavericks) and Apple Silicon. It installs `dimmitd` to
`/usr/local/sbin`, `dimmit-up` / `dimmit-down` to `/usr/local/bin`, and a
LaunchAgent at `/Library/LaunchAgents/com.schmonz.dimmitd.plist` that launchd
starts in your session at login (the installer also starts it immediately). The
daemon logs to `~/Library/Logs/dimmitd.log`. Re-installing a newer `.pkg` is
safe: it unloads the running agent, swaps the binaries, and reloads.

`dimmitd` captures the brightness keys directly; on modern macOS, grant it Input
Monitoring (see [Use](#use)).

> Phase 1 packages are **unsigned**. On modern macOS, clear the quarantine
> first: `xattr -dr com.apple.quarantine dimmit-<version>.pkg`

**Uninstalling:** macOS `.pkg`s have no built-in uninstaller — removing the
installed files and forgetting the receipt is the standard approach. The receipt
records exactly what landed (`pkgutil --files com.schmonz.dimmit` lists it), so
for dimmit that's:
```sh
launchctl unload -w /Library/LaunchAgents/com.schmonz.dimmitd.plist 2>/dev/null || true
sudo rm -f /usr/local/sbin/dimmitd /usr/local/bin/dimmit-up /usr/local/bin/dimmit-down \
           /Library/LaunchAgents/com.schmonz.dimmitd.plist
sudo pkgutil --forget com.schmonz.dimmit
```

### From source (all platforms)

```sh
sudo cmake --install build
```

This installs `dimmitd` to the system `sbin` directory and `dimmit-up` /
`dimmit-down` to `bin`. Example service definitions to run `dimmitd` at boot
are provided for each platform; install the one that matches yours:
- Linux (systemd): `build/service_systemd.service`
- macOS (launchd): a per-user LaunchAgent. After `sudo cmake --install build`
  installs the binaries, load the agent as your normal user (no sudo):

  ```sh
  sh build/service_darwin_install.sh
  ```

  This copies `com.schmonz.dimmitd.plist` into `~/Library/LaunchAgents/` and
  `launchctl load -w`s it. The daemon logs to `~/Library/Logs/dimmitd.log`.
  (In the macOS universal build the staged files live under the arch sub-build
  dir, e.g. `build/build-x86_64/`.) To stop and remove the agent:

  ```sh
  launchctl unload -w ~/Library/LaunchAgents/com.schmonz.dimmitd.plist
  rm ~/Library/LaunchAgents/com.schmonz.dimmitd.plist
  ```
- NetBSD (rc.d): `build/service_netbsd.sh`

## Configuration

`dimmitd` and the clients talk over a Unix socket, by default `/tmp/dimmit.sock`
(set at build time via the `DIMMIT_SOCK_DEFAULT` CMake cache variable). To
override the path at runtime, set `DIMMIT_SOCK` to the same value for both the
daemon and the clients:
```sh
DIMMIT_SOCK=/run/dimmit.sock ./dimmitd &
DIMMIT_SOCK=/run/dimmit.sock ./dimmit-down
```

### Logging (macOS)

When `dimmitd` runs under the LaunchAgent (or otherwise with a non-terminal
stdout), it redirects its own output to `~/Library/Logs/dimmitd.log` — launchd
captures nothing on its own and 10.9 has no unified log. Run interactively from
a terminal, it leaves stdout alone. Set `DIMMIT_LOG` to redirect to a different
file:
```sh
DIMMIT_LOG=/tmp/dimmitd.log /usr/local/sbin/dimmitd
```
On Linux the daemon logs to stdout/stderr for journald (`journalctl -u
dimmitd`); on NetBSD the rc.d script owns redirection.
