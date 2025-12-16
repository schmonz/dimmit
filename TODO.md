# TODO

## Platform: macOS

- [ ] Build Universal
- [ ] Stop needing `root`
- [ ] Publish `.pkg`
  - [ ] Make it uninstallable
- [ ] Be able to self-update somehow (no GUI, so not Sparkle)
- [ ] Stop needing [Karabiner-Elements](https://karabiner-elements.pqrs.org);
      somehow intercept brightness events directly
- [ ] Stop needing bespoke code; somehow vendor
      [m1ddc](https://github.com/waydabber/m1ddc)
      and
      [ddcctl](https://github.com/kfix/ddcctl)
      and make static libraries from each

## Platform: Linux

- [ ] Stop needing `root`
- [ ] Publish easy-update system packages through OpenBuildService

## Platform: NetBSD

- [ ] Test

## Moar goodies

- [ ] Handle multiple external displays
- [ ] Handle internal displays too
- [ ] Translate to Zig?