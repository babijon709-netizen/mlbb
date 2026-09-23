// -----------------------------------------------------------------------------
//  src/app/main_sdl.cpp - the app: an SDL2 window with the menu in it.
//
//  Rendering goes through the CPU rasterizer (src/render/SoftRenderer), so no
//  GPU, driver or GL context is involved - which is what makes it run on a
//  phone through Termux + Termux:X11. Input is plain SDL2, so a touch screen
//  works as-is (Termux:X11 turns taps and drags into mouse events).
//
//  Typical use:
//    ./build/spectre                      # fullscreen, uses the whole screen
//    ./build/spectre --window 800x600     # window (desktop / testing)
//    ./build/spectre --frames 2 --out shot.ppm     # render and save, no display
//
//  See run.sh for the phone entry point and the README for the flags.
// -----------------------------------------------------------------------------
#include <SDL.h>

#include "imgui.h"

#include "mlbb_gui/Menu.h"
#include "mlbb_gui/Theme.h"

#include "render/SoftRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// ----------------------------------------------------------------- options ---
struct Options {
    int   winW = 0, winH = 0;      // 0 = fullscreen desktop
    bool  fullscreen = true;
    float density = 0.0f;          // 0 = pick a density that fits the menu
    int   menuW = 1120, menuH = 680;
    int   layout = -1;             // -1 auto, 0 wide, 1 compact
    const char* font = nullptr;
    const char* bold = nullptr;
    const char* out = nullptr;     // screenshot target (PPM)
    int   frames = 0;              // >0: render N frames and exit
    int   tab = -1;
    bool  populate = false;
    bool  folded = false;
    float clickX = -1.0f, clickY = -1.0f;   // synthetic click, physical pixels
    int   fps = 60;
    float scaleHint = 1.0f;        // multiplies the auto density (--ui-scale)
    bool  listDrivers = false;
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
        "spectre - the mlbb cheat-menu UI in a window (no cheat logic inside)\n"
        "\n"
        "usage: spectre [options]\n"
        "\n"
        "  --window WxH      window size, e.g. 1280x720 (default: fullscreen)\n"
        "  --windowed        same as --window 1280x720\n"
        "  --density N       framebuffer scale; 0/omitted = fit the menu to the screen\n"
        "  --filter F        bilinear (default) or nearest texture filtering\n"
        "  --ui-scale N      multiply the automatic density (0.5 .. 3)\n"
        "  --menu WxH        logical menu size (default 1120x680, phone 440)\n"
        "  --compact         force the phone layout (single scrollable column)\n"
        "  --wide            force the desktop layout (two columns)\n"
        "  --font FILE       TTF to draw with (default: assets/fonts/DejaVuSansMono.ttf)\n"
        "  --bold FILE       bold TTF (default: assets/fonts/DejaVuSansMono-Bold.ttf)\n"
        "  --tab N           open tab 0..4 (ESP/AIM/VISUAL/MISC/CONFIG)\n"
        "  --populate        turn some toggles on so both widget states are visible\n"
        "  --folded          start with the window folded to its title strip\n"
        "  --frames N        render N frames, save --out and exit (headless capture)\n"
        "  --out FILE.ppm    screenshot target\n"
        "  --click X,Y       synthetic click at physical pixels (for --frames shots)\n"
        "  --fps N           frame rate cap, 0 = uncapped (default 60)\n"
        "  --drivers         list SDL video drivers and exit\n"
        "  -h, --help        this text\n"
        "\n"
        "keys: F1..F5 tabs, F12 screenshot, Esc quit, arrows/mouse wheel scroll\n");
}

// ------------------------------------------------------------------ helpers --
std::string ExeDir(const char* argv0)
{
    std::string p = argv0 ? argv0 : "";
    const size_t slash = p.find_last_of('/');
    return (slash == std::string::npos) ? std::string(".") : p.substr(0, slash);
}

bool FileExists(const char* path)
{
    if (!path) return false;
    if (FILE* f = std::fopen(path, "rb")) { std::fclose(f); return true; }
    return false;
}

