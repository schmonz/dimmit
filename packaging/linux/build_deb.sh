#!/bin/sh
# Build a dimmit .deb from a CMake build dir. Run inside the target distro's
# container (or plain ubuntu-latest for a basic build).
# Usage: build_deb.sh <build-dir> <version> <suite> <out.deb>
#
# If packaging/linux/dimmit-archive-keyring.asc exists, the .deb also ships the
# apt signing key + a per-<suite> apt source -- the "bootstrap" package that
# wires up apt upgrades. Without it you get a basic .deb (binaries + service),
# which is enough to prove the build+publish path.
set -eu
BUILD=$1; VERSION=$2; SUITE=$3; OUT=$4
HERE=$(cd "$(dirname "$0")" && pwd)
STAGE=$(mktemp -d)

# Install the built tree under /usr (Debian paths, not /usr/local).
DESTDIR="$STAGE" cmake --install "$BUILD" --prefix /usr >/dev/null

install -d "$STAGE/usr/lib/systemd/system" "$STAGE/DEBIAN"
# Force ExecStart to the packaged path (/usr/sbin), regardless of the build's
# configure-time CMAKE_INSTALL_PREFIX (which the unit's @...@ was filled from).
sed 's|^ExecStart=.*|ExecStart=/usr/sbin/dimmitd|' \
        "$BUILD/service_systemd.service" \
        > "$STAGE/usr/lib/systemd/system/dimmitd.service"
chmod 0644 "$STAGE/usr/lib/systemd/system/dimmitd.service"

if [ -f "$HERE/dimmit-archive-keyring.asc" ]; then
    install -d "$STAGE/usr/share/keyrings" "$STAGE/etc/apt/sources.list.d"
    gpg --dearmor < "$HERE/dimmit-archive-keyring.asc" \
        > "$STAGE/usr/share/keyrings/dimmit-archive-keyring.gpg"
    sed "s/@SUITE@/$SUITE/" "$HERE/templates/dimmit.sources.in" \
        > "$STAGE/etc/apt/sources.list.d/dimmit.sources"
else
    echo "note: no signing key; building a basic .deb (no apt source)" >&2
fi

# Compute Depends from the binary's NEEDED sonames -> owning packages, via
# objdump + dpkg -S. (dpkg-shlibdeps expects a debian/ source tree and emits
# nothing standalone.) This also yields looser deps -- just the package names,
# no strict >= version -- which is friendlier across Debian/Ubuntu releases and
# matches whatever libddcutil SONAME this container actually linked.
echo "staged binaries:" >&2
ls -l "$STAGE/usr/sbin" "$STAGE/usr/bin" >&2 || true
DEPS=""; _seen=" "
for soname in $(objdump -p "$STAGE/usr/sbin/dimmitd" | awk '/NEEDED/ {print $2}'); do
    libfile=$(ldconfig -p | awk -v s="$soname" '$1==s {print $NF; exit}')
    [ -n "$libfile" ] || { echo "warn: no file for soname $soname" >&2; continue; }
    # ldconfig reports /lib/... but on merged-/usr systems dpkg tracks the file
    # under /usr/lib/...; canonicalize (also resolves soname -> real file).
    real=$(readlink -f "$libfile" 2>/dev/null || echo "$libfile")
    pkg=$(dpkg -S "$real" 2>/dev/null | head -n1 | cut -d: -f1)
    [ -n "$pkg" ] || pkg=$(dpkg -S "$soname" 2>/dev/null | grep "/$soname" | head -n1 | cut -d: -f1)
    [ -n "$pkg" ] || { echo "warn: no package owns $libfile ($real)" >&2; continue; }
    case "$_seen" in *" $pkg "*) continue ;; esac
    _seen="$_seen$pkg "
    DEPS="${DEPS:+$DEPS, }$pkg"
done
echo "computed Depends: $DEPS" >&2
[ -n "$DEPS" ] || { echo "could not compute Depends from $STAGE/usr/sbin/dimmitd" >&2; exit 1; }

cat > "$STAGE/DEBIAN/control" <<EOF
Package: dimmit
Version: $VERSION
Architecture: amd64
Maintainer: Amitai Schleier <schmonz-web-dimmit@schmonz.com>
Depends: $DEPS
Section: utils
Priority: optional
Description: Smooth external-display brightness via DDC
 dimmitd controls external monitor brightness over DDC/CI; dimmit-up and
 dimmit-down are clients for the brightness keys.
EOF
for s in postinst prerm postrm; do
    install -m 0755 "$HERE/scripts/$s" "$STAGE/DEBIAN/$s"
done

dpkg-deb --build --root-owner-group "$STAGE" "$OUT"
echo "Built $OUT"
dpkg-deb --info "$OUT" | sed -n '/Package:/,/Description:/p'
