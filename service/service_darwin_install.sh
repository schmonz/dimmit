#!/bin/sh
# Install the dimmitd per-user LaunchAgent for the current user (no sudo).
# The daemon binary is installed separately (sudo cmake --install ..., to sbin).
#
# launchctl load -w is used deliberately: it is the only mechanism on 10.9
# (which predates bootstrap/bootout) and still works through current macOS.
set -e

PLIST_SRC="$(cd "$(dirname "$0")" && pwd)/service_darwin.plist"
AGENTS_DIR="$HOME/Library/LaunchAgents"
LABEL="com.schmonz.dimmitd"
DEST="$AGENTS_DIR/$LABEL.plist"

mkdir -p "$AGENTS_DIR"
cp "$PLIST_SRC" "$DEST"

# Reload if it was already loaded (ignore errors on first install).
launchctl unload -w "$DEST" 2>/dev/null || true
launchctl load -w "$DEST"

echo "Loaded $LABEL from $DEST"
echo "Logs: $HOME/Library/Logs/dimmitd.log"