// Looks for a font: the command line, then the repository, then the system.
const char* PickFont(const char* fromCli, const std::string& exeDir, const char* const* candidates, int count)
{
    if (fromCli) return fromCli;

    std::vector<std::string> paths;
    paths.push_back(exeDir + "/assets/fonts/");        // build/../.. handled below
    paths.push_back(exeDir + "/../assets/fonts/");
    paths.push_back(exeDir + "/../../assets/fonts/");
    paths.push_back("assets/fonts/");
    paths.push_back("../assets/fonts/");
    paths.push_back("");

    static std::vector<std::string> storage;   // keep the strings alive
    for (const std::string& dir : paths) {
        for (int i = 0; i < count; ++i) {
            storage.push_back(dir + candidates[i]);
            if (FileExists(storage.back().c_str())) return storage.back().c_str();
            storage.pop_back();
        }
    }
    return nullptr;
}

// A dark green backdrop with a soft glow, generated once: the per-frame cost is
// then a single memcpy instead of re-rasterizing a full-screen gradient.
void BuildBackdrop(std::vector<unsigned char>& px, int w, int h)
{
    px.resize((size_t)w * (size_t)h * 4u);
    for (int y = 0; y < h; ++y) {
        const float fy = (h > 1) ? (float)y / (float)(h - 1) : 0.0f;
        unsigned char* row = &px[(size_t)y * (size_t)w * 4u];
        for (int x = 0; x < w; ++x) {
            const float fx = (w > 1) ? (float)x / (float)(w - 1) : 0.0f;

            float r = 10.0f + 10.0f * (1.0f - fy);
            float g = 26.0f + 26.0f * (1.0f - fy);
            float b = 13.0f + 10.0f * (1.0f - fy);

            const float dx = (fx - 0.2f) * 1.7f;
            const float dy = (fy - 0.02f);
            const float glow = 1.0f / (1.0f + 26.0f * (dx * dx + dy * dy));
            g += 62.0f * glow;
            b += 24.0f * glow;
            r += 10.0f * glow;

            const float vx = (fx - 0.5f) * 1.35f, vy = (fy - 0.5f) * 1.15f;
            const float vig = 1.0f - 0.45f * std::min(1.0f, vx * vx + vy * vy);

            unsigned char* p = row + (size_t)x * 4u;
            p[0] = (unsigned char)std::min(255.0f, r * vig);
            p[1] = (unsigned char)std::min(255.0f, g * vig);
            p[2] = (unsigned char)std::min(255.0f, b * vig);
            p[3] = 255;
        }
    }
}

// ImGui 1.83 still uses the legacy input API (KeyMap + KeysDown).
void SetupKeyMap(ImGuiIO& io)
{
    io.KeyMap[ImGuiKey_Tab]        = SDL_SCANCODE_TAB;
    io.KeyMap[ImGuiKey_LeftArrow]  = SDL_SCANCODE_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = SDL_SCANCODE_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow]    = SDL_SCANCODE_UP;
    io.KeyMap[ImGuiKey_DownArrow]  = SDL_SCANCODE_DOWN;
    io.KeyMap[ImGuiKey_PageUp]     = SDL_SCANCODE_PAGEUP;
    io.KeyMap[ImGuiKey_PageDown]   = SDL_SCANCODE_PAGEDOWN;
    io.KeyMap[ImGuiKey_Home]       = SDL_SCANCODE_HOME;
    io.KeyMap[ImGuiKey_End]        = SDL_SCANCODE_END;
    io.KeyMap[ImGuiKey_Insert]     = SDL_SCANCODE_INSERT;
    io.KeyMap[ImGuiKey_Delete]     = SDL_SCANCODE_DELETE;
    io.KeyMap[ImGuiKey_Backspace]  = SDL_SCANCODE_BACKSPACE;
    io.KeyMap[ImGuiKey_Space]      = SDL_SCANCODE_SPACE;
    io.KeyMap[ImGuiKey_Enter]      = SDL_SCANCODE_RETURN;
    io.KeyMap[ImGuiKey_Escape]     = SDL_SCANCODE_ESCAPE;
    io.KeyMap[ImGuiKey_A] = SDL_SCANCODE_A;
    io.KeyMap[ImGuiKey_C] = SDL_SCANCODE_C;
    io.KeyMap[ImGuiKey_V] = SDL_SCANCODE_V;
    io.KeyMap[ImGuiKey_X] = SDL_SCANCODE_X;
    io.KeyMap[ImGuiKey_Y] = SDL_SCANCODE_Y;
    io.KeyMap[ImGuiKey_Z] = SDL_SCANCODE_Z;
}

