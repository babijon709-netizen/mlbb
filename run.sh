#!/usr/bin/env bash
# -----------------------------------------------------------------------------
#  run.sh - собрать и запустить меню. Один вход для телефона и для десктопа.
#
#    ./run.sh                  собрать и открыть окно (на телефоне - на весь экран)
#    ./run.sh --shot menu.ppm  отрендерить один кадр в файл, окно не нужно
#    ./run.sh --windowed 900x600
#    ./run.sh --tab 2 --populate
#
#  Android: нужен Termux + Termux:X11 (см. подсказки ниже, скрипт сам их печатает).
#  Linux/macOS: нужен SDL2 (libsdl2-dev / brew install sdl2).
#
#  Всё, что скрипт не понимает, он передаёт приложению - список флагов:
#    ./run.sh --help
# -----------------------------------------------------------------------------
set -u

cd "$(dirname "$0")" || exit 1
ROOT="$PWD"
BIN="$ROOT/build/spectre"
JOBS="$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )"

TERMUX=0
if [ -n "${TERMUX_VERSION:-}" ] || [ -d /data/data/com.termux/files/usr ]; then TERMUX=1; fi

# ------------------------------------------------------------------ helpers ---
c_reset=''; c_dim=''; c_green=''; c_pink=''; c_red=''
if [ -t 1 ]; then
    c_reset=$'\033[0m'; c_dim=$'\033[2m'; c_green=$'\033[38;5;114m'
    c_pink=$'\033[38;5;218m'; c_red=$'\033[38;5;203m'
fi

say()  { printf '%s\n' "$*"; }
ok()   { printf '%s%s%s\n' "$c_green" "$*" "$c_reset"; }
warn() { printf '%s%s%s\n' "$c_pink" "$*" "$c_reset"; }
die()  { printf '%s%s%s\n' "$c_red" "$*" "$c_reset" >&2; exit 1; }

usage() {
    sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
    say ""
    say "Флаги приложения (передаются как есть):"
    "$BIN" --help 2>/dev/null || say "  (сначала соберите: ./run.sh --build)"
}

with_install=1
SHOT=""
BUILD_ONLY=0
APP_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --shot)       SHOT="${2:-menu.ppm}"; shift 2 ;;
        --build)      BUILD_ONLY=1; shift ;;
        --no-install) with_install=0; shift ;;
        -h|--help)    usage; exit 0 ;;
        *)            APP_ARGS+=("$1"); shift ;;
    esac
done

# ------------------------------------------------------------------- Термукс ---
if [ "$TERMUX" = "1" ]; then
    need_pkgs=""
    command -v clang++    >/dev/null 2>&1 || need_pkgs="$need_pkgs clang"
    command -v make       >/dev/null 2>&1 || need_pkgs="$need_pkgs make"
    command -v pkg-config >/dev/null 2>&1 || need_pkgs="$need_pkgs pkg-config"
    pkg-config --exists sdl2 2>/dev/null || need_pkgs="$need_pkgs sdl2"

    if [ -n "$need_pkgs" ]; then
        if [ "$with_install" = "1" ] && command -v pkg >/dev/null 2>&1; then
            warn "Ставлю недостающее:$need_pkgs"
            pkg install -y $need_pkgs || die "pkg install не прошёл - поставьте руками:$need_pkgs"
        else
            die "Не хватает:$need_pkgs (поставьте: pkg install$need_pkgs)"
        fi
    fi
fi

# ---------------------------------------------------------------- ImGui/сборка -
if [ ! -f "$ROOT/external/imgui/imgui.h" ]; then
    warn "ImGui не найден (сабмодуль не подтянут) - пробую достать"
    if [ -d "$ROOT/.git" ] && command -v git >/dev/null 2>&1; then
        git -C "$ROOT" submodule update --init --recursive || true
    fi
fi
if [ ! -f "$ROOT/external/imgui/imgui.h" ] && command -v curl >/dev/null 2>&1; then
    v="$(git -C "$ROOT" config -f .gitmodules --get submodule.external/imgui.branch 2>/dev/null || true)"
    tag="${v:-v1.83}"
    say "Скачиваю ImGui $tag с github..."
    mkdir -p "$ROOT/external/imgui"
    tmp="$(mktemp -d)"
    if curl -fsSL "https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/$tag" -o "$tmp/imgui.tgz"; then
        tar xzf "$tmp/imgui.tgz" -C "$tmp"
        cp -a "$tmp"/imgui-*/. "$ROOT/external/imgui/" 2>/dev/null || true
        ok "ImGui распакован в external/imgui"
    fi
    rm -rf "$tmp"
fi
[ -f "$ROOT/external/imgui/imgui.h" ] || die "Без ImGui собрать нельзя: git submodule update --init --recursive"

say "${c_dim}Собираю...${c_reset}"
make -C "$ROOT" app -j"$JOBS" || die "Сборка не прошла"
ok "Готово: $BIN"
[ "$BUILD_ONLY" = "1" ] && exit 0

FONT_ARGS=(--font "$ROOT/assets/fonts/DejaVuSansMono.ttf" --bold "$ROOT/assets/fonts/DejaVuSansMono-Bold.ttf")

# ----------------------------------------------------------- рендер в картинку -
if [ -n "$SHOT" ]; then
    SDL_VIDEODRIVER=dummy "$BIN" "${FONT_ARGS[@]}" --frames 3 --out "$SHOT" "$@"
    status=$?
    if [ $status -eq 0 ] && command -v convert >/dev/null 2>&1; then
        case "$SHOT" in
            *.ppm) convert "$SHOT" "${SHOT%.ppm}.png" && ok "картинка: ${SHOT%.ppm}.png" ;;
        esac
    fi
    exit $status
fi

# ------------------------------------------------------------------ X11/дисплей
started_x11=0
if [ "$TERMUX" = "1" ] && [ -z "${DISPLAY:-}" ]; then
    if ! command -v termux-x11 >/dev/null 2>&1; then
        warn "Нужен Termux:X11 - это X-сервер для Termux."
        say  "  1) pkg install x11-repo"
        say  "  2) pkg install termux-x11-nightly"
        say  "  3) поставить APK Termux:X11:"
        say  "     https://github.com/termux/termux-x11/releases"
        die  "После установки запустите ./run.sh снова"
    fi
    say "${c_dim}Запускаю Termux:X11...${c_reset}"
    termux-x11 :0 -ac >/dev/null 2>&1 &
    x11_pid=$!
    started_x11=1
    sleep 1
    am start --user 0 -n com.termux.x11/com.termux.x11.MainActivity >/dev/null 2>&1 || \
        am start -n com.termux.x11/.MainActivity >/dev/null 2>&1 || true
    export DISPLAY=:0
    sleep 2
fi

if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ] && [ "$TERMUX" = "0" ]; then
    warn "Дисплей не найден: без X11/Wayland окно не откроется."
    say  "  Linux:  обычный запуск из-под графической сессии"
    say  "  macOS:  запускать из терминала в сессии"
    say  "  или отрендерить картинку: ./run.sh --shot menu.ppm"
fi

cleanup() {
    if [ "$started_x11" = "1" ]; then
        kill "$x11_pid" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM

ok "Запускаю меню${DISPLAY:+ на $DISPLAY}... (Esc - выход, F12 - скриншот)"
"$BIN" "${FONT_ARGS[@]}" "$@"
