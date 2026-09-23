#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"

if [ -z "$NDK" ]; then
    echo "[-] Error: Please set ANDROID_NDK_HOME or ANDROID_NDK_ROOT environment variable." >&2
    echo "    Example: export ANDROID_NDK_HOME=/path/to/android-ndk-r26b" >&2
    exit 1
fi

echo "[*] Using Android NDK: $NDK"
mkdir -p "$ROOT/build" "$ROOT/dist"

if command -v cmake >/dev/null 2>&1; then
    echo "[*] Building with CMake..."
    cmake -S "$ROOT" -B "$ROOT/build" \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a \
        -DANDROID_PLATFORM=android-28 \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "$ROOT/build" --target cyber_overlay -j"$(nproc)"
    cp "$ROOT/build/cyber_overlay" "$ROOT/dist/cyber_overlay_arm64"
else
    echo "[*] CMake not found. Compiling directly using NDK Clang++..."
    TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
    if [ ! -d "$TOOLCHAIN" ]; then
        TOOLCHAIN=$(find "$NDK/toolchains/llvm/prebuilt" -maxdepth 1 -type d | tail -n 1)
    fi
    CXX="$TOOLCHAIN/bin/aarch64-linux-android28-clang++"
    if [ ! -f "$CXX" ]; then
        CXX="$TOOLCHAIN/bin/aarch64-linux-android-clang++"
    fi
    echo "[*] Using CXX: $CXX"
    
    make -C "$ROOT" CXX="$CXX" TARGET="$ROOT/dist/cyber_overlay_arm64"
fi

echo "[*] Packaging self-extracting .sh overlay binary..."
"$ROOT/tools/package.sh" "$ROOT/dist/cyber_overlay_arm64" "$ROOT/dist/cyber_overlay.sh"

echo "[✓] Build complete! File ready at: dist/cyber_overlay.sh"
