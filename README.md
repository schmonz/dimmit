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
- macOS: a Command Line Tools / Xcode toolchain. On macOS &lt; 10.12 you also
  need [`macports-legacy-support`](https://github.com/macports/macports-legacy-support)
  (for `clock_gettime()`); install it from pkgsrc or MacPorts. The macOS
  universal build currently drives its per-architecture sub-builds through
  pkgsrc CMake at `/opt/pkg/bin/cmake`.

Run-time dependencies:
- Linux: `libddcutil4`
- macOS: [Karabiner-Elements](https://karabiner-elements.pqrs.org) to map the
  brightness keys (see [Use](#use)). On macOS &lt; 10.12 the binaries also link
  against the `macports-legacy-support` dylib, so it must remain installed.

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

On macOS no `root` is needed (DDC access doesn't require it, and `dimmitd`'s
root warning there is cosmetic):
```sh
cd build/universal
./dimmitd >/dev/null &
./dimmit-down
```

If it works, great! Map your brightness keys to `dimmit-up` and `dimmit-down`.

> **macOS:** the OS does not let arbitrary tools intercept the brightness keys,
> so mapping them to `dimmit-up`/`dimmit-down` requires
> [Karabiner-Elements](https://karabiner-elements.pqrs.org). This is a hard
> prerequisite for the keys to do anything.

## Install

```sh
sudo cmake --install build
```

This installs `dimmitd` to the system `sbin` directory and `dimmit-up` /
`dimmit-down` to `bin`. Example service definitions to run `dimmitd` at boot
are provided for each platform; install the one that matches yours:
- Linux (systemd): `build/service_systemd.service`
- macOS (launchd): `build/service_darwin.plist`
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
