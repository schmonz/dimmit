#!/bin/sh
# Build a distribution .pkg from a universal build output dir.
# Usage: build_pkg.sh <universal-bin-dir> <version> <out.pkg>
#   <universal-bin-dir>: dir containing dimmitd, dimmit-up, dimmit-down (fat or thin)
set -eu
BIN_DIR=$1; VERSION=$2; OUT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
# Explicit template: BSD mktemp (all macOS, incl. 10.9) requires one.
ROOT=$(mktemp -d "${TMPDIR:-/tmp}/dimmit-root.XXXXXX")

# Lay out the payload exactly as it should land on disk.
install -d "$ROOT/usr/local/sbin" "$ROOT/usr/local/bin" "$ROOT/Library/LaunchAgents"
install -m 0755 "$BIN_DIR/dimmitd"     "$ROOT/usr/local/sbin/dimmitd"
install -m 0755 "$BIN_DIR/dimmit-up"   "$ROOT/usr/local/bin/dimmit-up"
install -m 0755 "$BIN_DIR/dimmit-down" "$ROOT/usr/local/bin/dimmit-down"
install -m 0644 "$HERE/../../service/service_darwin_agent.plist" \
    "$ROOT/Library/LaunchAgents/com.schmonz.dimmitd.plist"

COMPONENT_DIR=$(mktemp -d "${TMPDIR:-/tmp}/dimmit-pkg.XXXXXX")
COMPONENT="$COMPONENT_DIR/dimmit-component.pkg"
pkgbuild --root "$ROOT" \
    --identifier com.schmonz.dimmit \
    --version "$VERSION" \
    --scripts "$HERE/scripts" \
    --install-location / \
    "$COMPONENT"

productbuild --distribution "$HERE/distribution.xml" \
    --package-path "$COMPONENT_DIR" \
    "$OUT"

echo "Built $OUT"
