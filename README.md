# Dimmit

The brightness keys on your keyboard adjust your display promptly and smoothly -- for _internal_ displays.
Dimmit lets them do the same for external displays.

Tested on Linux amd64 and macOS arm64. Other platforms welcome. (There's untested code for NetBSD and macOS amd64.)

## Build

Build-time dependencies:
- CMake
- `libddcutil-dev` (on Linux)

Run-time dependencies:
- `libddcutil4` (on Linux)

## Use

On Linux:
```sh
sudo usermod -a -G video $(whoami)
newgrp video
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