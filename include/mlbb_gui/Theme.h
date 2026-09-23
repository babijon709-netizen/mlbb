// -----------------------------------------------------------------------------
//  Theme.h - palette, metrics and ASCII art for the mlbb cheat menu.
//
//  The whole look is a "dark green terminal" theme: translucent window, muted
//  green labels, bright green values/bars and a pink block-letter banner.
// -----------------------------------------------------------------------------
#pragma once

#include "imgui.h"

namespace mlbb {
namespace theme {

// ---------------------------------------------------------------- palette ----
// Colors are ImU32 (IM_COL32) so they can be fed straight into ImDrawList.
namespace col {

constexpr ImU32 kWindowBg     = IM_COL32(  8,  17,  10, 244);  // window fill
constexpr ImU32 kTitleBg      = IM_COL32(255, 255, 255,   7);  // tab strip fill
constexpr ImU32 kTitleLine    = IM_COL32(255, 255, 255,  11);
constexpr ImU32 kPanelBg      = IM_COL32(255, 255, 255,   6);  // section fill
constexpr ImU32 kPanelBgAlt   = IM_COL32(255, 255, 255,   9);
constexpr ImU32 kPanelEdge    = IM_COL32(255, 255, 255,  20);  // section border
constexpr ImU32 kHover        = IM_COL32(255, 255, 255,   7);  // row hover
constexpr ImU32 kSeparator    = IM_COL32(255, 255, 255,  12);

constexpr ImU32 kTextBright   = IM_COL32(234, 246, 234, 255);  // headings
constexpr ImU32 kTextValue    = IM_COL32(205, 227, 205, 255);  // row values
constexpr ImU32 kTextLabel    = IM_COL32(114, 163, 111, 255);  // row labels
constexpr ImU32 kTextDim      = IM_COL32( 84, 116,  84, 255);  // captions
constexpr ImU32 kTextDark     = IM_COL32(  9,  24,  11, 255);  // on-accent text

constexpr ImU32 kAccent       = IM_COL32(112, 219, 118, 255);
constexpr ImU32 kAccentBright = IM_COL32(163, 255, 168, 255);
constexpr ImU32 kAccentDim    = IM_COL32( 76, 143,  79, 255);

constexpr ImU32 kBarTrack     = IM_COL32(255, 255, 255,  16);
constexpr ImU32 kBarFill      = IM_COL32(101, 212, 114, 230);
constexpr ImU32 kBarTip       = IM_COL32(178, 255, 186, 255);

constexpr ImU32 kPink         = IM_COL32(255, 150, 205, 255);
constexpr ImU32 kPinkBright   = IM_COL32(255, 194, 228, 255);
constexpr ImU32 kPinkDim      = IM_COL32(190,  92, 148, 255);

constexpr ImU32 kSwitchOn     = IM_COL32(113, 220, 120, 255);
constexpr ImU32 kSwitchKnob   = IM_COL32(238, 250, 238, 255);   // light knob, iOS style
constexpr ImU32 kSwitchOff    = IM_COL32(255, 255, 255,  26);
constexpr ImU32 kWarn         = IM_COL32(226, 198, 116, 255);
constexpr ImU32 kDanger       = IM_COL32(228, 108, 108, 255);

// The little status dots in the footer (same set as the reference shot).
constexpr ImU32 kDots[] = {
    IM_COL32(160, 166, 172, 255),  // grey
    IM_COL32( 92, 146, 232, 255),  // blue
    IM_COL32(148, 108, 232, 255),  // violet
    IM_COL32(228,  96,  96, 255),  // red
    IM_COL32(232, 168,  72, 255),  // amber
    IM_COL32(104, 202, 118, 255),  // green
};

}  // namespace col

// ---------------------------------------------------------------- metrics ----
namespace metrics {

constexpr float kPad          = 16.0f;   // window padding
constexpr float kTitleH       = 42.0f;   // tab strip height
constexpr float kFooterH      = 34.0f;
constexpr float kSidebarW     = 420.0f;
constexpr float kColumnGap    = 18.0f;

constexpr float kSectionPad   = 12.0f;   // inner padding of a section
constexpr float kSectionGap   = 14.0f;
constexpr float kSectionHead  = 26.0f;   // space taken by a section title

constexpr float kRowH         = 22.0f;   // info row
constexpr float kToggleH      = 26.0f;   // checkbox row
constexpr float kSliderH      = 36.0f;
constexpr float kWidgetH      = 28.0f;   // combo / input
constexpr float kButtonH      = 28.0f;

constexpr float kLabelW       = 66.0f;   // "PROC |" label column
constexpr float kBarW         = 124.0f;

constexpr float kRadiusWin    = 12.0f;
constexpr float kRadiusPanel  = 8.0f;
constexpr float kRadiusSmall  = 4.0f;
constexpr float kSwitchW      = 26.0f;   // checkbox size
constexpr float kSwitchH      = 15.0f;

}  // namespace metrics

// -------------------------------------------------------------------- art ----
// ASCII art of the menu. Both blocks are plain UTF-8 text, one entry per line,
// terminated by a nullptr. Font sizes are chosen automatically to fit the
// column, so any art of a similar aspect ratio can be dropped in here.

extern const char* const kBanner[];   // block letters, drawn in pink
extern const char* const kEmblem[];   // small mark under the banner

// ---------------------------------------------------------------------------
//  Applies the theme to the current ImGui context (call once after
//  ImGui::CreateContext()). Styles the widgets ImGui draws itself (popups,
//  combo boxes, text inputs) so they blend with the hand-drawn sections.
// ---------------------------------------------------------------------------
void ApplyTheme();

}  // namespace theme
}  // namespace mlbb
