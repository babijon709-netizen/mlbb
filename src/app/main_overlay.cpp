// -----------------------------------------------------------------------------
//  main_overlay.cpp - the same menu, but as an overlay on top of other apps.
//
//  How it works (no APK, no JNI, no Activity):
//
//    * a SurfaceFlinger layer is created through the vendored
//      ANativeWindowCreator (walks libgui.so's symbols - system-only, needs root);
//      the layer is topmost and marked as a trusted overlay, so it sits above
//      the game *and* stays out of the input dispatch tree: touches keep
//      reaching whatever is underneath;
//    * the frames are rasterized by the same CPU renderer the SDL build uses
//      and posted with ANativeWindow_lock()/unlockAndPost() - the CPU producer
//      path of a BufferQueue, no EGL and no GLES involved; the surface is
//      RGBA8888 and drawn premultiplied, so the transparent parts of the menu
//      let the game show through;
//    * input comes from /dev/input (SurfaceFlinger never forwards touches to a
//      layer that is not in the dispatch tree), read-only, and is fed to ImGui
//      as a mouse;
//    * the menu only redraws when something changed, plus a 1 Hz heartbeat for
//      the clock - an idle overlay costs no CPU and no battery.
//
//  Everything the process needs at runtime is already on the phone: libgui.so,
//  libutils.so, libandroid.so and the touch nodes. Build it on the device with
//  Termux's clang (`make overlay` / ./run.sh --overlay) - no NDK required.
//
//  Still a menu and nothing else: no game process is touched.
// -----------------------------------------------------------------------------
#include "imgui.h"
#include "mlbb_gui/Menu.h"
#include "mlbb_gui/Theme.h"
#include "overlay/ANativeWindowCreator.h"
#include "overlay/SurfaceSink.h"
#include "overlay/TouchReader.h"
#include "render/SoftRenderer.h"

#include <csignal>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t g_stop = 0;

void OnSignal(int) { g_stop = 1; }

// ----------------------------------------------------------------- options ---
struct Options {
    int         surfaceW = -1, surfaceH = -1;   // --size (default: the display)
    float       density = 0.0f;                 // 0 = pick from the screen
    float       scaleHint = 0.0f;
    int         menuW = 0, menuH = 0;
    int         layout = -1;                    // -1 auto, 0 wide, 1 compact
    int         tab = -1;
    int         fps = 30;                       // redraw cap while the menu moves
    int         framesLimit = 0;                // --frames N: stop after N frames (0 = until closed)
    bool        populate = false;
    bool        folded = false;
    bool        debugTouch = false;
    bool        grab = false;
    bool        probe = false;                  // --probe: check surface/touch, then quit
    int         timeoutSec = 0;                 // --timeout N: close itself after N seconds
    bool        exitChord = true;
    bool        verbose = true;
    mlbb::overlay::TouchMap touch;
    bool        touchSwapSet = false, touchMirrorXSet = false, touchMirrorYSet = false, touchRotSet = false;
    int         touchRot = 0;
    const char* shot = nullptr;                 // --shot FILE: render offline, no surface
    int         shotBg = -1;                    // --shot-bg RRGGBB: paint a colour under the menu
    // Offline shots only: on a phone these rows are filled from the system
    // properties (see QueryDeviceInfo), so there is nothing to override.
    const char* deviceModel = nullptr, *deviceSystem = nullptr, *deviceRender = nullptr;
};

const char* ArgValue(int argc, char** argv, const char* key)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], key) == 0) return argv[i + 1];
    return nullptr;
}

bool HasFlag(int argc, char** argv, const char* key)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], key) == 0) return true;
    return false;
}

