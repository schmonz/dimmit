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

## Use

On Linux:
```sh
sudo usermod -a -G i2c $(whoami)
newgrp i2c
cd build
sudo ./dimmitd >/dev/null &
./dimmit-down
```

On macOS:
```sh
cd build
./dimmitd >/dev/null &
./dimmit-down
```

If it works, great! Install, see the example service files, and map your brightness keys to `dimmit-up` and `dimmit-down`.