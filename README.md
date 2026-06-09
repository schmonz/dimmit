# Dimmit

The brightness keys on your keyboard adjust your display promptly and smoothly -- for _internal_ displays.
Dimmit lets them do the same for external displays.

Tested on Linux amd64 and macOS arm64. Other platforms welcome. (There's untested code for NetBSD and macOS amd64.)

Dimmit is heavily vibe-coded.
I certainly didn't know the first thing about how to control monitor brightness on any platform, and would not have bothered writing Dimmit without LLM help.

## Build

Build-time dependencies:
- CMake
- `pkg-config` and `libddcutil-dev` (on Linux)

Run-time dependencies:
- `libddcutil4` (on Linux)

Then:
```sh
cmake -B build && cmake --build build
```

The binaries land in `build/` on Linux and NetBSD, and in `build/universal/` on macOS (a single staging directory for the per-architecture builds).

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

On macOS:
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
