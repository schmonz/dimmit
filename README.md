# Dimmit

The brightness keys on your keyboard adjust your display promptly and smoothly -- for _internal_ displays.
Dimmit lets them do the same for external displays.

Tested on Linux. Other platforms welcome. (There's untested code for NetBSD and macOS.)

## Build

Build-time dependencies:
- CMake
- `libddcutil-dev`

Run-time dependencies:
- `libddcutil4`

## Use

On Linux:
```sh
sudo usermod -a -G video $(whoami)
newgrp video
cd build
sudo ./dimmitd >/dev/null &
./dimmitc down
```

If it works, great! Install, see the example service files, and wire up your brightness keys to `dimmitc`.