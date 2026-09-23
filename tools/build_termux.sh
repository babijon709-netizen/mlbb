#!/usr/bin/env bash
# Script for compiling directly on rooted Android phone inside Termux
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "[+] Detecting Termux environment..."
if ! command -v clang++ >/dev/null 2>&1; then
    echo "[!] Clang++ not found in Termux. Installing dependencies..."
    pkg update -y
    pkg install -y clang make binutils
fi

mkdir -p "$ROOT/dist"

echo "[*] Compiling Cyber Overlay natively for ARM64 on device..."
CXX=clang++
CXXFLAGS="-std=c++17 -O3 -fvisibility=hidden -ffunction-sections -fdata-sections -fno-rtti -fno-exceptions -Wall -Wextra -Wno-unused-parameter"
LDFLAGS="-Wl,--gc-sections -Wl,--strip-all -L/system/lib64 -L$PREFIX/lib"
LIBS="-llog -landroid -lEGL -lGLESv3 -lz -ldl"

make -C "$ROOT" \
    CXX="$CXX" \
    CXXFLAGS="$CXXFLAGS" \
    LDFLAGS="$LDFLAGS" \
    LIBS="$LIBS" \
    TARGET="$ROOT/dist/cyber_overlay_arm64"

echo "[*] Packaging self-extracting .sh executable..."
"$ROOT/tools/package.sh" "$ROOT/dist/cyber_overlay_arm64" "$ROOT/dist/cyber_overlay.sh"

echo ""
echo "[✓] Build succeeded directly on your phone!"
echo "[✓] Executable package: $ROOT/dist/cyber_overlay.sh"
echo ""
echo "To launch as root overlay:"
echo "   su -c \"sh $ROOT/dist/cyber_overlay.sh\""
