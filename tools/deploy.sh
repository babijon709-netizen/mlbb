#!/usr/bin/env bash
# Push and launch overlay on rooted Android device via ADB
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG="$ROOT/dist/cyber_overlay.sh"

if [ ! -f "$PKG" ]; then
    echo "[-] File $PKG not found! Build it first with tools/build_android.sh or make." >&2
    exit 1
fi

echo "[*] Checking connected ADB devices..."
adb devices

echo "[*] Pushing $PKG to /data/local/tmp/cyber_overlay.sh on phone..."
adb push "$PKG" /data/local/tmp/cyber_overlay.sh
adb shell "chmod 755 /data/local/tmp/cyber_overlay.sh"

echo "[+] Starting overlay as root..."
echo "    Press Ctrl+C to terminate."
adb shell "su -c '/data/local/tmp/cyber_overlay.sh'"
