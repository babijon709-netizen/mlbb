// -----------------------------------------------------------------------------
//  Menu.h - public interface of the mlbb ImGui menu.
//
//  Nothing in this project touches the game process: the menu is pure UI. All
//  the toggles/sliders below just keep their own state so the widgets have
//  something to show - wire them up to the cheat logic later if you want.
// -----------------------------------------------------------------------------
#pragma once

#include "imgui.h"

namespace mlbb {

// ----------------------------------------------------------------- fonts ----
// Every entry keeps the font pointer together with the size it is drawn at.
// Sizes are those passed to ImFontAtlas::AddFontFromFileTTF().
struct Fonts {
    ImFont* small   = nullptr;  float smallSize = 12.5f;  // labels, tags, captions
    ImFont* body    = nullptr;  float bodySize  = 15.0f;  // values
    ImFont* bold    = nullptr;  float boldSize  = 15.0f;  // headings, active items
    ImFont* title   = nullptr;  float titleSize = 17.0f;  // window title, greeting
    ImFont* art     = nullptr;  float artSize   = 11.0f;  // ASCII banner (auto-fitted)
};

// ---------------------------------------------------------------- config ----
struct Config {
    // --- window ---------------------------------------------------------
    const char* windowTitle = "mlbb // spectre";
    float width  = 1120.0f;
    float height = 680.0f;
    bool  resizable = false;   // lets the user drag the bottom-right corner

    // --- phone / hidpi ---------------------------------------------------
    // Fonts are rasterized at `size * pixelDensity` while everything keeps
    // being laid out in the logical units above. The renderer draws the menu
    // with the same density, so on a phone (density ~2) the menu comes out
    // physically big *and* crisp instead of being stretched after the fact.
    // 1.0 = desktop, let the app pick something like 2.0 on a phone screen.
    float pixelDensity = 1.0f;

    // One column instead of sidebar + dashboard, for narrow (portrait) phone
    // screens. The app turns this on automatically when the screen is taller
    // than it is wide; the content is then scrolled by dragging.
    bool  compact = false;

    // --- device rows -----------------------------------------------------
    // The DEVICE section is decoration: nothing is queried by the menu itself.
    // The overlay build fills these with the real phone (ro.product.model and
    // friends), the SDL build keeps the defaults.
    const char* deviceModel  = "iPhone 15,2";
    const char* deviceSystem = "iOS 17.4 \xc2\xb7 rootless";
    const char* deviceRender = "Metal \xc2\xb7 ImGui 1.83";

    // --- texts ----------------------------------------------------------
    const char* brand    = "spectre";                 // shown next to the icon
    const char* greeting = "Hey,";
    const char* account  = "Vanil";
    const char* footer   = "Bonsoir! Elliot.";
    const char* build    = "0x2F1A";
    const char* hint     = "drag the strip to move the window";

    // --- fonts ----------------------------------------------------------
    // Paths to TTF files. When a path is null the built-in ImGui font is used
    // (the layout still works, it just looks less like a terminal).
    const char* fontRegular = nullptr;   // body / labels
    const char* fontBold    = nullptr;   // headings
    const char* fontSmall   = nullptr;   // optional: falls back to fontRegular
};

// -------------------------------------------------------------- features ----
// Local UI state of every widget in the menu. Purely cosmetic for now.
struct Features {
    // --- window / layout -------------------------------------------------
    bool windowOpen = true;   // false hides the whole menu
    bool folded     = false;  // only the tab strip is visible
    bool unlocked   = false;  // allows resizing the window with the mouse
    int  tab        = 0;      // active tab (0 = ESP)

    // --- ESP ------------------------------------------------------------
    bool  espBox        = false;
    bool  espSkeleton   = false;
    bool  espLine       = false;
    bool  espHealthBar  = false;
    bool  espName       = false;
    bool  espDistance   = false;
    bool  espIcon       = false;
    bool  espTeamCheck  = true;
    int   espBoxStyle   = 0;    // Rect / Corner / Circle
    int   espIndicator  = 0;    // Bar / Text / Both

    // --- AIM ------------------------------------------------------------
    bool  aimEnabled    = false;
    bool  aimPrediction = false;
    bool  aimVisible    = false;
    bool  aimFovCircle  = true;
    float aimFov        = 120.0f;
    float aimSmooth     = 4.0f;
    float aimOffsetY    = 0.0f;
    int   aimPriority   = 0;    // Nearest / Lowest HP / Crosshair
    int   aimHitbox     = 0;    // Head / Chest / Pelvis

    // --- VISUAL ---------------------------------------------------------
    bool  visMapHack    = false;
    bool  visSkillCd    = false;
    bool  visSpellCd    = false;
    bool  visDroneView  = false;
    bool  visZoomOut    = false;
    float visCamera     = 45.0f;
    int   visFpsCap     = 2;    // 60 / 90 / 120 / 144 / 240

    // --- MISC -----------------------------------------------------------
    bool  miscAntiBan   = false;
    bool  miscBypass    = false;
    bool  miscRetri     = false;
    bool  miscNoGrass   = false;
    bool  miscRecall    = false;
    float miscDelay     = 0.35f;
    int   miscLanguage  = 0;    // EN / RU / ES / PT

    // --- CONFIG ---------------------------------------------------------
    bool  cfgAutoSave   = true;
    bool  cfgLoadOnStart = true;
    char  cfgName[64]   = "default";
    int   cfgProfile    = 0;    // legit / default / rage
};

// ------------------------------------------------------------------ api -----
// Creates the fonts described by `cfg` and returns them. Call once, after
// ImGui::CreateContext() and before the first frame.
Fonts LoadFonts(const Config& cfg);

// Appends a line to the LOG section of the menu (printf style). Handy for
// reporting real game events once the cheat logic is hooked up.
void PushLog(const char* fmt, ...);

// Draws the window. Call every frame between ImGui::NewFrame() and
// ImGui::Render(). The window can be dragged by its tab strip, folded and
// closed (set cfg/Features::windowOpen back to true to show it again).
void DrawMenu(Config& cfg, Fonts& fonts, Features& features);

}  // namespace mlbb