void Populate(mlbb::Features& st)
{
    st.espBox = true;
    st.espSkeleton = true;
    st.espHealthBar = true;
    st.espDistance = true;
    st.espName = true;
    st.espLine = true;
    st.aimEnabled = true;
    st.aimPrediction = true;
    st.visSkillCd = true;
    st.visSpellCd = true;
    st.miscAntiBan = true;
    st.miscRetri = true;
}

int ScreenshotNumber = 0;

}  // namespace

int main(int argc, char** argv)
{
    Options opt;
    if (HasFlag(argc, argv, "-h") || HasFlag(argc, argv, "--help")) { PrintUsage(); return 0; }
    opt.listDrivers = HasFlag(argc, argv, "--drivers");

    if (const char* v = ArgValue(argc, argv, "--window")) { std::sscanf(v, "%dx%d", &opt.winW, &opt.winH); opt.fullscreen = false; }
    if (HasFlag(argc, argv, "--windowed")) { opt.fullscreen = false; if (!opt.winW) { opt.winW = 1280; opt.winH = 720; } }
    if (const char* v = ArgValue(argc, argv, "--density")) opt.density = (float)std::atof(v);
    if (const char* v = ArgValue(argc, argv, "--ui-scale")) opt.scaleHint = (float)std::atof(v);
    if (const char* v = ArgValue(argc, argv, "--menu")) std::sscanf(v, "%dx%d", &opt.menuW, &opt.menuH);
    if (const char* v = ArgValue(argc, argv, "--tab")) opt.tab = std::atoi(v);
    if (const char* v = ArgValue(argc, argv, "--fps")) opt.fps = std::atoi(v);
    if (HasFlag(argc, argv, "--compact")) opt.layout = 1;
    if (HasFlag(argc, argv, "--wide")) opt.layout = 0;
    if (HasFlag(argc, argv, "--populate")) opt.populate = true;
    if (HasFlag(argc, argv, "--folded")) opt.folded = true;

    opt.font = ArgValue(argc, argv, "--font");
    opt.bold = ArgValue(argc, argv, "--bold");
    opt.out  = ArgValue(argc, argv, "--out");
    if (const char* v = ArgValue(argc, argv, "--frames")) { opt.frames = std::atoi(v); if (opt.frames < 1) opt.frames = 1; }
    if (const char* v = ArgValue(argc, argv, "--click")) std::sscanf(v, "%f,%f", &opt.clickX, &opt.clickY);
    if (opt.out && opt.frames == 0) opt.frames = 3;      // --out implies a capture

    // Headless capture: no display needed, the dummy driver still gives us a
    // window surface to blit into (and the framebuffer to save).
    if (opt.frames > 0 && !std::getenv("SDL_VIDEODRIVER") &&
        !std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY"))
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    if (opt.listDrivers) {
        for (int i = 0; i < SDL_GetNumVideoDrivers(); ++i) std::printf("%s\n", SDL_GetVideoDriver(i));
        SDL_Quit();
        return 0;
    }

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");     // taps -> mouse, for ImGui
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_ShowCursor(opt.frames > 0 ? SDL_DISABLE : SDL_ENABLE);

    // ---- window ------------------------------------------------------------
    SDL_Window* window = nullptr;
    if (opt.fullscreen && opt.winW == 0) {
        window = SDL_CreateWindow("spectre", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        const int w = opt.winW > 0 ? opt.winW : 1280;
        const int h = opt.winH > 0 ? opt.winH : 720;
        window = SDL_CreateWindow("spectre", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    }
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int winW = 0, winH = 0;
    SDL_GetWindowSize(window, &winW, &winH);
    if (winW < 64 || winH < 64) { winW = 1080; winH = 2400; }

    // ---- layout decision ---------------------------------------------------
    // Phones are usually taller than wide; the menu then becomes a single
    // column that fills the screen. `--wide`/`--compact` override this.
    const bool compact = (opt.layout == 1) || (opt.layout < 0 && winH > winW);
    const int  menuW = compact ? std::min(opt.menuW, 560) : opt.menuW;
    const int  menuH = opt.menuH;

    // Density: how many physical pixels one logical menu pixel gets. The menu
    // should nearly fill the screen, which is what makes it readable on a phone.
    float density = opt.density;
    if (density <= 0.0f) {
        const float margin = compact ? 8.0f : 28.0f;
        const float byW = (float)winW / ((float)menuW + margin * 2.0f);
        const float byH = (float)winH / ((float)menuH + margin * 2.0f);
        density = compact ? byW : std::min(byW, byH);
        density *= (opt.scaleHint > 0.0f ? opt.scaleHint : 1.0f);
    }
    density = std::max(0.35f, std::min(density, 6.0f));

    int canvasW = (int)std::lround((double)winW / density);
    int canvasH = (int)std::lround((double)winH / density);
    canvasW = std::max(canvasW, 200);
    canvasH = std::max(canvasH, 200);

    mlbb::render::SoftRenderer renderer;
    renderer.Init(canvasW, canvasH, density);
    // The atlas is rasterized at exactly `density`, so both filters look the
    // same on a phone; bilinear is the safer default when the density and the
    // framebuffer scale drift apart (window resize, --density on the desktop).
    const char* filter = ArgValue(argc, argv, "--filter");
    renderer.SetFilter(filter && std::strcmp(filter, "nearest") == 0
                           ? mlbb::render::SoftRenderer::FilterNearest
                           : mlbb::render::SoftRenderer::FilterBilinear);

    std::vector<unsigned char> backdrop;
    BuildBackdrop(backdrop, renderer.FramebufferWidth(), renderer.FramebufferHeight());

    // ---- ImGui -------------------------------------------------------------
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
    static const char* const kRegular[] = { "DejaVuSansMono.ttf", "JetBrainsMono-Regular.ttf", "RobotoMono-Regular.ttf" };
    static const char* const kBoldList[] = { "DejaVuSansMono-Bold.ttf", "JetBrainsMono-Bold.ttf", "RobotoMono-Bold.ttf" };

    mlbb::Config cfg;
    cfg.width  = (float)menuW;
    cfg.height = (float)menuH;
    cfg.pixelDensity = density;
    cfg.compact = compact;
    cfg.hint = compact ? "tap to toggle \xc2\xb7 drag to scroll" : "drag the strip to move the window";
    cfg.fontRegular = PickFont(opt.font, exeDir, kRegular, (int)(sizeof(kRegular) / sizeof(kRegular[0])));
    cfg.fontBold    = PickFont(opt.bold, exeDir, kBoldList, (int)(sizeof(kBoldList) / sizeof(kBoldList[0])));

    mlbb::Features st;
    st.folded = opt.folded;
    if (opt.populate) Populate(st);
    if (opt.tab >= 0) st.tab = opt.tab;

    mlbb::Fonts fonts = mlbb::LoadFonts(cfg);

    unsigned char* texels = nullptr;
    int texW = 0, texH = 0;
    io.Fonts->GetTexDataAsRGBA32(&texels, &texW, &texH);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);

    if (!cfg.fontRegular || !cfg.fontBold) {
        std::fprintf(stderr, "warning: no TTF found (looked next to the binary and in assets/fonts);"
                             " falling back to the built-in font - box drawing and block glyphs"
                             " (the ASCII banner) will not render.\n");
    }

    std::printf("spectre: window %dx%d, canvas %dx%d, density %.2f, %s layout%s%s\n",
                winW, winH, canvasW, canvasH, density, compact ? "compact/phone" : "wide/desktop",
                cfg.fontRegular ? ", font " : "", cfg.fontRegular ? cfg.fontRegular : "");

    // ---- loop --------------------------------------------------------------
    bool running = true;
    int  rendered = 0;
    Uint32 lastCounter = SDL_GetTicks();
    int    framesSince = 0;
    float  fpsSmoothed = 60.0f;

    while (running) {
        const Uint32 frameStart = SDL_GetTicks();

        // ---- events
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                        e.window.event == SDL_WINDOWEVENT_RESIZED) {
                        int nw = 0, nh = 0;
                        SDL_GetWindowSize(window, &nw, &nh);
                        if (nw > 63 && nh > 63 && (nw != winW || nh != winH)) {
                            winW = nw; winH = nh;
                            // The density (and therefore the font atlas) stays;
                            // a resize just means more or less logical space.
                            canvasW = std::max(200, (int)std::lround((double)winW / density));
                            canvasH = std::max(200, (int)std::lround((double)winH / density));
                            renderer.Init(canvasW, canvasH, density);
                            BuildBackdrop(backdrop, renderer.FramebufferWidth(), renderer.FramebufferHeight());
                            io.DisplaySize = ImVec2((float)canvasW, (float)canvasH);
                            std::printf("spectre: resized to %dx%d (canvas %dx%d)\n", winW, winH, canvasW, canvasH);
                        }
                    }
                    break;
                case SDL_MOUSEMOTION:
                    io.MousePos = ImVec2((float)e.motion.x / density, (float)e.motion.y / density);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_LEFT)   io.MouseDown[0] = true;
                    if (e.button.button == SDL_BUTTON_RIGHT)  io.MouseDown[1] = true;
                    if (e.button.button == SDL_BUTTON_MIDDLE) io.MouseDown[2] = true;
                    io.MousePos = ImVec2((float)e.button.x / density, (float)e.button.y / density);
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (e.button.button == SDL_BUTTON_LEFT)   io.MouseDown[0] = false;
                    if (e.button.button == SDL_BUTTON_RIGHT)  io.MouseDown[1] = false;
                    if (e.button.button == SDL_BUTTON_MIDDLE) io.MouseDown[2] = false;
                    io.MousePos = ImVec2((float)e.button.x / density, (float)e.button.y / density);
                    break;
                case SDL_MOUSEWHEEL:
                    io.MouseWheel = (float)e.wheel.y;
                    break;
                case SDL_FINGERDOWN:
                case SDL_FINGERMOTION:
                    // Some builds do not synthesise mouse events for touches.
                    io.MousePos = ImVec2(e.tfinger.x * (float)canvasW, e.tfinger.y * (float)canvasH);
                    if (!io.MouseDown[0]) io.MouseDown[0] = true;
                    break;
                case SDL_FINGERUP:
                    io.MouseDown[0] = false;
                    break;
                case SDL_TEXTINPUT:
                    io.AddInputCharactersUTF8(e.text.text);
                    break;
                case SDL_KEYDOWN: {
                    const SDL_Keycode k = e.key.keysym.sym;
                    if (k == SDLK_ESCAPE) running = false;
                    if (k == SDLK_F12) {                            // screenshot
                        char path[64];
                        std::snprintf(path, sizeof(path), "spectre-%d.ppm", ++ScreenshotNumber);
                        renderer.SavePPM(path, 1);
                        std::printf("screenshot: %s\n", path);
                    }
                    if (k >= SDLK_F1 && k <= SDLK_F5) st.tab = (int)(k - SDLK_F1);
                    if (k == SDLK_F11) {
                        const Uint32 flags = SDL_GetWindowFlags(window);
                        SDL_SetWindowFullscreen(window, (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                    io.KeysDown[e.key.keysym.scancode] = true;
                    io.KeyCtrl  = (e.key.keysym.mod & KMOD_CTRL)  != 0;
                    io.KeyShift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                    io.KeyAlt   = (e.key.keysym.mod & KMOD_ALT)   != 0;
                    io.KeySuper = (e.key.keysym.mod & KMOD_GUI)   != 0;
                    break;
                }
                case SDL_KEYUP: {
                    io.KeysDown[e.key.keysym.scancode] = false;
                    io.KeyCtrl  = (e.key.keysym.mod & KMOD_CTRL)  != 0;
                    io.KeyShift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                    io.KeyAlt   = (e.key.keysym.mod & KMOD_ALT)   != 0;
                    io.KeySuper = (e.key.keysym.mod & KMOD_GUI)   != 0;
                    break;
                }
                default:
                    break;
            }
        }

        // Synthetic click for headless captures: the press happens two frames
        // before the shot and the release one frame later, so widgets that act
        // on release (toggles, buttons, tabs) end up in the "clicked" state.
        if (opt.clickX >= 0.0f) {
            const int pressFrame = std::max(1, opt.frames - 2);
            io.MousePos = ImVec2(opt.clickX / density, opt.clickY / density);
            io.MouseDown[0] = (rendered == pressFrame);
        }

        // ---- frame
        ImGui::NewFrame();

        mlbb::DrawMenu(cfg, fonts, st);

        ImGui::Render();

        // The phone window covers the canvas, so the backdrop would be copied
        // only to be painted over - skip it and save ~10 MB of memcpy per frame.
        const bool backdropHidden = compact && !st.folded;
        if (!backdropHidden) std::memcpy(renderer.Data(), backdrop.data(), backdrop.size());
        renderer.Render(ImGui::GetDrawData(), texels, texW, texH);

        if (opt.frames > 0) io.MouseDown[0] = false;

        // ---- present
        int blitW = renderer.FramebufferWidth();
        int blitH = renderer.FramebufferHeight();
        SDL_Surface* src = SDL_CreateRGBSurfaceWithFormatFrom(renderer.Data(), blitW, blitH, 32,
                                                              renderer.Pitch(), SDL_PIXELFORMAT_RGBA32);
        if (src) {
            SDL_Surface* dst = SDL_GetWindowSurface(window);
            if (dst) {
                if (dst->w != blitW || dst->h != blitH)
                    SDL_BlitScaled(src, nullptr, dst, nullptr);
                else
                    SDL_BlitSurface(src, nullptr, dst, nullptr);
                SDL_UpdateWindowSurface(window);
            }
            SDL_FreeSurface(src);
        }

        // ---- frame pacing
        ++rendered;
        if (opt.frames > 0 && rendered >= opt.frames) break;

        const Uint32 spent = SDL_GetTicks() - frameStart;
        ++framesSince;
        const Uint32 now = SDL_GetTicks();
        if (now - lastCounter >= 1000) {
            fpsSmoothed = (float)framesSince * 1000.0f / (float)(now - lastCounter);
            framesSince = 0;
            lastCounter = now;
        }
        if (opt.fps > 0) {
            const Uint32 budget = (Uint32)(1000 / opt.fps);
            if (spent < budget) SDL_Delay(budget - spent);
        }
    }

    if (opt.frames == 0)
        std::printf("spectre: closed after %d frames (%.1f fps average)\n", rendered, fpsSmoothed);

    if (opt.out) {
        if (renderer.SavePPM(opt.out, 1))
            std::printf("wrote %s (%dx%d, density %.2f, %s layout)\n", opt.out,
                        renderer.FramebufferWidth(), renderer.FramebufferHeight(), density,
                        compact ? "compact" : "wide");
        else
            std::fprintf(stderr, "error: cannot write '%s'\n", opt.out);
    }

    ImGui::DestroyContext();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
