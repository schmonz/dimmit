# Dimmit

The brightness keys on your keyboard adjust your display promptly and smoothly -- for _internal_ displays.
Dimmit lets them do the same for external displays.

Simple and reliable on Mac OS X 10.9 Mavericks and Linux Mint.
More platforms and package formats forthcoming.

## Disclaimit

Dimmit is developed with LLM help, without which I wouldn't have bothered figuring out where to start.
My feelings about this are mixed, but I'm happy to have Dimmit.

## Caveits

Some big limitations that I hope to lift:

- All connected external displays now dim together in relative lockstep; internal-panel handling on Linux/Windows is still to come (on macOS the OS already dims the built-in panel on the brightness key)
- On modern macOS, the access permissions probably don't stick
- On Linux, mapping the brightness keys is your responsibility

## Install

Get the [latest release package](https://github.com/schmonz/dimmit/releases/latest) for your platform.

### Modern macOS

Before installing:
```sh
xattr -dr com.apple.quarantine dimmit-*.pkg
```
After installing: enable System Settings → Privacy & Security → Input Monitoring for `dimmitd`.
Worth a shot. If the brightness keys still don't work, that's a known limitation, being worked on.

### Linux

Before installing:
```sh
sudo usermod -a -G i2c $(whoami)
```
After installing: make sure the new group membership is available in your user session.
Then configure your desktop's keyboard settings to map the brightness keys to `dimmit-up` and `dimmit-down`.

## From Source

### Build Dependencies

- CMake
- Debian: `pkg-config` and `libddcutil-dev`
- macOS: Command Line Tools (or full Xcode)

### Runtime Dependencies

- Debian: `libddcutil4`

### Fetch

```sh
git clone --recurse-submodules
```

Or if you've already cloned but didn't know you'd need submodules (sorry):
```sh
git submodule update --init --recursive
```

### Build

```sh
cmake -B build
cmake --build build
```

### Install

```sh
sudo cmake --install build
```

Example service definitions for each platform are in `service/`.

### Configuration

To override the default control socket (`/tmp/dimmit.sock`), set `DIMMIT_SOCK` in the environment.

To override the default log location (stdout), set `DIMMIT_LOG` in the environment. Exception: on macOS, when stdout is not a terminal (such as a LaunchAgent), the default log location is `~/Library/Logs/dimmitd.log`.