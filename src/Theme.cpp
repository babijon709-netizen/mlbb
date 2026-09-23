#include "mlbb_gui/Theme.h"

namespace mlbb {
namespace theme {

// -------------------------------------------------------------------- art ----
// Block-letter banner (figlet "ansi_shadow" style). The art font size is fitted
// to the column automatically, so the banner can be replaced by any art of a
// similar aspect ratio.
const char* const kBanner[] = {
    "███████╗██████╗ ███████╗ ██████╗████████╗██████╗ ███████╗",
    "██╔════╝██╔══██╗██╔════╝██╔════╝╚══██╔══╝██╔══██╗██╔════╝",
    "███████╗██████╔╝█████╗  ██║        ██║   ██████╔╝█████╗  ",
    "╚════██║██╔═══╝ ██╔══╝  ██║        ██║   ██╔══██╗██╔══╝  ",
    "███████║██║     ███████╗╚██████╗   ██║   ██║  ██║███████╗",
    "╚══════╝╚═╝     ╚══════╝ ╚═════╝   ╚═╝   ╚═╝  ╚═╝╚══════╝",
    nullptr,
};

// Small "spectre" mark - an eye/lens built from block elements. Sits under the
// device section and keeps the pink accent the banner uses.
const char* const kEmblem[] = {
    "           ▄▄██████████████▄▄            ",
    "       ▄██████▀▀░░░░░░░░▀▀██████▄        ",
    "   ██████▀  ░░▒▒▓▓▓▓▓▓▓▓▒▒░░  ▀██████    ",
    " ████▀   ░▒▒▓▓▓███████████▓▓▓▒▒░   ▀████ ",
    "███     ░▒▒▓▓▓█████████████▓▓▓▒▒░     ███",
    " ████▄   ░▒▒▓▓▓███████████▓▓▓▒▒░   ▄████ ",
    "   ██████▄  ░░▒▒▓▓▓▓▓▓▓▓▒▒░░  ▄██████    ",
    "       ▀██████▄▄░░░░░░░░▄▄██████▀        ",
    "           ▀▀██████████████▀▀            ",
    nullptr,
};

// ---------------------------------------------------------------------------
void ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = metrics::kRadiusWin;
    s.WindowBorderSize  = 1.0f;
    s.WindowPadding     = ImVec2(0.0f, 0.0f);   // the menu handles its own padding
    s.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    s.ChildRounding     = metrics::kRadiusPanel;
    s.FrameRounding     = metrics::kRadiusSmall;
    s.PopupRounding     = metrics::kRadiusPanel;
    s.GrabRounding      = metrics::kRadiusSmall;
    s.ScrollbarRounding = metrics::kRadiusPanel;
    s.ItemSpacing       = ImVec2(8.0f, 6.0f);
    s.FramePadding      = ImVec2(8.0f, 4.0f);
    s.ScrollbarSize     = 8.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = ImVec4(0.031f, 0.067f, 0.039f, 0.957f);
    c[ImGuiCol_Border]          = ImVec4(0.376f, 0.612f, 0.388f, 0.141f);
    c[ImGuiCol_Text]            = ImVec4(0.804f, 0.890f, 0.804f, 1.00f);
    c[ImGuiCol_TextDisabled]    = ImVec4(0.329f, 0.455f, 0.329f, 1.00f);

    // Widgets ImGui still draws itself (text inputs, combo boxes, popups).
    c[ImGuiCol_FrameBg]         = ImVec4(1.000f, 1.000f, 1.000f, 0.043f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(1.000f, 1.000f, 1.000f, 0.075f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(1.000f, 1.000f, 1.000f, 0.098f);
    c[ImGuiCol_PopupBg]         = ImVec4(0.047f, 0.094f, 0.055f, 0.988f);
    c[ImGuiCol_Header]          = ImVec4(0.439f, 0.859f, 0.463f, 0.180f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.439f, 0.859f, 0.463f, 0.282f);
    c[ImGuiCol_HeaderActive]    = ImVec4(0.439f, 0.859f, 0.463f, 0.380f);
    c[ImGuiCol_Button]          = ImVec4(1.000f, 1.000f, 1.000f, 0.055f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.439f, 0.859f, 0.463f, 0.220f);
    c[ImGuiCol_ButtonActive]    = ImVec4(0.439f, 0.859f, 0.463f, 0.320f);
    c[ImGuiCol_CheckMark]       = ImVec4(0.639f, 1.000f, 0.659f, 1.00f);
    c[ImGuiCol_SliderGrab]      = ImVec4(0.439f, 0.859f, 0.463f, 1.00f);
    c[ImGuiCol_SliderGrabActive]= ImVec4(0.639f, 1.000f, 0.659f, 1.00f);
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(1.000f, 1.000f, 1.000f, 0.100f);
    c[ImGuiCol_TextSelectedBg]  = ImVec4(0.439f, 0.859f, 0.463f, 0.280f);
    c[ImGuiCol_NavHighlight]    = ImVec4(0.439f, 0.859f, 0.463f, 0.000f);

    // Windows have no title bar of their own - the menu draws its own strip.
    // Only the title bar starts a window drag so it never fights the widgets.
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigWindowsMoveFromTitleBarOnly = true;
}

}  // namespace theme
}  // namespace mlbb