void PrintUsage()
{
    std::printf(
        "spectre overlay - the menu on top of other apps (needs root)\n"
        "\n"
        "  ./run.sh --overlay [options]      build in Termux and start it as root\n"
        "  su -c /data/local/tmp/spectre-overlay [options]\n"
        "\n"
        "  --size WxH        surface size        (default: the whole display)\n"
        "  --menu WxH        window size in the wide layout (default 1120x680)\n"
        "  --density N       pixels per logical unit (default: fit the screen)\n"
        "  --ui-scale N      multiplier on the automatic density\n"
        "  --compact/--wide  one scrolling column / floating window\n"
        "  --tab N           tab to open (0 ESP, 1 AIM, 2 VISUAL, 3 MISC, 4 CONFIG)\n"
        "  --populate        fill every widget with sample state\n"
        "  --folded          start with only the tab strip visible\n"
        "  --fps N           redraw cap while things move (default 30)\n"
        "  --frames N        stop after N frames (with --shot: frames to render)\n"
        "  --shot FILE       render one frame to a PPM image and exit (works on a PC)\n"
        "  --shot-bg RRGGBB  colour to paint behind the menu in that shot, to show\n"
        "                    that the transparent parts really are transparent\n"
        "  --device-model/-system/-render TEXT   override the three DEVICE rows\n"
        "                    (on a phone they come from the system properties)\n"
        "\n"
        "  touch overrides (the mapping is detected automatically):\n"
        "  --touch-swap / --touch-no-swap      screen x  <- panel y\n"
        "  --touch-mirror-x / -y / --touch-no-mirror-x|-y\n"
        "  --touch-rot 0|90|180|270\n"
        "  --debug-touch     draw a crosshair where the touch lands\n"
        "  --grab            take the touch device away from the game (see README)\n"
        "  --probe           create the layer, check touch, report and exit (first run)\n"
        "  --timeout N       close by itself after N seconds (safety net)\n"
        "  --no-exit-chord   do not quit on volume-up + volume-down\n"
        "  --quiet           less chatter on stdout\n");
}

// -------------------------------------------------------------- rendering ----
void SetupKeyMap(ImGuiIO& io)
{
    io.KeyMap[ImGuiKey_Tab] = ImGuiKey_Tab;
    io.KeyMap[ImGuiKey_LeftArrow] = ImGuiKey_LeftArrow;
    io.KeyMap[ImGuiKey_RightArrow] = ImGuiKey_RightArrow;
    io.KeyMap[ImGuiKey_UpArrow] = ImGuiKey_UpArrow;
    io.KeyMap[ImGuiKey_DownArrow] = ImGuiKey_DownArrow;
    io.KeyMap[ImGuiKey_PageUp] = ImGuiKey_PageUp;
    io.KeyMap[ImGuiKey_PageDown] = ImGuiKey_PageDown;
    io.KeyMap[ImGuiKey_Home] = ImGuiKey_Home;
    io.KeyMap[ImGuiKey_End] = ImGuiKey_End;
    io.KeyMap[ImGuiKey_Insert] = ImGuiKey_Insert;
    io.KeyMap[ImGuiKey_Delete] = ImGuiKey_Delete;
    io.KeyMap[ImGuiKey_Backspace] = ImGuiKey_Backspace;
    io.KeyMap[ImGuiKey_Space] = ImGuiKey_Space;
    io.KeyMap[ImGuiKey_Enter] = ImGuiKey_Enter;
    io.KeyMap[ImGuiKey_Escape] = ImGuiKey_Escape;
    io.KeyMap[ImGuiKey_A] = ImGuiKey_A;
    io.KeyMap[ImGuiKey_C] = ImGuiKey_C;
    io.KeyMap[ImGuiKey_V] = ImGuiKey_V;
    io.KeyMap[ImGuiKey_X] = ImGuiKey_X;
    io.KeyMap[ImGuiKey_Y] = ImGuiKey_Y;
    io.KeyMap[ImGuiKey_Z] = ImGuiKey_Z;
}

std::string ExeDir(const char* argv0)
{
    std::string path = argv0 ? argv0 : "";
    const size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
}

bool FileExists(const char* path)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// Looks for a TTF next to the binary (Termux copy) and in the repo's asset
// folder, so the same binary works from build/ and from /data/local/tmp.
// Looks for a TTF next to the binary (that is where run.sh copies them), then
// in the usual places relative to it - so the same ELF works from build/ in the
// repo, from /data/local/tmp and from the Termux prefix. `slot` owns the string
// for the lifetime of the program (the two fonts must not share one buffer).
const char* PickFont(std::string& slot, const std::string& exeDir, const char* const* candidates, int count)
{
    const char* const dirs[] = { "", "assets/fonts/", "../assets/fonts/", "../../assets/fonts/" };
    for (const char* dir : dirs) {
        for (int i = 0; i < count; ++i) {
            slot = exeDir + "/" + dir + candidates[i];
            if (FileExists(slot.c_str())) return slot.c_str();
        }
    }
    slot.clear();
    return nullptr;
}

