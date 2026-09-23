#!/usr/bin/env bash
# -----------------------------------------------------------------------------
#  run.sh - собрать и запустить меню на телефоне (или на десктопе) одной командой.
#
#    ./run.sh                  сам выбрать способ: окно (Termux:X11) или оверлей (root)
#    ./run.sh --overlay        повесить меню ПОВЕРХ других приложений (нужен root,
#                              X11/SDL/NDK не нужны вообще)
#    ./run.sh --window         окно на весь экран (нужен Termux:X11)
#    ./run.sh --check          что вообще есть в системе и что запустится
#    ./run.sh --shot menu.ppm  отрендерить кадр в файл - окно и root не нужны
#    ./run.sh --windowed 900x600 / --tab 2 --populate
#
#  Android: нужен Termux. Для окна - ещё Termux:X11 (APK + пакет), для оверлея -
#  root. Всё, что скрипт не понимает, он передаёт приложению: ./run.sh --help
#
#  Linux/macOS: нужен SDL2 (libsdl2-dev / brew install sdl2), окно как обычно.
# -----------------------------------------------------------------------------
set -u

cd "$(dirname "$0")" || exit 1
ROOT="$PWD"
JOBS="$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )"
PREFIX="${PREFIX:-/data/data/com.termux/files/usr}"
STAGED="/data/local/tmp/spectre-overlay"

# --------------------------------------------------------------- оформление ---
c_reset=''; c_dim=''; c_green=''; c_pink=''; c_red=''
if [ -t 1 ]; then
    c_reset=$'\033[0m'; c_dim=$'\033[2m'; c_green=$'\033[38;5;114m'
    c_pink=$'\033[38;5;218m'; c_red=$'\033[38;5;203m'
fi

say()  { printf '%s\n' "$*"; }
ok()   { printf '%s%s%s\n' "$c_green" "$*" "$c_reset"; }
warn() { printf '%s%s%s\n' "$c_pink" "$*" "$c_reset"; }
die()  { printf '%s%s%s\n' "$c_red" "$*" "$c_reset" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# ------------------------------------------------------------------- флаги ----
MODE="auto"          # auto | window | overlay
CHECK=0
SHOT=""
BUILD_ONLY=0
WITH_INSTALL=1
APP_ARGS=()

usage() {
    sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
    say ""
    say "Флаги приложения (передаются как есть):"
    local bin="$ROOT/build/spectre"
    for a in "$@"; do
        [ "$a" = "--overlay" ] && bin="$ROOT/build/spectre-overlay"
    done
    "$bin" --help 2>/dev/null || say "  (сначала соберите: ./run.sh --build)"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --overlay|--root) MODE="overlay"; shift ;;
        --window)         MODE="window";  shift ;;
        --check|--doctor) CHECK=1;        shift ;;
        --shot)           SHOT="${2:-menu.ppm}"; shift 2 ;;
        --build)          BUILD_ONLY=1;   shift ;;
        --no-install)     WITH_INSTALL=0; shift ;;
        -h|--help)        usage "$@"; exit 0 ;;
        *)                APP_ARGS+=("$1"); shift ;;
    esac
done

# ------------------------------------------------------------ что есть в системе
TERMUX=0
if [ -n "${TERMUX_VERSION:-}" ] || [ -d /data/data/com.termux/files/usr ]; then TERMUX=1; fi

# Термуксу нужен clang, десктопу хватает любого c++ (g++/clang++/c++).
HAVE_CLANG=0
if have clang++ || have clang || have g++ || have c++; then HAVE_CLANG=1; fi
HAVE_MAKE=0;   have make && HAVE_MAKE=1
HAVE_SDL=0;    pkg-config --exists sdl2 2>/dev/null && HAVE_SDL=1
HAVE_IMGUI=0;  [ -f "$ROOT/external/imgui/imgui.h" ] && HAVE_IMGUI=1
HAVE_FONTS=0;  [ -f "$ROOT/assets/fonts/DejaVuSansMono.ttf" ] && HAVE_FONTS=1
HAVE_X11TOOL=0; have termux-x11 && HAVE_X11TOOL=1
HAVE_DISPLAY=0
if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then HAVE_DISPLAY=1; fi
HAVE_SU=0;     have su && HAVE_SU=1
HAVE_ROOT=0
if [ "$HAVE_SU" = "1" ] && [ "$TERMUX" = "1" ]; then
    su -c id >/dev/null 2>&1 && HAVE_ROOT=1
elif [ "$(id -u 2>/dev/null || echo 1)" = "0" ]; then
    HAVE_ROOT=1
fi

mark() { if [ "$1" = "1" ]; then printf '%s✓%s' "$c_green" "$c_reset"; else printf '%s✗%s' "$c_red" "$c_reset"; fi; }

