#!/usr/bin/env bash
set -euo pipefail

BIN="${1:?usage: package.sh <android-arm64-elf> [output.sh]}"
OUT="${2:-dist/cyber_overlay.sh}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ ! -f "$BIN" ]; then
    echo "[-] Error: Binary $BIN does not exist!" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")"

# Copy the shell script header up to and including __PAYLOAD_BELOW__
awk 'BEGIN{p=1} /^__PAYLOAD_BELOW__$/{print; exit} {print}' "$ROOT/run_ui.sh" > "$OUT"

# Append the gzipped binary payload
gzip -n -9 -c "$BIN" >> "$OUT"
chmod 755 "$OUT"

# Generate checksums
sha256sum "$BIN" "$OUT" > "$OUT.sha256"

echo "[+] Successfully packaged self-extracting overlay:"
echo "    Output file: $OUT ($(du -h "$OUT" | cut -f1))"
echo "    Checksum:    $OUT.sha256"