// The three DEVICE rows are decoration, but on a phone there is no reason to
// make them up: bionic's libc exports the system properties (no libcutils, no
// link-time dependency - the overlay stays self-contained).
struct DeviceInfo {
    std::string model, system, render;
};

DeviceInfo QueryDeviceInfo()
{
    auto prop = [](const char* name, const char* fallback) -> std::string {
        char value[128] = { 0 };
        if (__system_property_get(name, value) > 0 && value[0] != '\0') return std::string(value);
        return std::string(fallback);
    };

    DeviceInfo info;
    info.model = prop("ro.product.model", "android device");

    const std::string release = prop("ro.build.version.release", "");
    const std::string sdk     = prop("ro.build.version.sdk", "");
    if (!release.empty()) {
        info.system = "Android " + release;
        if (!sdk.empty()) info.system += " \xc2\xb7 API " + sdk;
        info.system += " \xc2\xb7 root";
    } else {
        info.system = "android \xc2\xb7 root";
    }

    info.render = "cpu raster \xc2\xb7 SurfaceFlinger";
    return info;
}

void Populate(mlbb::Features& st)
{
    st.espBox = true; st.espSkeleton = true; st.espHealthBar = true; st.espName = true;
    st.espDistance = true; st.espTeamCheck = false; st.espBoxStyle = 1;
    st.aimEnabled = true; st.aimPrediction = true; st.aimFovCircle = true;
    st.aimFov = 96.0f; st.aimSmooth = 7.0f; st.aimPriority = 1;
    st.visMapHack = true; st.visSkillCd = true; st.visZoomOut = true; st.visCamera = 62.0f;
    st.miscAntiBan = true; st.miscNoGrass = true; st.miscDelay = 0.5f;
}

}  // namespace