# --------------------------------------------------------------------- check ---
if [ "$CHECK" = "1" ]; then
    say "окружение"
    say "  Termux .......... $(mark "$TERMUX")      clang ... $(mark "$HAVE_CLANG")   make ... $(mark "$HAVE_MAKE")"
    say "  SDL2 ............ $(mark "$HAVE_SDL")      Termux:X11 $(mark "$HAVE_X11TOOL")   DISPLAY $(mark "$HAVE_DISPLAY")"
    say "  root (su) ....... $(mark "$HAVE_ROOT")      ImGui ... $(mark "$HAVE_IMGUI")   шрифты $(mark "$HAVE_FONTS")"
    say "  собрано: build/spectre $(mark "$([ -x "$ROOT/build/spectre" ] && echo 1 || echo 0)")   build/spectre-overlay $(mark "$([ -x "$ROOT/build/spectre-overlay" ] && echo 1 || echo 0)")"
    say ""
    say "что можно запустить:"
    if [ "$TERMUX" = "1" ]; then
        [ "$HAVE_ROOT" = "1" ] && say "  ./run.sh --overlay   оверлей поверх других приложений (X11 не нужен)"
        if [ "$HAVE_X11TOOL" = "1" ] || [ "$HAVE_DISPLAY" = "1" ]; then
            say "  ./run.sh             окно на весь экран через Termux:X11"
        else
            say "  ./run.sh             окно - но сначала: pkg install x11-repo && pkg install termux-x11-nightly"
            say "                       и приложение Termux:X11 (APK) с github.com/termux/termux-x11/releases"
        fi
    else
        say "  ./run.sh             окно на этой машине (нужен SDL2)"
        say "  ./run.sh --overlay --shot menu.ppm   оверлей умеет только Android, тут - только картинка"
    fi
    say ""
    say "  ./run.sh --shot menu.ppm   отрендерить кадр без окна и без root"
    exit 0
fi

# -------------------------------------------------------------- выбор способа -
if [ "$MODE" = "auto" ]; then
    if [ -n "$SHOT" ]; then
        # Картинку умеет и оверлей, но у оконной сборки больше флагов - берём её,
        # если SDL есть; иначе оверлей (он вообще без зависимостей).
        if [ "$HAVE_SDL" = "1" ] || [ "$TERMUX" != "1" ]; then MODE="window"; else MODE="overlay"; fi
    elif [ "$HAVE_DISPLAY" = "1" ] || [ "$HAVE_X11TOOL" = "1" ] || [ "$TERMUX" != "1" ]; then
        MODE="window"
    elif [ "$HAVE_ROOT" = "1" ]; then
        warn "Termux:X11 не найден - запускаю оверлеем (ему X11 не нужен)."
        MODE="overlay"
    else
        MODE="none"
    fi
fi

if [ "$MODE" = "none" ]; then
    warn "Не вижу, как запустить меню на этом телефоне. Варианты:"
    say  "  1) оверлей поверх других приложений (нужен root):"
    say  "       ./run.sh --overlay"
    say  "  2) окно через Termux:X11:"
    say  "       pkg install x11-repo && pkg install termux-x11-nightly"
    say  "       + приложение Termux:X11 (APK): https://github.com/termux/termux-x11/releases"
    say  "  3) просто картинка:  ./run.sh --shot menu.ppm"
    die  "Подробнее: ./run.sh --check"
fi

# ------------------------------------------------------------------- Termux ---
if [ "$TERMUX" = "1" ]; then
    need_pkgs=""
    [ "$HAVE_CLANG" = "1" ] || need_pkgs="$need_pkgs clang"
    [ "$HAVE_MAKE"  = "1" ] || need_pkgs="$need_pkgs make"
    if [ "$MODE" = "window" ]; then
        # Оверлею SDL не нужен: он рисует сам и говорит прямо с SurfaceFlinger.
        pkg-config --exists pkg-config 2>/dev/null || true
        [ "$HAVE_SDL" = "1" ] || need_pkgs="$need_pkgs sdl2"
    fi

    if [ -n "$need_pkgs" ]; then
        if [ "$WITH_INSTALL" = "1" ] && have pkg; then
            warn "Ставлю недостающее:$need_pkgs"
            pkg install -y $need_pkgs || die "pkg install не прошёл - поставьте руками:$need_pkgs"
            HAVE_CLANG=1; HAVE_MAKE=1
            [ "$MODE" = "window" ] && HAVE_SDL=1
        else
            die "Не хватает:$need_pkgs (поставьте: pkg install$need_pkgs)"
        fi
    fi
fi

# ---------------------------------------------------------------- ImGui/сборка -
if [ ! -f "$ROOT/external/imgui/imgui.h" ]; then
    warn "ImGui не найден (сабмодуль не подтянут) - пробую достать"
    if [ -d "$ROOT/.git" ] && have git; then
        git -C "$ROOT" submodule update --init --recursive || true
    fi
fi
if [ ! -f "$ROOT/external/imgui/imgui.h" ] && have curl; then
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

BIN="$ROOT/build/spectre"
if [ "$MODE" = "overlay" ]; then
    say "${c_dim}Собираю оверлей (без SDL, без NDK - только clang)...${c_reset}"
    make -C "$ROOT" overlay -j"$JOBS" || die "Сборка не прошла"
    BIN="$ROOT/build/spectre-overlay"
