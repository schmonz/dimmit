# TODO

## Platform: macOS

- [x] Mavericks support (x86_64 build + DDC read/write verified on a 2013 Mac
      Pro / AMD FirePro D500 / DELL S3422DW)
- [ ] Build Universal — scaffolding is in place (ExternalProject + lipo staging
      in CMakeLists.txt), but only the x86_64 sub-build is wired up; the arm64
      ExternalProject is still commented out, so the "universal" binary is
      currently thin (x86_64 only)
- [ ] Stop needing `root`
- [ ] Publish `.pkg`
  - [ ] Make it uninstallable
- [ ] Be able to self-update somehow (no GUI, so not Sparkle)
- [ ] Stop needing [Karabiner-Elements](https://karabiner-elements.pqrs.org);
      somehow intercept brightness events directly

## Platform: Linux

- [ ] Stop needing `root`
- [ ] Vendor [libddcutil.a](https://github.com/rockowitz/ddcutil) for consistency?
- [ ] Publish easy-update system packages through OpenBuildService

## Platform: NetBSD

- [ ] Test

## Moar goodies

- [ ] Handle multiple external displays
- [ ] Handle internal displays too
- [ ] Translate to Zig?