int main(int argc, char** argv)
{
    Options opt;

    if (HasFlag(argc, argv, "-h") || HasFlag(argc, argv, "--help")) { PrintUsage(); return 0; }

    if (const char* v = ArgValue(argc, argv, "--size"))    std::sscanf(v, "%dx%d", &opt.surfaceW, &opt.surfaceH);
    if (const char* v = ArgValue(argc, argv, "--menu"))    std::sscanf(v, "%dx%d", &opt.menuW, &opt.menuH);
    if (const char* v = ArgValue(argc, argv, "--density")) opt.density = (float)std::atof(v);
    if (const char* v = ArgValue(argc, argv, "--ui-scale")) opt.scaleHint = (float)std::atof(v);
    if (const char* v = ArgValue(argc, argv, "--tab"))     opt.tab = std::atoi(v);
    if (const char* v = ArgValue(argc, argv, "--fps"))     opt.fps = std::atoi(v);
    if (const char* v = ArgValue(argc, argv, "--frames"))  opt.framesLimit = std::atoi(v);
    if (const char* v = ArgValue(argc, argv, "--shot"))    opt.shot = v;
    if (const char* v = ArgValue(argc, argv, "--shot-bg")) opt.shotBg = (int)std::strtol(v, nullptr, 16);
    opt.deviceModel  = ArgValue(argc, argv, "--device-model");
    opt.deviceSystem = ArgValue(argc, argv, "--device-system");
    opt.deviceRender = ArgValue(argc, argv, "--device-render");
    if (HasFlag(argc, argv, "--compact")) opt.layout = 1;
    if (HasFlag(argc, argv, "--wide"))    opt.layout = 0;
    if (HasFlag(argc, argv, "--populate")) opt.populate = true;
    if (HasFlag(argc, argv, "--folded"))   opt.folded = true;
    if (HasFlag(argc, argv, "--debug-touch")) opt.debugTouch = true;
    if (HasFlag(argc, argv, "--grab"))        opt.grab = true;
    if (HasFlag(argc, argv, "--no-exit-chord")) opt.exitChord = false;
    if (HasFlag(argc, argv, "--probe"))         opt.probe = true;
    if (const char* v = ArgValue(argc, argv, "--timeout")) opt.timeoutSec = std::atoi(v);
    if (HasFlag(argc, argv, "--quiet"))        opt.verbose = false;

    if (HasFlag(argc, argv, "--touch-swap"))    { opt.touch.swapXY = true;  opt.touchSwapSet = true; }
    if (HasFlag(argc, argv, "--touch-no-swap")) { opt.touch.swapXY = false; opt.touchSwapSet = true; }
    if (HasFlag(argc, argv, "--touch-mirror-x"))    { opt.touch.mirrorX = true;  opt.touchMirrorXSet = true; }
    if (HasFlag(argc, argv, "--touch-no-mirror-x")) { opt.touch.mirrorX = false; opt.touchMirrorXSet = true; }
    if (HasFlag(argc, argv, "--touch-mirror-y"))    { opt.touch.mirrorY = true;  opt.touchMirrorYSet = true; }
    if (HasFlag(argc, argv, "--touch-no-mirror-y")) { opt.touch.mirrorY = false; opt.touchMirrorYSet = true; }
    if (const char* v = ArgValue(argc, argv, "--touch-rot")) {
        opt.touchRot = std::atoi(v);
        opt.touch.rot = opt.touchRot;
        opt.touchRotSet = true;
    }

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    std::signal(SIGHUP, OnSignal);

    // ------------------------------------------------------------ surface ----
    // --probe is about the real surface, so it wins over --shot.
    const bool offline = (opt.shot != nullptr) && !opt.probe;

    int surfaceW = opt.surfaceW;
    int surfaceH = opt.surfaceH;

    android::ANativeWindowCreator::DisplayInfo display{ 0, 0, 0 };
    void* window = nullptr;

    if (!offline) {
        if (!FileExists("/system/lib64/libgui.so") && !FileExists("/system/lib/libgui.so")) {
            std::fprintf(stderr,
                         "spectre-overlay: /system/lib64/libgui.so not found - this binary only runs on\n"
                         "                 Android (as root). Use the SDL build (./build/spectre) on a PC,\n"
                         "                 or --shot FILE to render a frame offline.\n");
            return 1;
        }

        display = android::ANativeWindowCreator::GetDisplayInfo();
        if (opt.verbose)
            std::printf("overlay: display %dx%d, rotation %d, uid %d\n",
                        display.width, display.height, display.orientation, (int)getuid());

        if (getuid() != 0 && opt.verbose)
            std::printf("overlay: warning: uid %d - creating a surface for a foreign layer stack\n"
                        "         normally needs root, run it through su\n", (int)getuid());

        window = android::ANativeWindowCreator::Create("spectre", opt.surfaceW, opt.surfaceH);
        if (!window) {
            std::fprintf(stderr, "spectre-overlay: SurfaceFlinger refused to create the surface\n"
                                 "                 (root? SELinux? see the messages above)\n");
            return 1;
        }
    }

    mlbb::overlay::SurfaceSink sink;
    if (!offline) {
        if (!sink.Open(window, display.width, display.height)) {
            std::fprintf(stderr, "spectre-overlay: %s\n", sink.Error());
            android::ANativeWindowCreator::Destroy(static_cast<ANativeWindow*>(window));
            return 1;
        }
        surfaceW = sink.Width();
        surfaceH = sink.Height();
    } else if (surfaceW <= 0 || surfaceH <= 0) {
        surfaceW = 2400;   // the default for an offline shot
        surfaceH = 1080;
    }

    // -------------------------------------------------------------- layout ----
    const bool compact = (opt.layout == 1) || (opt.layout < 0 && surfaceH > surfaceW);
    const int  menuW = compact ? (opt.menuW > 0 ? (opt.menuW < 560 ? opt.menuW : 560) : 560)
                               : (opt.menuW > 0 ? opt.menuW : 1120);
    const int  menuH = compact ? (opt.menuH > 0 ? opt.menuH : 900)
                               : (opt.menuH > 0 ? opt.menuH : 680);

    float density = opt.density;
    if (density <= 0.0f) {
        const float margin = compact ? 8.0f : 28.0f;
        const float byW = (float)surfaceW / ((float)menuW + margin * 2.0f);
        const float byH = (float)surfaceH / ((float)menuH + margin * 2.0f);
        density = compact ? byW : (byW < byH ? byW : byH);
        density *= (opt.scaleHint > 0.0f ? opt.scaleHint : 1.0f);
    }
    density = density < 0.35f ? 0.35f : (density > 6.0f ? 6.0f : density);

    int canvasW = (int)std::lround((double)surfaceW / density);
    int canvasH = (int)std::lround((double)surfaceH / density);
    if (canvasW < 200) canvasW = 200;
    if (canvasH < 200) canvasH = 200;

    mlbb::render::SoftRenderer renderer;
    renderer.Init(canvasW, canvasH, density);
    renderer.SetPremultiplied(true);          // the layer wants premultiplied RGBA

    // --------------------------------------------------------------- ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2((float)canvasW, (float)canvasH);
    io.DeltaTime = 1.0f / 60.0f;
    SetupKeyMap(io);

    mlbb::theme::ApplyTheme();

    const std::string exeDir = ExeDir(argv[0]);
    static const char* const kRegular[]  = { "DejaVuSansMono.ttf", "JetBrainsMono-Regular.ttf", "RobotoMono-Regular.ttf" };
    static const char* const kBoldList[] = { "DejaVuSansMono-Bold.ttf", "JetBrainsMono-Bold.ttf", "RobotoMono-Bold.ttf" };

    mlbb::Config cfg;
    cfg.width  = (float)menuW;
    cfg.height = (float)menuH;
    cfg.pixelDensity = density;
    cfg.compact = compact;
    cfg.hint = compact ? "tap to toggle \xc2\xb7 drag to scroll" : "drag the strip to move the window";
    static std::string deviceModel, deviceSystem, deviceRender;
    if (!offline) {
        const DeviceInfo device = QueryDeviceInfo();
        deviceModel  = device.model;
        deviceSystem = device.system;
        deviceRender = device.render;
    } else {
        // No system properties to read on a PC - keep whatever was passed.
        deviceModel  = opt.deviceModel  ? opt.deviceModel  : "offline render";
        deviceSystem = opt.deviceSystem ? opt.deviceSystem : "no device queried";
        deviceRender = opt.deviceRender ? opt.deviceRender : "cpu raster \xc2\xb7 premultiplied";
    }
    cfg.deviceModel  = deviceModel.c_str();
    cfg.deviceSystem = deviceSystem.c_str();
    cfg.deviceRender = deviceRender.c_str();

    static std::string fontRegularPath, fontBoldPath;
    cfg.fontRegular = PickFont(fontRegularPath, exeDir, kRegular, (int)(sizeof(kRegular) / sizeof(kRegular[0])));
    cfg.fontBold    = PickFont(fontBoldPath, exeDir, kBoldList, (int)(sizeof(kBoldList) / sizeof(kBoldList[0])));

    mlbb::Features st;
    st.folded = opt.folded;
    if (opt.populate) Populate(st);
    if (opt.tab >= 0) st.tab = opt.tab;

    mlbb::Fonts fonts = mlbb::LoadFonts(cfg);

    unsigned char* texels = nullptr;
    int texW = 0, texH = 0;
    io.Fonts->GetTexDataAsRGBA32(&texels, &texW, &texH);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);

    if (opt.verbose)
        std::printf("overlay: fonts %s | %s\n",
                    cfg.fontRegular ? cfg.fontRegular : "(built-in)",
                    cfg.fontBold ? cfg.fontBold : "(built-in)");

    if (!cfg.fontRegular || !cfg.fontBold)
        std::fprintf(stderr, "overlay: warning: no TTF found next to the binary - box and block glyphs"
                             " (the ASCII banner) will not render\n");

    // ---------------------------------------------------------------- touch ----
    mlbb::overlay::TouchInput touch;
    bool touchReady = false;

    unsigned touchSetMask = 0;
    if (opt.touchSwapSet)    touchSetMask |= mlbb::overlay::TouchInput::MapSetSwap;
    if (opt.touchMirrorXSet) touchSetMask |= mlbb::overlay::TouchInput::MapSetMirrorX;
    if (opt.touchMirrorYSet) touchSetMask |= mlbb::overlay::TouchInput::MapSetMirrorY;
    if (opt.touchRotSet)     touchSetMask |= mlbb::overlay::TouchInput::MapSetRot;

    if (!offline) {
        touchReady = touch.Init(surfaceW, surfaceH, opt.touch, touchSetMask, opt.grab, opt.verbose);
        if (!touchReady && opt.verbose)
            std::printf("overlay: no touch input - the menu shows, but cannot be clicked\n");
    }

    if (opt.verbose) {
        std::printf("overlay: surface %dx%d, canvas %dx%d, density %.2f, %s layout, menu %dx%d\n",
                    surfaceW, surfaceH, canvasW, canvasH, density, compact ? "compact" : "wide", menuW, menuH);
        std::printf("overlay: %s | quit: the X button", offline ? "offline shot" : "volume-up + volume-down together");
        std::printf("%s\n", opt.exitChord || offline ? "" : " (chord disabled)");
        if (!offline) touch.PrintDevices();
    }

    if (opt.probe) {
        std::printf("overlay: probe ok - the layer, the CPU buffer and %s are all usable.\n",
                    touchReady ? "the touch screen" : "the buttons (no touch screen found)");
        std::printf("overlay: nothing was left on screen; start it for real without --probe\n");
        touch.Close();
        sink.Close();
        ImGui::DestroyContext();
        android::ANativeWindowCreator::Destroy(static_cast<ANativeWindow*>(window));
        android::ANativeWindowCreator::Cleanup();
        return touchReady ? 0 : 2;
    }

    // ----------------------------------------------------------------- loop ----
    // A tap can produce down+up inside a single poll, and ImGui only sees
    // mouse buttons as transitions between frames - so every change of the
    // pointer state is queued and fed one sample per rendered frame. Without
    // this, a quick tap over the game would simply not register.
    struct PointerSample { float x, y; bool down; };
    PointerSample queue[16];
    int queueHead = 0, queueCount = 0;
    PointerSample current{ 0.0f, 0.0f, false };

    auto pushSample = [&](const mlbb::overlay::TouchState& state) {
        if (queueCount == 16) { queueHead = (queueHead + 1) % 16; --queueCount; }   // drop the oldest
        queue[(queueHead + queueCount) % 16] = PointerSample{ state.x / density, state.y / density, state.down };
        ++queueCount;
    };

    auto  clock    = []() { return std::chrono::steady_clock::now(); };
    auto  lastTick = clock();
    double nowSec = 0.0, lastDrawSec = -10.0, lastEventSec = -10.0;
    const double frameBudget = opt.fps > 0 ? 1.0 / (double)opt.fps : 0.0;
    int  frames = 0;
    bool first  = true;
    double drawSeconds = 0.0;

    while (!g_stop) {
        // Poll blocks until a touch arrives, but never longer than one frame:
        // the idle heartbeat and the offline shot rely on that.
        const int timeoutMs = opt.fps > 0 ? (1000 / opt.fps) : 100;
        const bool event = touchReady ? touch.Poll(timeoutMs) : false;
        if (!touchReady) usleep((useconds_t)(timeoutMs * 1000));

        const auto   tick    = clock();
        const double elapsed = std::chrono::duration<double>(tick - lastTick).count();
        lastTick = tick;
        nowSec += elapsed < 1.0 ? elapsed : 0.0;

        if (opt.timeoutSec > 0 && nowSec >= (double)opt.timeoutSec) {
            std::printf("overlay: %d seconds are up - closing\n", opt.timeoutSec);
            break;
        }

        if (touch.ExitChord() && opt.exitChord) {
            std::printf("overlay: volume-up + volume-down - closing\n");
            break;
        }

        if (event) {
            pushSample(touch.State());
            lastEventSec = nowSec;
        }

        // Redraw when the pointer changed, for a moment after the last event
        // (ImGui animates the release), and once a second so the clock ticks.
        const int  wanted = opt.framesLimit > 0 ? opt.framesLimit : (offline ? 1 : 0);
        bool redraw = offline ? (frames < wanted)
                              : (first || queueCount > 0 || (nowSec - lastEventSec < 0.25) || (nowSec - lastDrawSec >= 1.0));
        if (redraw && !first && frameBudget > 0.0 && (nowSec - lastDrawSec) < frameBudget)
            redraw = false;

        if (redraw) {
            const auto drawStart = clock();
            if (queueCount > 0) {
                current = queue[queueHead];
                queueHead = (queueHead + 1) % 16;
                --queueCount;
            }
            io.MousePos     = ImVec2(current.x, current.y);
            io.MouseDown[0] = current.down;

            io.DeltaTime = (float)((elapsed > 0.0 && elapsed < 0.5) ? elapsed : 1.0 / 60.0);

            // There is no backdrop to copy over the buffer like the SDL build
            // does, so it is wiped before every frame: the pixels the UI leaves
            // alone stay premultiplied-transparent and show whatever is on
            // screen underneath (the game). --shot-bg only exists to make that
            // visible in an offline image.
            if (opt.shotBg >= 0)
                renderer.Clear(mlbb::render::Color4{ (unsigned char)((opt.shotBg >> 16) & 0xFF),
                                                     (unsigned char)((opt.shotBg >> 8) & 0xFF),
                                                     (unsigned char)(opt.shotBg & 0xFF), 255 });
            else
                renderer.Clear(mlbb::render::Color4{ 0, 0, 0, 0 });

            ImGui::NewFrame();
            mlbb::DrawMenu(cfg, fonts, st);

            if (opt.debugTouch && !offline) {
                ImDrawList* fg = ImGui::GetForegroundDrawList();
                const ImVec2 p = io.MousePos;
                fg->AddCircleFilled(p, 14.0f, IM_COL32(255, 64, 160, 90), 24);
                fg->AddCircle(p, 26.0f, IM_COL32(255, 64, 160, 220), 48, 2.0f);
                fg->AddLine(ImVec2(p.x - 40.0f, p.y), ImVec2(p.x + 40.0f, p.y), IM_COL32(255, 64, 160, 200), 1.5f);
                fg->AddLine(ImVec2(p.x, p.y - 40.0f), ImVec2(p.x, p.y + 40.0f), IM_COL32(255, 64, 160, 200), 1.5f);
                char info[160];
                std::snprintf(info, sizeof(info), "raw %.3f,%.3f  screen %.0f,%.0f  %s  queue %d",
                              touch.NormX(), touch.NormY(), current.x * density, current.y * density,
                              current.down ? "down" : "up", queueCount);
                fg->AddText(ImVec2(24.0f, 24.0f), IM_COL32(255, 64, 160, 230), info);
            }

            ImGui::Render();
            renderer.Render(ImGui::GetDrawData(), texels, texW, texH);
            ++frames;

            if (!offline && !sink.Present(renderer.Data(), renderer.Pitch()) && opt.verbose)
                std::fprintf(stderr, "overlay: present failed: %s\n", sink.Error());

            drawSeconds += std::chrono::duration<double>(clock() - drawStart).count();
            lastDrawSec = nowSec;
            first = false;
        }

        if (offline && frames >= wanted) break;
        if (opt.framesLimit > 0 && !offline && frames >= opt.framesLimit) break;

        // Closing the window (the X in the title strip) quits the overlay: with
        // no window manager around, there is nobody else to shut it down.
        if (!st.windowOpen) {
            std::printf("overlay: menu closed\n");
            break;
        }
    }

    if (opt.shot) {
        if (renderer.SavePPM(opt.shot, 1))
            std::printf("overlay: wrote %s (%dx%d, density %.2f)\n", opt.shot,
                        renderer.FramebufferWidth(), renderer.FramebufferHeight(), density);
        else
            std::fprintf(stderr, "overlay: cannot write '%s'\n", opt.shot);
    }

    if (opt.verbose && !offline)
        std::printf("overlay: %d frames drawn%s%s\n", frames,
                    frames > 0 ? ", " : "",
                    frames > 0 ? (std::to_string(drawSeconds * 1000.0 / frames) + " ms per redraw").c_str() : "");

    touch.Close();
    sink.Close();
    ImGui::DestroyContext();
    if (window) {
        android::ANativeWindowCreator::Destroy(static_cast<ANativeWindow*>(window));
        android::ANativeWindowCreator::Cleanup();
    }

    return 0;
}