else
    say "${c_dim}Собираю...${c_reset}"
    make -C "$ROOT" app -j"$JOBS" || die "Сборка не прошла"
fi
ok "Готово: $BIN"
[ "$BUILD_ONLY" = "1" ] && exit 0

FONT_ARGS=()
if [ "$MODE" = "window" ]; then
    FONT_ARGS=(--font "$ROOT/assets/fonts/DejaVuSansMono.ttf" --bold "$ROOT/assets/fonts/DejaVuSansMono-Bold.ttf")
fi

# ----------------------------------------------------------- рендер в картинку -
if [ -n "$SHOT" ]; then
    if [ "$MODE" = "overlay" ]; then
        SDL_VIDEODRIVER=dummy "$BIN" --shot "$SHOT" "${APP_ARGS[@]+"${APP_ARGS[@]}"}"
    else
        SDL_VIDEODRIVER=dummy "$BIN" "${FONT_ARGS[@]}" --frames 3 --out "$SHOT" "${APP_ARGS[@]+"${APP_ARGS[@]}"}"
    fi
    status=$?
    if [ $status -eq 0 ] && have convert; then
        case "$SHOT" in
            *.ppm) convert "$SHOT" "${SHOT%.ppm}.png" && ok "картинка: ${SHOT%.ppm}.png" ;;
        esac
    fi
    exit $status
fi

# ------------------------------------------------------------------ оверлей ----
if [ "$MODE" = "overlay" ]; then
    if [ "$TERMUX" != "1" ]; then
        warn "Оверлей живёт на Android (SurfaceFlinger) - на этой машине он умеет только --shot."
        "$BIN" "${APP_ARGS[@]+"${APP_ARGS[@]}"}"
        exit $?
    fi

    have su || die "su не найден: для оверлея нужен root (Magisk/SuperSU)"
    say "Проверяю root - на телефоне разрешите запрос суперпользователя..."
    su -c id >/dev/null 2>&1 || die "root недоступен (su -c id не сработал)"

    libcxx="$(find "$PREFIX/lib" -maxdepth 1 -name 'libc++_shared.so' 2>/dev/null | head -1)"
    say "Кладу бинарь и шрифты в $STAGED (оттуда root его запустит)..."

    su -c "mkdir -p $STAGED && cp '$BIN' $STAGED/spectre-overlay && chmod 755 $STAGED/spectre-overlay" \
        || die "не получилось скопировать бинарь в $STAGED - root разрешён?"
    su -c "cp '$ROOT'/assets/fonts/*.ttf $STAGED/ 2>/dev/null" \
        || warn "шрифты не скопировались - меню отрисуется встроенным шрифтом (без ASCII-баннера)"
    if [ -n "$libcxx" ]; then
        su -c "cp '$libcxx' $STAGED/ 2>/dev/null" || warn "libc++_shared.so не скопировался: $libcxx"
    else
        warn "libc++_shared.so не найден в $PREFIX/lib - если бинарь не стартует, покажите это сообщение"
    fi
    su -c "test -x $STAGED/spectre-overlay" || die "в $STAGED нет исполняемого spectre-overlay"

    quoted="$(printf '%q ' "${APP_ARGS[@]+"${APP_ARGS[@]}"}")"
    say ""
    ok "Меню поверх всего. Тап - клик, свайп - прокрутка. Выход: ✕ в меню или Громкость+ и Громкость- вместе."
    say "${c_dim}Касания попадают не туда? ./run.sh --overlay --debug-touch, затем"
    say "подгоните --touch-swap / --touch-mirror-x|y / --touch-rot.${c_reset}"
    say ""

    su -c "cd $STAGED && LD_LIBRARY_PATH=$STAGED:$PREFIX/lib ./spectre-overlay $quoted"
    exit $?
fi

# ---------------------------------------------------------------- X11/дисплей ---
started_x11=0
x11_pid=""
if [ "$TERMUX" = "1" ] && [ -z "${DISPLAY:-}" ]; then
    if ! have termux-x11; then
        warn "Нужен Termux:X11 - это X-сервер для Termux (или запустите ./run.sh --overlay)."
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
    if [ "$started_x11" = "1" ] && [ -n "$x11_pid" ]; then
        kill "$x11_pid" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM

ok "Запускаю меню${DISPLAY:+ на $DISPLAY}... (тап - клик, свайп - прокрутка, Esc - выход)"
"$BIN" "${FONT_ARGS[@]}" "${APP_ARGS[@]+"${APP_ARGS[@]}"}"
status=$?

# Окно могло не открыться (нет дисплея, X-сервер не поднялся) - подскажем оверлей.
if [ $status -ne 0 ] && [ "$TERMUX" = "1" ]; then
    warn "Меню закрылось с кодом $status."
    if [ "$HAVE_ROOT" = "1" ]; then
        say "Можно попробовать оверлей - ему X11 не нужен:  ./run.sh --overlay"
    else
        say "Проверьте Termux:X11 (./run.sh --check) или запустите оверлей с root: ./run.sh --overlay"
    fi
fi
exit $status
