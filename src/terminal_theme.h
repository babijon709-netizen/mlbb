#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>
#include "imgui.h"

namespace CyberTheme {
    // 1:1 color palette from Terminal.jpeg reference
    constexpr ImU32 ColBgDark       = IM_COL32(12, 16, 20, 245);    // Terminal background
    constexpr ImU32 ColHeader       = IM_COL32(20, 27, 36, 255);    // Titlebar
    constexpr ImU32 ColTabActive    = IM_COL32(28, 38, 52, 255);    // Active tab
    constexpr ImU32 ColTabHover     = IM_COL32(36, 48, 66, 255);
    constexpr ImU32 ColBorder       = IM_COL32(42, 56, 76, 255);    // Box border cyan/slate
    constexpr ImU32 ColBorderGlow   = IM_COL32(87, 199, 255, 180);  // Cyan border highlight
    
    constexpr ImU32 ColLime         = IM_COL32(90, 247, 142, 255);  // #5af78e (PowerShell green)
    constexpr ImU32 ColPink         = IM_COL32(255, 110, 151, 255); // #ff6e97 (ASCII art pink)
    constexpr ImU32 ColCyan         = IM_COL32(87, 199, 255, 255);  // #57c7ff (Headers cyan)
    constexpr ImU32 ColYellow       = IM_COL32(241, 250, 140, 255); // #f1fa8c (Uptime/Date yellow)
    constexpr ImU32 ColOrange       = IM_COL32(255, 184, 108, 255); // #ffb86c (Bonsoir! Elliot.)
    constexpr ImU32 ColRed          = IM_COL32(255, 85, 85, 255);   // Red dot
    constexpr ImU32 ColBlue         = IM_COL32(98, 114, 254, 255);  // Blue dot
    constexpr ImU32 ColMagenta      = IM_COL32(189, 147, 249, 255); // Magenta dot
    constexpr ImU32 ColWhite        = IM_COL32(245, 247, 250, 255); // White text
    constexpr ImU32 ColMuted        = IM_COL32(130, 145, 165, 255); // Muted comment text
    constexpr ImU32 ColTrackDark    = IM_COL32(25, 34, 46, 255);    // Progress bar empty track

    // The ASCII art face/mask from Terminal.jpeg reference
    const char* const ASCII_ART_LINES[] = {
        "              .",
        "            -+-               :-",
        "    -      +###-             -===-",
        "  .###   +#####*.          +#####*:",
        " *###- +####- -####*     -###*-=*#:",
        " *******#--#-*.::**- ==- -=*#:",
        " =###*: ... **.:*#**+ -..-*- +###*:",
        ":      -###+::: *+.:-..+###=.     :",
        "**:     **#+  =+         *#::.    =##*",
        ".###*+==: +#=.+.    -     =- =#+ .-+#####*",
        " ####***+*+ ..=##+  ####=  : -**-***####*",
        " =####+=- .. -####  ####*: *** -***###*",
        "  =########*.:###+ =######-:-***###*",
        "    =#######:.*** +########=.:=*****=",
        "      .+***=..:. +*********+:.",
        "        -**#::+#*: :+=+- .#+. =########-",
        "        =***###***- :  -***-  +*********=",
        "         *#########+ ***###* -***#####-",
        "          *###*#***  -*****-.##:",
        "           *#*******-   -*- +***=",
        "            *#########* - -#####+",
        "                        .+#*+",
        "                          :"
    };
    constexpr int ASCII_ART_LINE_COUNT = sizeof(ASCII_ART_LINES) / sizeof(ASCII_ART_LINES[0]);

    // Renders a terminal-style progress bar: [||||||||||||||||||---] 79%
    inline void RenderTerminalProgressBar(ImDrawList* dl, ImVec2 pos, float width, float height, float fraction, const char* percent_text, ImU32 fill_col = ColLime) {
        float r = 3.0f;
        // Background track
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ColTrackDark, r);
        // Filled segment
        float fill_w = width * std::clamp(fraction, 0.0f, 1.0f);
        if (fill_w > 1.0f) {
            dl->AddRectFilled(pos, ImVec2(pos.x + fill_w, pos.y + height), fill_col, r);
        }
        // Outline border
        dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height), ColBorder, r, 0, 1.0f);
        // Percentage text right of bar
        if (percent_text) {
            dl->AddText(ImVec2(pos.x + width + 8.0f, pos.y - 1.0f), ColWhite, percent_text);
        }
    }

    // Custom cyber toggle button
    inline bool CyberToggle(const char* label, bool* v) {
        ImGui::PushID(label);
        bool clicked = false;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float w = 54.0f;
        float h = 24.0f;
        
        ImGui::InvisibleButton("##btn", ImVec2(w, h));
        if (ImGui::IsItemClicked()) {
            *v = !*v;
            clicked = true;
        }

        ImU32 bg_col = *v ? ColLime : IM_COL32(35, 45, 60, 255);
        ImU32 text_col = *v ? IM_COL32(10, 20, 15, 255) : ColMuted;
        const char* status_text = *v ? "ON" : "OFF";

        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg_col, 4.0f);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), *v ? ColLime : ColBorder, 4.0f, 0, 1.0f);

        ImVec2 ts = ImGui::CalcTextSize(status_text);
        dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), text_col, status_text);

        ImGui::SameLine(0, 12.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::PopID();
        return clicked;
    }

    // Draws a styled cyber box header and frame
    inline void BeginCyberBox(ImDrawList* dl, ImVec2 min_p, ImVec2 max_p, const char* title, ImU32 title_col = ColCyan) {
        dl->AddRect(min_p, max_p, ColBorder, 6.0f, 0, 1.0f);
        if (title && title[0] != '\0') {
            ImVec2 ts = ImGui::CalcTextSize(title);
            float title_x = min_p.x + 18.0f;
            // Clear gap in border line
            dl->AddRectFilled(ImVec2(title_x - 6.0f, min_p.y - 4.0f), ImVec2(title_x + ts.x + 6.0f, min_p.y + 12.0f), ColBgDark);
            dl->AddText(ImVec2(title_x, min_p.y - 7.0f), title_col, title);
        }
    }
}
