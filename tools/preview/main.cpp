// -----------------------------------------------------------------------------
//  tools/preview - renders the menu into an image, no GPU or window needed.
//
//  Usage:
//    mlbb_preview [--out shot.ppm] [--width 1120] [--height 680] [--scale 2]
//                 [--display 1400x820]      (screen size, menu is centred on it)
//                 [--font regular.ttf] [--bold bold.ttf] [--bg backdrop.ppm]
//                 [--tab 0..4] [--populate] [--mouse x,y] [--click x,y]
//                 [--uptime minutes] [--folded]
//
//  The result is a PPM (P6); convert it with ImageMagick:
//    convert shot.ppm shot.png
// -----------------------------------------------------------------------------
#include "imgui.h"

#include "mlbb_gui/Menu.h"
#include "mlbb_gui/Theme.h"

#include "SoftRenderer.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

namespace {

const char* ArgValue(int argc, char** argv, const char* key, const char* fallback)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], key) == 0) return argv[i + 1];
    return fallback;
}

bool HasFlag(int argc, char** argv, const char* key)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], key) == 0) return true;
    return false;
}

// Turns a few features on so the screenshot shows the widgets in both states.
void Populate(mlbb::Features& st, int tab)
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
    st.tab = tab;
}

}  // namespace

int main(int argc, char** argv)
{
    const char* outPath   = ArgValue(argc, argv, "--out", "preview.ppm");
    const char* bgPath    = ArgValue(argc, argv, "--bg", nullptr);
    const char* fontPath  = ArgValue(argc, argv, "--font", nullptr);
    const char* boldPath  = ArgValue(argc, argv, "--bold", nullptr);
    const int   width     = std::atoi(ArgValue(argc, argv, "--width", "1120"));
    const int   height    = std::atoi(ArgValue(argc, argv, "--height", "680"));
    const int   scale     = std::atoi(ArgValue(argc, argv, "--scale", "2"));
    const int   frames    = std::atoi(ArgValue(argc, argv, "--frames", "3"));
    const int   tab       = std::atoi(ArgValue(argc, argv, "--tab", "0"));
    const char* mouseArg  = ArgValue(argc, argv, "--mouse", nullptr);
    const char* dispArg   = ArgValue(argc, argv, "--display", nullptr);
    const char* uptimeArg = ArgValue(argc, argv, "--uptime", nullptr);   // minutes shown as UPTIME
    const char* clickArg  = ArgValue(argc, argv, "--click", nullptr);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    int dispW = width, dispH = height;
    if (dispArg) std::sscanf(dispArg, "%dx%d", &dispW, &dispH);
    io.DisplaySize = ImVec2((float)dispW, (float)dispH);
    io.DeltaTime = 1.0f / 60.0f;

    mlbb::theme::ApplyTheme();

    mlbb::Config cfg;
    cfg.width  = (float)width;
    cfg.height = (float)height;
    if (fontPath) cfg.fontRegular = fontPath;
    if (boldPath) cfg.fontBold = boldPath;

    mlbb::Features st;
    if (HasFlag(argc, argv, "--folded")) st.folded = true;
    if (HasFlag(argc, argv, "--populate")) Populate(st, tab);
    else st.tab = tab;

    // Optional: report a session that has been running for a while.
    if (uptimeArg) io.DeltaTime = (float)(std::atof(uptimeArg) * 60.0) / (float)(frames > 0 ? frames : 1);

    mlbb::Fonts fonts = mlbb::LoadFonts(cfg);

    io.Fonts->Build();
    unsigned char* texels = nullptr;
    int texW = 0, texH = 0;
    io.Fonts->GetTexDataAsRGBA32(&texels, &texW, &texH);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);

    mlbb_preview::SoftRenderer renderer;
    renderer.Init(dispW, dispH, scale);
    if (bgPath) {
        if (!renderer.LoadBackgroundPPM(bgPath))
            std::fprintf(stderr, "warning: could not load background '%s'\n", bgPath);
    } else {
        renderer.Clear(mlbb_preview::Color4{ 12, 22, 14, 255 });
    }

    float mx = -1.0f, my = -1.0f;
    if (mouseArg && std::sscanf(mouseArg, "%f,%f", &mx, &my) != 2) { mx = my = -1.0f; }

    for (int i = 0; i < frames; ++i) {
        // One synthetic click: press on frame 1, release on frame 2.
        if (clickArg && i == 1) {
            float cx = 0.0f, cy = 0.0f;
            if (std::sscanf(clickArg, "%f,%f", &cx, &cy) == 2) { mx = cx; my = cy; io.MouseDown[0] = true; }
        } else if (clickArg && i >= 2) {
            io.MouseDown[0] = false;
        } else {
            io.MouseDown[0] = false;
        }
        io.MousePos = ImVec2(mx, my);

        ImGui::NewFrame();
        mlbb::DrawMenu(cfg, fonts, st);
        ImGui::Render();
    }

    renderer.Render(ImGui::GetDrawData(), texels, texW, texH);

    if (!renderer.SavePPM(outPath)) {
        std::fprintf(stderr, "error: cannot write '%s'\n", outPath);
        return 1;
    }
    std::printf("wrote %s (menu %dx%d on a %dx%d screen, supersample x%d)\n",
                outPath, width, height, dispW, dispH, scale);

    ImGui::DestroyContext();
    return 0;
}
