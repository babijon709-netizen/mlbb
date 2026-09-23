// -----------------------------------------------------------------------------
//  Menu.cpp - everything that is drawn by the menu.
//
//  The widgets are hand-drawn with ImDrawList on purpose: the terminal-ish look
//  of the reference (sections with the title sitting on the border, fastfetch
//  style info rows with bars, pink block-letter art) is not something the stock
//  ImGui widgets can produce. Only popups / text inputs are real ImGui widgets,
//  they are re-colored by theme::ApplyTheme().
//
//  !!! No cheat logic lives here !!!  Every toggle below just keeps its own
//  value so that the UI has something to show.
// -----------------------------------------------------------------------------
#include "mlbb_gui/Menu.h"
#include "mlbb_gui/Theme.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace mlbb {
namespace {

using namespace mlbb::theme;

// ============================================================ tiny helpers ===
constexpr float kPi = 3.14159265358979323846f;

inline ImVec2 V(float x, float y) { return ImVec2(x, y); }
inline ImVec2 VAdd(const ImVec2& a, const ImVec2& b) { return V(a.x + b.x, a.y + b.y); }
inline float  Min(float a, float b) { return a < b ? a : b; }
inline float  Max(float a, float b) { return a > b ? a : b; }
inline float  Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float  ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int    ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float TextW(ImFont* font, float size, const char* text)
{
    if (!font || !text) return 0.0f;
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

inline void Text(ImDrawList* dl, ImFont* font, float size, const ImVec2& p, ImU32 c, const char* text)
{
    if (font && text) dl->AddText(font, size, p, c, text);
}

inline void TextRight(ImDrawList* dl, ImFont* font, float size, float rightX, float y, ImU32 c, const char* text)
{
    Text(dl, font, size, V(rightX - TextW(font, size, text), y), c, text);
}

// Centered text inside a box.
inline void TextCenter(ImDrawList* dl, ImFont* font, float size, const ImVec2& min, const ImVec2& max, ImU32 c, const char* text)
{
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    Text(dl, font, size, V(min.x + (max.x - min.x - ts.x) * 0.5f, min.y + (max.y - min.y - ts.y) * 0.5f), c, text);
}

// Small icons drawn with primitives so they never depend on the font.
inline void IconMinus(ImDrawList* dl, const ImVec2& c, ImU32 col)
{
    dl->AddLine(V(c.x - 5.0f, c.y), V(c.x + 5.0f, c.y), col, 1.6f);
}

inline void IconSquare(ImDrawList* dl, const ImVec2& c, ImU32 col)
{
    dl->AddRect(V(c.x - 4.5f, c.y - 4.5f), V(c.x + 4.5f, c.y + 4.5f), col, 1.5f, 0, 1.4f);
}

inline void IconCross(ImDrawList* dl, const ImVec2& c, ImU32 col)
{
    dl->AddLine(V(c.x - 4.5f, c.y - 4.5f), V(c.x + 4.5f, c.y + 4.5f), col, 1.6f);
    dl->AddLine(V(c.x + 4.5f, c.y - 4.5f), V(c.x - 4.5f, c.y + 4.5f), col, 1.6f);
}

inline void IconChevronDown(ImDrawList* dl, const ImVec2& c, ImU32 col)
{
    dl->AddLine(V(c.x - 3.5f, c.y - 1.5f), V(c.x, c.y + 1.5f), col, 1.4f);
    dl->AddLine(V(c.x + 3.5f, c.y - 1.5f), V(c.x, c.y + 1.5f), col, 1.4f);
}

// The little diamond mark used as the window/brand icon.
inline void IconDiamond(ImDrawList* dl, const ImVec2& c, float r, ImU32 col)
{
    const ImVec2 p[4] = { V(c.x, c.y - r), V(c.x + r, c.y), V(c.x, c.y + r), V(c.x - r, c.y) };
    dl->AddConvexPolyFilled(p, 4, col);
}

// =============================================================== menu state ==
struct UiState {
    bool   placed = false;
    bool   dragging = false;
    ImVec2 dragOffset = ImVec2(0.0f, 0.0f);
    bool   wasFolded = false;
    ImVec2 unfoldedSize = ImVec2(0.0f, 0.0f);
    float  scroll = 0.0f;      // compact layout: pixels the page is scrolled by
    double startedAt = 0.0;
    char   startedDate[32] = { 0 };
};

UiState g_ui;

// ---------------------------------------------------------------- log -------
// Tiny ring buffer of UI events, printed in the LOG section. It exists so the
// menu can *show* something happening without a single line of cheat logic.
struct LogEntry {
    char time[16];
    char text[96];
};

struct Log {
    LogEntry items[64];
    int count = 0;   // total pushed
};

Log g_log;

void ClockStamp(char* out, int outSize)
{
    std::time_t now = std::time(nullptr);
    std::tm* lt = std::localtime(&now);
    if (!lt) { std::snprintf(out, outSize, "--:--:--"); return; }
    std::snprintf(out, (size_t)outSize, "%02u:%02u:%02u",
                  (unsigned)lt->tm_hour % 100u, (unsigned)lt->tm_min % 100u, (unsigned)lt->tm_sec % 100u);
}

void LogPush(const char* fmt, ...)
{
    LogEntry& e = g_log.items[g_log.count % (int)(sizeof(g_log.items) / sizeof(g_log.items[0]))];
    ClockStamp(e.time, (int)sizeof(e.time));

    va_list args;
    va_start(args, fmt);
    std::vsnprintf(e.text, sizeof(e.text), fmt, args);
    va_end(args);

    ++g_log.count;
}

const LogEntry* LogAt(int indexFromNewest)
{
    if (indexFromNewest < 0 || indexFromNewest >= g_log.count) return nullptr;
    const int cap = (int)(sizeof(g_log.items) / sizeof(g_log.items[0]));
    if (indexFromNewest >= cap) return nullptr;
    return &g_log.items[(g_log.count - 1 - indexFromNewest) % cap];
}

void FormatDateTime(char* out, int outSize)
{
    std::time_t now = std::time(nullptr);
    std::tm* lt = std::localtime(&now);
    if (!lt) { std::snprintf(out, (size_t)outSize, "--"); return; }

    // Unsigned + explicit modulo keeps the field widths provably small, so the
    // compiler does not warn about a potentially truncated snprintf().
    const unsigned y  = (unsigned)(lt->tm_year + 1900) % 10000u;
    const unsigned mo = (unsigned)(lt->tm_mon + 1) % 100u;
    const unsigned d  = (unsigned)lt->tm_mday % 100u;
    const unsigned h  = (unsigned)lt->tm_hour % 100u;
    const unsigned mi = (unsigned)lt->tm_min % 100u;
    const unsigned se = (unsigned)lt->tm_sec % 100u;

    char buf[24];   // "YYYY-MM-DD HH:MM:SS" + NUL
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", y, mo, d, h, mi, se);
    std::snprintf(out, (size_t)outSize, "%s", buf);
}

// ================================================================ sections ===
// A section is a rounded panel whose title sits on the top border (the border
// path is stroked by hand so that a gap is left for the title).
struct Section {
    ImDrawList* dl = nullptr;
    const Fonts* f = nullptr;
    ImVec2 min, max;
    ImVec2 inner;      // first row position
    float  innerW = 0.0f;
    float  y = 0.0f;   // running row cursor

    float Row(float h) { const float r = y; y += h; return r; }
};

// Height of a section that holds `rows` info rows.
float SectionHeight(int rows, float rowH)
{
    return metrics::kSectionHead + metrics::kSectionPad * 2.0f + (float)rows * rowH;
}

Section BeginSection(ImDrawList* dl, const Fonts& f, const char* title, const ImVec2& pos, float w, float h)
{
    Section s;
    s.dl = dl; s.f = &f;
    s.min = pos;
    s.max = V(pos.x + w, pos.y + h);
    s.inner = V(pos.x + metrics::kSectionPad, pos.y + metrics::kSectionHead);
    s.innerW = w - metrics::kSectionPad * 2.0f;
    s.y = s.inner.y;

    dl->AddRectFilled(s.min, s.max, col::kPanelBg, metrics::kRadiusPanel);

    // ---- border with a gap in the middle of the top edge
    const float r  = metrics::kRadiusPanel;
    const float tw = TextW(f.small, f.smallSize, title);
    const float tx = pos.x + (w - tw) * 0.5f;
    const float gap = 10.0f;
    const float x0 = pos.x, x1 = pos.x + w, y0 = pos.y, y1 = pos.y + h;

    dl->PathClear();
    dl->PathLineTo(V(tx - gap, y0));
    dl->PathLineTo(V(x0 + r, y0));
    dl->PathArcTo(V(x0 + r, y0 + r), r, kPi * 1.5f, kPi, 6);
    dl->PathLineTo(V(x0, y1 - r));
    dl->PathArcTo(V(x0 + r, y1 - r), r, kPi, kPi * 0.5f, 6);
    dl->PathLineTo(V(x1 - r, y1));
    dl->PathArcTo(V(x1 - r, y1 - r), r, kPi * 0.5f, 0.0f, 6);
    dl->PathLineTo(V(x1, y0 + r));
    dl->PathArcTo(V(x1 - r, y0 + r), r, 0.0f, -kPi * 0.5f, 6);
    dl->PathLineTo(V(tx + tw + gap, y0));
    dl->PathStroke(col::kPanelEdge, 0, 1.0f);

    // ---- title on the top edge
    Text(dl, f.small, f.smallSize, V(tx, y0 - f.smallSize * 0.5f - 1.0f), col::kTextBright, title);
    return s;
}

// ================================================================ widgets ====
// A progress bar in the style of the reference: rounded track, green fill and a
// brighter tip.
void DrawBar(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float pct, ImU32 fill, ImU32 tip)
{
    const float h = max.y - min.y;
    const float round = h * 0.5f;
    dl->AddRectFilled(min, max, col::kBarTrack, round);
    const float w = max.x - min.x;
    const float fw = w * Clamp01(pct);
    if (fw > 0.5f) {
        const float tipW = Min(fw, 7.0f);
        dl->AddRectFilled(min, V(min.x + fw, max.y), fill, round);
        dl->AddRectFilled(V(min.x + fw - tipW, min.y), V(min.x + fw, max.y), tip, round);
    }
}

// --------------------------------------------------------------- info row ----
// "LABEL | value ................ [=====] 79%" - the fastfetch look.
void InfoRow(ImDrawList* dl, const Fonts& f, const ImVec2& pos, float w, float h,
             const char* label, const char* value, float pct, const char* pctText)
{
    const float cy = pos.y + h * 0.5f;

    Text(dl, f.small, f.smallSize, V(pos.x, cy - f.smallSize * 0.5f), col::kTextLabel, label);

    const float sepX = pos.x + metrics::kLabelW - 14.0f;
    dl->AddLine(V(sepX, cy - 6.0f), V(sepX, cy + 6.0f), col::kSeparator, 1.0f);

    Text(dl, f.body, f.bodySize, V(pos.x + metrics::kLabelW, cy - f.bodySize * 0.5f), col::kTextValue, value);

    if (pct >= 0.0f) {
        const float pctW = pctText ? 46.0f : 0.0f;
        const float barW = Min(metrics::kBarW, w - metrics::kLabelW - 150.0f - pctW);
        if (barW > 24.0f) {
            const float bx = pos.x + w - pctW - barW;
            DrawBar(dl, V(bx, cy - 3.0f), V(bx + barW, cy + 3.0f), pct, col::kBarFill, col::kBarTip);
        }
        if (pctText)
            TextRight(dl, f.bold, f.boldSize, pos.x + w, cy - f.boldSize * 0.5f, col::kAccent, pctText);
    }
}

// ---------------------------------------------------------------- toggle -----
bool ToggleRow(ImDrawList* dl, const Fonts& f, const ImVec2& pos, float w, float h,
               const char* label, bool* value, const char* key)
{
    ImGui::PushID(label);
    ImGui::SetCursorScreenPos(pos);
    const bool clicked = ImGui::InvisibleButton("##toggle", V(w, h));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    if (hovered)
        dl->AddRectFilled(pos, V(pos.x + w, pos.y + h), col::kHover, metrics::kRadiusSmall);

    if (clicked && value) {
        *value = !*value;
        LogPush("%s %s", label, *value ? "enabled" : "disabled");
    }
    const bool on = value && *value;
    const float cy = pos.y + h * 0.5f;

    // switch
    const float sh = metrics::kSwitchH, sw = metrics::kSwitchW;
    const ImVec2 s0 = V(pos.x + 3.0f, cy - sh * 0.5f);
    const ImVec2 s1 = V(s0.x + sw, s0.y + sh);
    dl->AddRectFilled(s0, s1, on ? col::kSwitchOn : col::kSwitchOff, sh * 0.5f);
    if (!on) dl->AddRect(s0, s1, col::kPanelEdge, sh * 0.5f, 0, 1.0f);
    const float kr = sh * 0.5f - 2.0f;
    const ImVec2 kc = V(on ? s1.x - kr - 2.0f : s0.x + kr + 2.0f, cy);
    dl->AddCircleFilled(kc, kr, on ? col::kSwitchKnob : col::kTextLabel, 18);

    const float tx = s1.x + 10.0f;
    float avail = pos.x + w - tx - 6.0f;
    if (key) avail -= 46.0f;
    const char* shown = label;
    char clipped[64];
    if (TextW(f.body, f.bodySize, label) > avail && avail > 12.0f) {
        std::snprintf(clipped, sizeof(clipped), "%s", label);
        int n = (int)std::strlen(clipped);
        while (n > 1 && TextW(f.body, f.bodySize, clipped) > avail - 12.0f) clipped[--n] = '\0';
        std::strcat(clipped, "…");
        shown = clipped;
    }
    Text(dl, on ? f.bold : f.body, f.bodySize, V(tx, cy - f.bodySize * 0.5f),
         on ? col::kTextBright : col::kTextValue, shown);

    if (key) {
        const float kw = TextW(f.small, f.smallSize, key);
        const ImVec2 k0 = V(pos.x + w - kw - 16.0f, cy - 8.0f);
        const ImVec2 k1 = V(pos.x + w - 2.0f, cy + 8.0f);
        dl->AddRectFilled(k0, k1, col::kPanelBgAlt, 3.0f);
        dl->AddRect(k0, k1, col::kPanelEdge, 3.0f, 0, 1.0f);
        Text(dl, f.small, f.smallSize, V(k0.x + 6.0f, cy - f.smallSize * 0.5f), col::kTextDim, key);
    }
    return clicked;
}

// ---------------------------------------------------------------- slider -----
bool SliderRow(ImDrawList* dl, const Fonts& f, const ImVec2& pos, float w, float h,
               const char* label, float* value, float lo, float hi,
               const char* fmt, float step)
{
    ImGui::PushID(label);
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##slider", V(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();
    ImGui::PopID();

    const float x0 = pos.x + 3.0f, x1 = pos.x + w - 3.0f;
    const float cy = pos.y + h * 0.5f;
    const float trackY = pos.y + h - 9.0f;

    if (active && value) {
        float t = Clamp01((ImGui::GetIO().MousePos.x - x0) / Max(1.0f, x1 - x0));
        float v = lo + t * (hi - lo);
        if (step > 0.0f) v = lo + std::floor((v - lo) / step + 0.5f) * step;
        *value = ClampF(v, lo, hi);
    }

    const float t = value ? Clamp01((*value - lo) / Max(0.0001f, hi - lo)) : 0.0f;

    // label + current value
    Text(dl, f.body, f.bodySize, V(pos.x + 3.0f, cy - f.bodySize * 0.5f - 4.0f), col::kTextLabel, label);
    char buf[48];
    std::snprintf(buf, sizeof(buf), fmt ? fmt : "%.2f", value ? (double)*value : 0.0);
    TextRight(dl, f.bold, f.boldSize, pos.x + w - 3.0f, cy - f.boldSize * 0.5f - 4.0f,
              active ? col::kAccentBright : col::kAccent, buf);

    // track
    dl->AddRectFilled(V(x0, trackY - 3.0f), V(x1, trackY + 3.0f), col::kBarTrack, 3.0f);
    const float fx = x0 + (x1 - x0) * t;
    if (fx > x0 + 0.5f)
        dl->AddRectFilled(V(x0, trackY - 3.0f), V(fx, trackY + 3.0f), col::kBarFill, 3.0f);

    // knob
    const float kr = (hovered || active) ? 6.5f : 5.5f;
    dl->AddCircleFilled(V(fx, trackY), kr, active ? col::kAccentBright : col::kAccent, 20);
    dl->AddCircleFilled(V(fx, trackY), 1.8f, col::kTextDark, 10);
    if (ImGui::IsItemDeactivatedAfterEdit() && value)
        LogPush("%s set to %s", label, buf);
    return active;
}

// ----------------------------------------------------------------- combo -----
bool ComboRow(ImDrawList* dl, const Fonts& f, const ImVec2& pos, float w, float h,
              const char* label, int* value, const char* const* items, int count)
{
    const float labelW = Min(122.0f, w * 0.46f);
    const float boxH = Min(metrics::kWidgetH, h);
    const float cy = pos.y + h * 0.5f;

    Text(dl, f.body, f.bodySize, V(pos.x + 3.0f, cy - f.bodySize * 0.5f), col::kTextLabel, label);

    ImGui::PushID(label);
    ImGui::SetCursorScreenPos(V(pos.x + labelW, cy - boxH * 0.5f));
    ImGui::SetNextItemWidth(w - labelW);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, V(10.0f, (boxH - f.bodySize) * 0.5f - 1.0f));
    const bool changed = ImGui::Combo("##combo", value, items, count);
    ImGui::PopStyleVar();
    ImGui::PopID();

    // chevron, drawn on top of the real widget
    IconChevronDown(dl, V(pos.x + w - 12.0f, cy), col::kAccentDim);
    if (changed && value && items && *value >= 0 && *value < count)
        LogPush("%s -> %s", label, items[*value]);
    return changed;
}

// ----------------------------------------------------------------- input -----
bool InputRow(ImDrawList* dl, const Fonts& f, const ImVec2& pos, float w, float h,
              const char* label, char* buf, int bufSize)
{
    const float labelW = Min(122.0f, w * 0.46f);
    const float boxH = Min(metrics::kWidgetH, h);
    const float cy = pos.y + h * 0.5f;

    Text(dl, f.body, f.bodySize, V(pos.x + 3.0f, cy - f.bodySize * 0.5f), col::kTextLabel, label);

    ImGui::PushID(label);
    ImGui::SetCursorScreenPos(V(pos.x + labelW, cy - boxH * 0.5f));
    ImGui::SetNextItemWidth(w - labelW);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, V(10.0f, (boxH - f.bodySize) * 0.5f - 1.0f));
    const bool changed = ImGui::InputText("##input", buf, (size_t)bufSize);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------- button -----
// variant: 0 = neutral, 1 = accent (called to action), 2 = danger
bool ButtonRow(ImDrawList* dl, const Fonts& f, const ImVec2& pos, float w, float h,
               const char* label, int variant)
{
    ImGui::PushID(label);
    ImGui::SetCursorScreenPos(pos);
    const bool clicked = ImGui::InvisibleButton("##button", V(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();
    ImGui::PopID();

    ImU32 bg     = col::kPanelBgAlt;
    ImU32 border = col::kPanelEdge;
    ImU32 text   = col::kTextValue;
    if (variant == 1) { bg = IM_COL32(113, 220, 120, hovered ? 48 : 30); border = IM_COL32(113, 220, 120, 150); text = col::kAccentBright; }
    if (variant == 2) { bg = IM_COL32(228, 108, 108, hovered ? 42 : 22); border = IM_COL32(228, 108, 108, 120); text = col::kDanger; }
    if (hovered) bg = variant == 0 ? col::kHover : bg;
    if (active)  bg = col::kHover;

    dl->AddRectFilled(pos, V(pos.x + w, pos.y + h), bg, metrics::kRadiusSmall);
    dl->AddRect(pos, V(pos.x + w, pos.y + h), border, metrics::kRadiusSmall, 0, 1.0f);
    TextCenter(dl, f.bold, f.boldSize, pos, V(pos.x + w, pos.y + h), text, label);
    return clicked;
}

// ============================================== feature rows (declarative) ===
enum class RowKind { Toggle, Slider, Combo };

struct RowSpec {
    RowKind kind = RowKind::Toggle;
    const char* label = "";
    const char* key = nullptr;            // toggles: hotkey tag
    bool  Features::* b = nullptr;
    float Features::* f = nullptr;
    int   Features::* i = nullptr;
    float lo = 0.0f, hi = 1.0f, step = 0.0f;
    const char* fmt = nullptr;
    const char* const* items = nullptr;
    int count = 0;
    int span = 1;                          // in columns
};

RowSpec Toggle(const char* label, bool Features::* m, const char* key = nullptr, int span = 1)
{
    RowSpec r; r.kind = RowKind::Toggle; r.label = label; r.b = m; r.key = key; r.span = span; return r;
}

RowSpec Slider(const char* label, float Features::* m, float lo, float hi, const char* fmt, float step = 0.0f, int span = 1)
{
    RowSpec r; r.kind = RowKind::Slider; r.label = label; r.f = m;
    r.lo = lo; r.hi = hi; r.fmt = fmt; r.step = step; r.span = span; return r;
}

RowSpec Combo(const char* label, int Features::* m, const char* const* items, int count, int span = 1)
{
    RowSpec r; r.kind = RowKind::Combo; r.label = label; r.i = m; r.items = items; r.count = count; r.span = span; return r;
}

inline float RowHeight(RowKind k)
{
    switch (k) {
        case RowKind::Toggle: return metrics::kToggleH;
        case RowKind::Slider: return metrics::kSliderH;
        case RowKind::Combo:  return metrics::kWidgetH;
    }
    return metrics::kToggleH;
}

struct Block {
    const RowSpec* rows;
    int count;
    int cols;
};

// Height of a block laid out in `cols` columns. Must match LayoutBlock().
float MeasureBlock(const Block& b, float totalW, float colGap, float rowGap)
{
    (void)totalW;
    float y = 0.0f;
    int i = 0;
    while (i < b.count) {
        float lineH = 0.0f;
        int used = 0;
        while (i < b.count && used < b.cols) {
            const int span = ClampI(b.rows[i].span, 1, b.cols);
            if (used > 0 && used + span > b.cols) break;
            lineH = Max(lineH, RowHeight(b.rows[i].kind));
            used += span;
            ++i;
            if (span == b.cols) break;
        }
        y += lineH + rowGap;
    }
    return y > 0.0f ? y - rowGap : 0.0f;
}

// Lays the block out and draws it. Returns the height it consumed.
float LayoutBlock(ImDrawList* dl, const Fonts& f, const Block& b, const ImVec2& origin,
                  float totalW, float colGap, float rowGap, Features& st, float clipBottom)
{
    const float colW = (totalW - colGap * (b.cols - 1)) / (float)b.cols;
    float y = 0.0f;
    int i = 0;
    while (i < b.count) {
        const int lineFirst = i;
        float lineH = 0.0f;
        int used = 0;
        while (i < b.count && used < b.cols) {
            const int span = ClampI(b.rows[i].span, 1, b.cols);
            if (used > 0 && used + span > b.cols) break;
            lineH = Max(lineH, RowHeight(b.rows[i].kind));
            used += span;
            ++i;
            if (span == b.cols) break;
        }

        float x = origin.x;
        for (int k = lineFirst; k < i; ++k) {
            const RowSpec& r = b.rows[k];
            const int span = ClampI(r.span, 1, b.cols);
            const float w = colW * (float)span + colGap * (float)(span - 1);
            const ImVec2 pos = V(x, origin.y + y);
            if (clipBottom <= 0.0f || pos.y + lineH <= clipBottom) {
                switch (r.kind) {
                    case RowKind::Toggle: ToggleRow(dl, f, pos, w, lineH, r.label, r.b ? &(st.*(r.b)) : nullptr, r.key); break;
                    case RowKind::Slider: SliderRow(dl, f, pos, w, lineH, r.label, r.f ? &(st.*(r.f)) : nullptr, r.lo, r.hi, r.fmt, r.step); break;
                    case RowKind::Combo:  ComboRow (dl, f, pos, w, lineH, r.label, r.i ? &(st.*(r.i)) : nullptr, r.items, r.count); break;
                }
            }
            x += w + colGap;
        }
        y += lineH + rowGap;
    }
    return y > 0.0f ? y - rowGap : 0.0f;
}

// ================================================ tab content description ====
const char* const kBoxStyles[]  = { "Rect", "Corner", "Circle" };
const char* const kIndicators[] = { "Bar", "Text", "Both" };
const char* const kPriorities[] = { "Nearest", "Lowest HP", "Crosshair" };
const char* const kHitboxes[]   = { "Head", "Chest", "Pelvis" };
const char* const kFpsCaps[]    = { "60", "90", "120", "144", "240" };
const char* const kLanguages[]  = { "English", "Русский", "Español", "Português" };
const char* const kProfiles[]   = { "Legit", "Default", "Rage" };

const RowSpec kEspRows[] = {
    Toggle("ESP Box",     &Features::espBox,       "F1"), Toggle("Skeleton",   &Features::espSkeleton, "F2"),
    Toggle("Line",        &Features::espLine,      "F3"), Toggle("Health Bar", &Features::espHealthBar, "F4"),
    Toggle("Hero Name",   &Features::espName,      "F5"), Toggle("Distance",   &Features::espDistance,  "F6"),
    Toggle("Hero Icon",   &Features::espIcon,      "F7"), Toggle("Team Check", &Features::espTeamCheck, "F8"),
    Combo ("Box Style",   &Features::espBoxStyle,  kBoxStyles, 3),
    Combo ("Indicator",   &Features::espIndicator, kIndicators, 3),
};

const RowSpec kAimRows[] = {
    Toggle("Enable",        &Features::aimEnabled,    "F1"), Toggle("Prediction",    &Features::aimPrediction, "F2"),
    Toggle("Visible Check", &Features::aimVisible,    "F3"), Toggle("FOV Circle",    &Features::aimFovCircle,  "F4"),
    Slider("Field of View", &Features::aimFov, 0.0f, 360.0f, "%.0f°"),
    Slider("Smoothness",    &Features::aimSmooth, 0.0f, 10.0f, "%.1f"),
    Slider("Offset Y",      &Features::aimOffsetY, -50.0f, 50.0f, "%.0f"),
    Combo ("Priority",      &Features::aimPriority, kPriorities, 3),
    Combo ("Hitbox",        &Features::aimHitbox,   kHitboxes, 3),
};

const RowSpec kVisualRows[] = {
    Toggle("Map Hack",   &Features::visMapHack,  "F1"), Toggle("Skill CD",  &Features::visSkillCd,   "F2"),
    Toggle("Spell CD",   &Features::visSpellCd,  "F3"), Toggle("Drone View",&Features::visDroneView, "F4"),
    Toggle("Zoom Out",   &Features::visZoomOut,  "F5"),
    Slider("Camera Height", &Features::visCamera, 0.0f, 100.0f, "%.0f"),
    Combo ("FPS Cap",    &Features::visFpsCap, kFpsCaps, 5, 2),
};

const RowSpec kMiscRows[] = {
    Toggle("Anti Ban",         &Features::miscAntiBan, "F1"), Toggle("Bypass",        &Features::miscBypass, "F2"),
    Toggle("Auto Retribution", &Features::miscRetri,   "F3"), Toggle("No Grass",      &Features::miscNoGrass, "F4"),
    Toggle("Instant Recall",   &Features::miscRecall,  "F5"),
    Slider("Recall Delay",     &Features::miscDelay, 0.0f, 2.0f, "%.2f s"),
    Combo ("Language",         &Features::miscLanguage, kLanguages, 4, 2),
};

const Block kEspBlocks[]    = { { kEspRows,    (int)(sizeof(kEspRows)    / sizeof(RowSpec)), 2 } };
const Block kAimBlocks[]    = { { kAimRows,    (int)(sizeof(kAimRows)    / sizeof(RowSpec)), 2 } };
const Block kVisualBlocks[] = { { kVisualRows, (int)(sizeof(kVisualRows) / sizeof(RowSpec)), 2 } };
const Block kMiscBlocks[]   = { { kMiscRows,   (int)(sizeof(kMiscRows)   / sizeof(RowSpec)), 2 } };
// The CONFIG tab has no row table - it is drawn by hand in DrawTabContent().

struct TabSpec {
    const char* name;
    const char* caption;
    const Block* blocks;
    int blockCount;
};

const int kTabCount = 5;
const TabSpec kTabs[kTabCount] = {
    { "ESP",    "visual assistance", kEspBlocks,    1 },
    { "AIM",    "assist & prediction", kAimBlocks,  1 },
    { "VISUAL", "world & camera",    kVisualBlocks, 1 },
    { "MISC",   "utilities",         kMiscBlocks,   1 },
    { "CONFIG", "profiles & files",  nullptr,       0 },
};

float TabContentHeight(int tab, float w, float colGap, float rowGap, const Fonts& f)
{
    if (tab == 4) {                                                // CONFIG
        return metrics::kWidgetH + rowGap                                // profile combo
             + metrics::kWidgetH + rowGap                                // config name
             + metrics::kToggleH * 2.0f + rowGap                         // 2 toggles
             + metrics::kButtonH;                                        // buttons
    }
    float h = 0.0f;
    const TabSpec& t = kTabs[tab];
    for (int i = 0; i < t.blockCount; ++i)
        h += MeasureBlock(t.blocks[i], w, colGap, rowGap) + rowGap;
    (void)f;
    return h > 0.0f ? h - rowGap : 0.0f;
}

void DrawTabContent(ImDrawList* dl, const Fonts& f, int tab, const ImVec2& origin,
                    float w, float clipBottom, Features& st)
{
    const float colGap = 12.0f;
    const float rowGap = 8.0f;

    if (tab == 4) {                                                // ----- CONFIG
        float y = origin.y;
        ComboRow(dl, f, V(origin.x, y), w, metrics::kWidgetH, "Profile",
                 &st.cfgProfile, kProfiles, 3);
        y += metrics::kWidgetH + rowGap;
        InputRow(dl, f, V(origin.x, y), w, metrics::kWidgetH, "Config name",
                 st.cfgName, (int)sizeof(st.cfgName));
        y += metrics::kWidgetH + rowGap;

        const float colW = (w - colGap) * 0.5f;
        ToggleRow(dl, f, V(origin.x, y), colW, metrics::kToggleH, "Auto Save", &st.cfgAutoSave, nullptr);
        ToggleRow(dl, f, V(origin.x + colW + colGap, y), colW, metrics::kToggleH, "Load On Start", &st.cfgLoadOnStart, nullptr);
        y += metrics::kToggleH + rowGap;

        const float btnW = (w - colGap * 3.0f) * 0.25f;
        ButtonRow(dl, f, V(origin.x, y), btnW, metrics::kButtonH, "Save", 1);
        ButtonRow(dl, f, V(origin.x + (btnW + colGap), y), btnW, metrics::kButtonH, "Load", 0);
        ButtonRow(dl, f, V(origin.x + (btnW + colGap) * 2.0f, y), btnW, metrics::kButtonH, "Reset", 0);
        ButtonRow(dl, f, V(origin.x + (btnW + colGap) * 3.0f, y), btnW, metrics::kButtonH, "Unload", 2);
        return;
    }

    const TabSpec& t = kTabs[tab];
    float y = origin.y;
    for (int i = 0; i < t.blockCount; ++i) {
        y += LayoutBlock(dl, f, t.blocks[i], V(origin.x, y), w, colGap, rowGap, st, clipBottom) + rowGap;
    }
}

// ======================================================= window chrome =======
// On a phone the strip gets a second row: the tabs move down and span the full
// width so they stay reachable with a thumb.
float TitleStripHeight(const Config& cfg)
{
    return cfg.compact ? metrics::kTitleH + 32.0f : metrics::kTitleH;
}

void DrawTitleStrip(ImDrawList* dl, const Fonts& f, Config& cfg, Features& st,
                    const ImVec2& wpos, float w, float bodyTop)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool  compact = cfg.compact;
    const float stripH  = TitleStripHeight(cfg);

    // background + bottom hairline
    dl->AddRectFilled(wpos, V(wpos.x + w, wpos.y + stripH), col::kTitleBg,
                      metrics::kRadiusWin, ImDrawFlags_RoundCornersTop);
    dl->AddLine(V(wpos.x, wpos.y + stripH - 0.5f),
                V(wpos.x + w, wpos.y + stripH - 0.5f), col::kTitleLine, 1.0f);

    const float controlsW = 3.0f * 30.0f + 8.0f;

    // ---- brand
    const float iconSize = 22.0f;
    const ImVec2 i0 = V(wpos.x + 14.0f, wpos.y + (metrics::kTitleH - iconSize) * 0.5f);
    const ImVec2 i1 = V(i0.x + iconSize, i0.y + iconSize);
    dl->AddRectFilled(i0, i1, IM_COL32(255, 150, 205, 20), 6.0f);
    dl->AddRect(i0, i1, IM_COL32(255, 150, 205, 90), 6.0f, 0, 1.0f);
    IconDiamond(dl, V(i0.x + iconSize * 0.5f, i0.y + iconSize * 0.5f), 5.0f, col::kPink);

    const char* name = cfg.windowTitle ? cfg.windowTitle : "mlbb";
    float x = i1.x + 10.0f;
    if (!compact) {
        const float titleY = wpos.y + (metrics::kTitleH - f.boldSize) * 0.5f;
        Text(dl, f.bold, f.boldSize, V(x, titleY), col::kTextBright, name);
        x += TextW(f.bold, f.boldSize, name);

        // build tag
        char buildTag[64];
        std::snprintf(buildTag, sizeof(buildTag), "build %s", cfg.build ? cfg.build : "0x0000");
        x += 12.0f;
        Text(dl, f.small, f.smallSize, V(x, wpos.y + (metrics::kTitleH - f.smallSize) * 0.5f), col::kTextDim, buildTag);
        x += TextW(f.small, f.smallSize, buildTag) + 16.0f;

        // separator
        dl->AddLine(V(x, wpos.y + 11.0f), V(x, wpos.y + metrics::kTitleH - 11.0f), col::kSeparator, 1.0f);
        x += 14.0f;
    }

    // ---- tabs
    ImFont*     tabFont = compact ? f.small : f.bold;
    const float tabSize = compact ? f.smallSize : f.boldSize;
    const float tabPad  = compact ? 12.0f : 30.0f;
    const float tabH    = compact ? 24.0f : 26.0f;
    const float tabTop  = compact ? wpos.y + stripH - tabH - 2.0f
                                  : wpos.y + metrics::kTitleH - tabH - 2.0f;
    const float tabEnd  = compact ? wpos.x + w - 8.0f : wpos.x + w - controlsW - 8.0f;
    if (compact) x = wpos.x + 12.0f;
    for (int i = 0; i < kTabCount; ++i) {
        const char* label = kTabs[i].name;
        const float tw = TextW(tabFont, tabSize, label) + tabPad * 2.0f;
        if (x + tw > tabEnd) break;

        char id[32];
        std::snprintf(id, sizeof(id), "##tab%d", i);
        ImGui::SetCursorScreenPos(V(x, tabTop));
        ImGui::InvisibleButton(id, V(tw, tabH));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemActivated()) st.tab = i;

        const bool sel = (st.tab == i);
        const ImVec2 t0 = V(x, sel ? tabTop : tabTop + 2.0f);
        const ImVec2 t1 = V(x + tw, compact ? wpos.y + stripH : wpos.y + metrics::kTitleH);
        if (sel) {
            dl->AddRectFilled(t0, t1, col::kPanelBgAlt, 7.0f, ImDrawFlags_RoundCornersTop);
            dl->AddLine(V(t0.x + 1.0f, t1.y - 0.5f), V(t1.x - 1.0f, t1.y - 0.5f), IM_COL32(255, 150, 205, 70), 1.6f);
        } else if (hovered) {
            dl->AddRectFilled(t0, t1, col::kHover, 7.0f, ImDrawFlags_RoundCornersTop);
        }
        TextCenter(dl, tabFont, tabSize, V(x, t0.y), V(x + tw, t1.y),
                   sel ? col::kTextBright : (hovered ? col::kTextValue : col::kTextDim), label);
        x += tw + 4.0f;
    }

    // decorative "+" as in the reference tab strip
    const float plusX = x + 8.0f;
    if (plusX < tabEnd) {
        const float cy = (compact ? tabTop + tabH * 0.5f : wpos.y + metrics::kTitleH * 0.5f) + 1.0f;
        dl->AddLine(V(plusX - 5.0f, cy), V(plusX + 5.0f, cy), col::kTextDim, 1.4f);
        dl->AddLine(V(plusX, cy - 5.0f), V(plusX, cy + 5.0f), col::kTextDim, 1.4f);
    }

    // ---- window controls
    const ImU32 btnCol[3] = { col::kTextLabel, col::kTextLabel, col::kTextDim };
    const ImU32 btnColHover[3] = { col::kAccentBright, col::kAccentBright, col::kDanger };
    for (int i = 0; i < 3; ++i) {
        const float bx = wpos.x + w - 14.0f - (3 - i) * 30.0f;
        const ImVec2 b0 = V(bx, wpos.y + (metrics::kTitleH - 26.0f) * 0.5f);
        const ImVec2 b1 = V(bx + 26.0f, b0.y + 26.0f);

        char id[32];
        std::snprintf(id, sizeof(id), "##wbtn%d", i);
        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton(id, V(26.0f, 26.0f));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            const ImU32 hb = (i == 2) ? IM_COL32(228, 108, 108, 40) : IM_COL32(255, 255, 255, 10);
            dl->AddRectFilled(b0, b1, hb, metrics::kRadiusSmall);
        }

        const ImVec2 c = V((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f);
        const ImU32 cc = hovered ? btnColHover[i] : btnCol[i];
        if (i == 0) IconMinus(dl, c, cc);
        if (i == 1) IconSquare(dl, c, cc);
        if (i == 2) IconCross(dl, c, cc);

        if (ImGui::IsItemActivated()) {
            if (i == 0) { st.folded = !st.folded; }
            if (i == 1) { st.unlocked = !st.unlocked; }
            if (i == 2) { st.windowOpen = false; }
        }
    }
    // ---- drag area (the compact window fills the screen, so it never moves)
    if (compact) return;

    // Submitted last: an active tab/button already owns ActiveId, so pressing a
    // tab never starts a window drag. ImGuiWindowFlags_NoMove keeps ImGui's own
    // "drag anywhere" behaviour out of the way.
    ImGui::SetCursorScreenPos(V(wpos.x + 6.0f, wpos.y + 4.0f));
    ImGui::InvisibleButton("##title_drag", V(Max(1.0f, w - 12.0f - controlsW), metrics::kTitleH - 8.0f));
    if (ImGui::IsItemActivated()) {
        g_ui.dragging = true;
        g_ui.dragOffset = V(io.MousePos.x - wpos.x, io.MousePos.y - wpos.y);
    }
    if (g_ui.dragging && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImVec2 p = V(io.MousePos.x - g_ui.dragOffset.x, io.MousePos.y - g_ui.dragOffset.y);
        p.x = ClampF(p.x, 0.0f, Max(0.0f, io.DisplaySize.x - w));
        p.y = ClampF(p.y, 0.0f, Max(0.0f, io.DisplaySize.y - metrics::kTitleH));
        ImGui::SetWindowPos(p);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) g_ui.dragging = false;

    (void)bodyTop;
}

void DrawFooter(ImDrawList* dl, const Fonts& f, const Config& cfg, const ImVec2& wpos, float w, float h)
{
    const float y = wpos.y + h - metrics::kFooterH;
    dl->AddLine(V(wpos.x + metrics::kPad, y + 0.5f), V(wpos.x + w - metrics::kPad, y + 0.5f), col::kSeparator, 1.0f);

    const float midY = y + metrics::kFooterH * 0.5f;
    Text(dl, f.bold, f.boldSize, V(wpos.x + metrics::kPad, midY - f.boldSize * 0.5f), col::kPinkBright,
         cfg.footer ? cfg.footer : "");

    // the colored dots of the reference shot
    const int dotCount = (int)(sizeof(col::kDots) / sizeof(col::kDots[0]));
    const float r = 3.4f, step = 13.0f;
    float dx = wpos.x + w - metrics::kPad - (dotCount - 1) * step - r;
    for (int i = 0; i < dotCount; ++i) {
        dl->AddCircleFilled(V(dx + i * step, midY), r, col::kDots[i], 16);
    }

    // hint in the middle
    const char* hint = cfg.hint ? cfg.hint : "";
    Text(dl, f.small, f.smallSize, V(wpos.x + w * 0.5f - TextW(f.small, f.smallSize, hint) * 0.5f,
                                     midY - f.smallSize * 0.5f), col::kTextDim, hint);
}

// ====================================================== shared sections ======
// The building blocks of the menu. The desktop layout arranges them in two
// columns (sidebar on the left, dashboard on the right); the compact layout
// used on a phone stacks the very same functions in one scrollable column.

const float kArtRef = 16.0f;   // art is measured at this size, then scaled

struct BannerFit {
    int   rows = 0;
    float size = 0.0f;     // size the art is drawn at
    float line = 0.0f;     // line step
    float height = 0.0f;   // art + caption
};

// The banner is measured at a fixed reference size and then scaled so that it
// fills the column exactly - the block letters tile seamlessly at any width.
BannerFit FitBanner(const Fonts& f, float w)
{
    float artW = 1.0f;
    int   rows = 0;
    for (int i = 0; kBanner[i]; ++i) {
        artW = Max(artW, TextW(f.art, kArtRef, kBanner[i]));
        ++rows;
    }

    BannerFit fit;
    fit.rows = rows;
    fit.size = kArtRef * (w / artW);
    // Row step = the font's real line height, so block glyphs tile seamlessly.
    const float lineRatio = f.art ? (f.art->Ascent - f.art->Descent) / Max(1.0f, f.art->FontSize) : 1.2f;
    fit.line   = fit.size * lineRatio;
    fit.height = (float)rows * fit.line + 4.0f + 30.0f;
    return fit;
}

float DrawBannerArt(ImDrawList* dl, const Fonts& f, const ImVec2& pos, float w)
{
    const BannerFit fit = FitBanner(f, w);
    for (int i = 0; i < fit.rows; ++i) {
        const ImU32 c = (i < 2) ? col::kPinkBright : (i < 4 ? col::kPink : col::kPinkDim);
        Text(dl, f.art, fit.size, V(pos.x, pos.y + (float)i * fit.line), c, kBanner[i]);
    }
    float y = pos.y + (float)fit.rows * fit.line + 4.0f;

    // ---- caption under the banner
    dl->AddLine(V(pos.x, y + 8.0f), V(pos.x + w * 0.32f, y + 8.0f), IM_COL32(255, 150, 205, 70), 1.0f);
    Text(dl, f.small, f.smallSize, V(pos.x, y + 16.0f), col::kTextDim, "external toolkit");
    TextRight(dl, f.bold, f.boldSize, pos.x + w, y + 13.0f, col::kPink, "v2.0");
    y += 30.0f;
    return y - pos.y;
}

// DEVICE - static placeholder data, nothing is queried anywhere.
float DrawDeviceSection(ImDrawList* dl, const Fonts& f, const Config& cfg, const ImVec2& pos, float w)
{
    const float h = SectionHeight(4, metrics::kRowH);
    Section dev = BeginSection(dl, f, "DEVICE", pos, w, h);
    InfoRow(dl, f, V(dev.inner.x, dev.Row(metrics::kRowH)), dev.innerW, metrics::kRowH, "MODEL", "iPhone 15,2", -1.0f, nullptr);
    InfoRow(dl, f, V(dev.inner.x, dev.Row(metrics::kRowH)), dev.innerW, metrics::kRowH, "SYSTEM", "iOS 17.4 \xc2\xb7 rootless", -1.0f, nullptr);
    InfoRow(dl, f, V(dev.inner.x, dev.Row(metrics::kRowH)), dev.innerW, metrics::kRowH, "RENDER", "Metal \xc2\xb7 ImGui 1.83", -1.0f, nullptr);
    InfoRow(dl, f, V(dev.inner.x, dev.Row(metrics::kRowH)), dev.innerW, metrics::kRowH, "BUILD", cfg.build ? cfg.build : "-", -1.0f, nullptr);
    return h;
}

// MODULES - mirrors what is toggled in the tabs.
float DrawModulesSection(ImDrawList* dl, const Fonts& f, const Features& st, const ImVec2& pos, float w)
{
    const int espOn  = (st.espBox ? 1 : 0) + (st.espSkeleton ? 1 : 0) + (st.espLine ? 1 : 0) + (st.espHealthBar ? 1 : 0)
                     + (st.espName ? 1 : 0) + (st.espDistance ? 1 : 0) + (st.espIcon ? 1 : 0) + (st.espTeamCheck ? 1 : 0);
    const int visOn  = (st.visMapHack ? 1 : 0) + (st.visSkillCd ? 1 : 0) + (st.visSpellCd ? 1 : 0)
                     + (st.visDroneView ? 1 : 0) + (st.visZoomOut ? 1 : 0);
    const int miscOn = (st.miscAntiBan ? 1 : 0) + (st.miscBypass ? 1 : 0) + (st.miscRetri ? 1 : 0)
                     + (st.miscNoGrass ? 1 : 0) + (st.miscRecall ? 1 : 0);

    char espBuf[32], aimBuf[32], visBuf[32], miscBuf[32];
    std::snprintf(espBuf,  sizeof(espBuf),  "%d / 8 active", espOn);
    std::snprintf(aimBuf,  sizeof(aimBuf),  "%s", st.aimEnabled ? "armed" : "idle");
    std::snprintf(visBuf,  sizeof(visBuf),  "%d / 5 active", visOn);
    std::snprintf(miscBuf, sizeof(miscBuf), "%d / 5 active", miscOn);

    const float h = SectionHeight(4, metrics::kRowH);
    Section mod = BeginSection(dl, f, "MODULES", pos, w, h);
    InfoRow(dl, f, V(mod.inner.x, mod.Row(metrics::kRowH)), mod.innerW, metrics::kRowH, "ESP",    espBuf,  (float)espOn / 8.0f,  nullptr);
    InfoRow(dl, f, V(mod.inner.x, mod.Row(metrics::kRowH)), mod.innerW, metrics::kRowH, "AIM",    aimBuf,  st.aimEnabled ? 1.0f : 0.0f, nullptr);
    InfoRow(dl, f, V(mod.inner.x, mod.Row(metrics::kRowH)), mod.innerW, metrics::kRowH, "VISUAL", visBuf,  (float)visOn / 5.0f,  nullptr);
    InfoRow(dl, f, V(mod.inner.x, mod.Row(metrics::kRowH)), mod.innerW, metrics::kRowH, "MISC",   miscBuf, (float)miscOn / 5.0f, nullptr);
    return h;
}

// LOG console, clipped to exactly `h` pixels.
float DrawLogSection(ImDrawList* dl, const Fonts& f, const ImVec2& pos, float w, float h)
{
    Section log = BeginSection(dl, f, "LOG", pos, w, h);
    const float lineH = f.smallSize * 1.62f;
    const int   fits  = (int)((h - metrics::kSectionHead - metrics::kSectionPad * 2.0f) / lineH);
    dl->PushClipRect(log.inner, V(log.max.x - 2.0f, log.max.y - 4.0f), true);
    for (int i = 0; i < fits; ++i) {
        const LogEntry* e = LogAt(fits - 1 - i);   // newest entry goes to the bottom
        if (!e) continue;
        char line[128];
        std::snprintf(line, sizeof(line), "[%s] %s", e->time, e->text);
        Text(dl, f.small, f.smallSize,
             V(log.inner.x, log.inner.y + (float)(fits - 1 - i) * lineH),
             (i == 0) ? col::kAccentBright : col::kTextDim, line);
    }
    dl->PopClipRect();
    return h;
}

float DrawLoginSection(ImDrawList* dl, const Fonts& f, const Config& cfg, const ImVec2& pos, float w, const char* startStamp)
{
    const float h = SectionHeight(1, metrics::kRowH * 2.0f);
    Section login = BeginSection(dl, f, "LOGIN", pos, w, h);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s // %s", cfg.account ? cfg.account : "-", startStamp);
    InfoRow(dl, f, V(login.inner.x, login.Row(metrics::kRowH * 2.0f)), login.innerW, metrics::kRowH * 2.0f,
            "ACCOUNT", buf, -1.0f, nullptr);
    return h;
}

float DrawUptimeSection(ImDrawList* dl, const Fonts& f, const ImVec2& pos, float w,
                        const char* uptime, const char* nowStamp)
{
    const float h = SectionHeight(2, metrics::kRowH);
    Section up = BeginSection(dl, f, "UPTIME / DATE", pos, w, h);
    InfoRow(dl, f, V(up.inner.x, up.Row(metrics::kRowH)), up.innerW, metrics::kRowH, "UPTIME", uptime, -1.0f, nullptr);
    InfoRow(dl, f, V(up.inner.x, up.Row(metrics::kRowH)), up.innerW, metrics::kRowH, "DATE", nowStamp, -1.0f, nullptr);
    return h;
}

// STATUS - the fastfetch block of the reference. Placeholder numbers as well.
float DrawStatusSection(ImDrawList* dl, const Fonts& f, const ImVec2& pos, float w)
{
    struct StatusRow { const char* label; const char* value; float pct; const char* pctText; };
    const StatusRow rows[] = {
        { "CPU",   "44 % \xc2\xb7 6 cores",        0.44f, "44%"  },
        { "GPU",   "Apple A17 Pro \xc2\xb7 61 %",  0.61f, "61%"  },
        { "FPS",   "120 / 120 fps",                1.00f, "100%" },
        { "PING",  "18 ms \xc2\xb7 jitter 3 ms",   0.12f, "12%"  },
        { "RAM",   "12.61 GiB / 15.89 GiB",        0.79f, "79%"  },
        { "VRAM",  "3.42 GiB / 8.00 GiB",          0.43f, "43%"  },
    };
    const int rowCount = (int)(sizeof(rows) / sizeof(rows[0]));
    const float h = SectionHeight(rowCount, metrics::kRowH);
    Section status = BeginSection(dl, f, "STATUS", pos, w, h);
    for (int i = 0; i < rowCount; ++i)
        InfoRow(dl, f, V(status.inner.x, status.Row(metrics::kRowH)), status.innerW, metrics::kRowH,
                rows[i].label, rows[i].value, rows[i].pct, rows[i].pctText);
    return h;
}

// The rows of the active tab. maxH <= 0 asks for the natural height.
float DrawActiveTabPanel(ImDrawList* dl, const Fonts& f, Features& st, const ImVec2& pos, float w, float maxH)
{
    const TabSpec& tab = kTabs[ClampI(st.tab, 0, kTabCount - 1)];
    const float colGap = 12.0f, rowGap = 8.0f;
    const float contentH = TabContentHeight(st.tab, w - metrics::kSectionPad * 2.0f, colGap, rowGap, f);
    const float wantedH  = metrics::kSectionHead + metrics::kSectionPad * 2.0f + contentH;
    const float h = (maxH > 0.0f) ? Min(wantedH, Max(maxH, 80.0f)) : wantedH;

    Section panel = BeginSection(dl, f, tab.name, pos, w, h);
    TextRight(dl, f.small, f.smallSize, panel.max.x - metrics::kSectionPad,
              panel.min.y - f.smallSize * 0.5f - 1.0f, col::kTextDim, tab.caption);

    dl->PushClipRect(panel.inner, V(panel.max.x, panel.max.y - 4.0f), true);
    DrawTabContent(dl, f, st.tab, panel.inner, panel.innerW, panel.max.y - 6.0f, st);
    dl->PopClipRect();
    return h;
}

float DrawGreeting(ImDrawList* dl, const Fonts& f, const Config& cfg, const ImVec2& pos, float w)
{
    float x = pos.x;
    Text(dl, f.title, f.titleSize, V(x, pos.y + 2.0f), col::kAccent, cfg.greeting ? cfg.greeting : "Hey,");
    x += TextW(f.title, f.titleSize, cfg.greeting ? cfg.greeting : "Hey,") + 8.0f;
    Text(dl, f.title, f.titleSize, V(x, pos.y + 2.0f), col::kTextBright, cfg.account ? cfg.account : "");

    // pulsing online chip on the right
    const float pulse = 0.55f + 0.45f * (float)std::sin(ImGui::GetTime() * 2.2);
    const ImU32 dotCol = IM_COL32(113, 220, 120, (int)(90 + 150 * pulse));
    const char* chip = "ATTACHED";
    const float chipW = TextW(f.small, f.smallSize, chip);
    const float chipRight = pos.x + w - 118.0f;   // keep clear of the title strip buttons
    Text(dl, f.small, f.smallSize, V(chipRight - chipW, pos.y + 4.0f), col::kTextDim, chip);
    dl->AddCircleFilled(V(chipRight + 8.0f, pos.y + 9.0f), 3.4f, dotCol, 16);

    return f.titleSize + 14.0f;
}

// ============================================================== sidebar ======
void DrawSidebar(ImDrawList* dl, const Fonts& f, const Config& cfg, const Features& st,
                 const ImVec2& pos, float w, float h)
{
    float y = pos.y;
    y += DrawBannerArt(dl, f, V(pos.x, y), w);
    y += DrawDeviceSection(dl, f, cfg, V(pos.x, y), w) + metrics::kSectionGap;
    y += DrawModulesSection(dl, f, st, V(pos.x, y), w) + metrics::kSectionGap;

    // ---- log console: everything that is left in the column
    const float logSpace = pos.y + h - y;
    if (logSpace >= 74.0f) {
        DrawLogSection(dl, f, V(pos.x, y), w, logSpace);
        y += logSpace;
    }

    // ---- emblem: only drawn when a resize leaves a comfortable gap
    const BannerFit fit = FitBanner(f, w);
    float emW = 1.0f;
    int   emRows = 0;
    for (int i = 0; kEmblem[i]; ++i) {
        emW = Max(emW, TextW(f.art, kArtRef, kEmblem[i]));
        ++emRows;
    }
    const float emSpace = pos.y + h - y - 10.0f;
    if (emSpace < 200.0f) return;
    const float emFit = emSpace / ((float)emRows * 1.18f);
    const float emSize = Min(Min(kArtRef * (w / emW), fit.size * 1.05f), emFit);
    const float emLine = emSize * (fit.size > 0.0f ? fit.line / fit.size : 1.2f);
    const float emH = (float)emRows * emLine;
    const float emWidth = TextW(f.art, emSize, kEmblem[0]);
    if (emSize >= 7.0f && y + emH < pos.y + h) {
        const float ex = pos.x + (w - emWidth) * 0.5f;
        for (int i = 0; i < emRows; ++i) {
            const ImU32 c = (i == 4) ? col::kPinkBright : col::kPinkDim;
            Text(dl, f.art, emSize, V(ex, y + (float)i * emLine), c, kEmblem[i]);
        }
    }
}

// ============================================================== dashboard ====
void DrawDashboard(ImDrawList* dl, const Fonts& f, const Config& cfg, Features& st,
                   const ImVec2& pos, float w, float h,
                   const char* uptime, const char* startStamp, const char* nowStamp)
{
    float y = pos.y;
    y += DrawGreeting(dl, f, cfg, V(pos.x, y), w);

    // ---- LOGIN + UPTIME / DATE, side by side
    const float gap = metrics::kColumnGap;
    const float halfW = (w - gap) * 0.5f;
    const float loginH = DrawLoginSection(dl, f, cfg, V(pos.x, y), halfW, startStamp);
    DrawUptimeSection(dl, f, V(pos.x + halfW + gap, y), halfW, uptime, nowStamp);
    y += loginH + metrics::kSectionGap;

    y += DrawStatusSection(dl, f, V(pos.x, y), w) + metrics::kSectionGap;

    // ---- the active tab fills whatever is left of the column
    const float availH = pos.y + h - y - metrics::kSectionGap;
    y += DrawActiveTabPanel(dl, f, st, V(pos.x, y), w, availH) + metrics::kSectionGap;
    (void)y;
}

// ====================================================== compact (phone) ======
// One column that is scrolled by dragging - what the menu looks like on a
// phone in portrait. Exactly the same sections, just stacked.
const float kCompactLogH = 132.0f;

float CompactContentHeight(const Fonts& f, const Features& st, float w)
{
    float h = 0.0f;
    h += FitBanner(f, w).height + metrics::kSectionGap;
    h += f.titleSize + 14.0f + metrics::kSectionGap;
    h += SectionHeight(1, metrics::kRowH * 2.0f) + metrics::kSectionGap;
    h += SectionHeight(2, metrics::kRowH) + metrics::kSectionGap;
    h += SectionHeight(6, metrics::kRowH) + metrics::kSectionGap;
    h += metrics::kSectionHead + metrics::kSectionPad * 2.0f
       + TabContentHeight(st.tab, w - metrics::kSectionPad * 2.0f, 12.0f, 8.0f, f) + metrics::kSectionGap;
    h += SectionHeight(4, metrics::kRowH) + metrics::kSectionGap;
    h += SectionHeight(4, metrics::kRowH) + metrics::kSectionGap;
    h += kCompactLogH;
    return h;
}

void DrawCompactBody(ImDrawList* dl, const Fonts& f, Config& cfg, Features& st,
                     const ImVec2& pos, float w, float viewH,
                     const char* uptime, const char* startStamp, const char* nowStamp)
{
    const float contentH  = CompactContentHeight(f, st, w);
    const float maxScroll = Max(0.0f, contentH - viewH);
    g_ui.scroll = ClampF(g_ui.scroll, 0.0f, maxScroll);

    float y = pos.y - g_ui.scroll;
    y += DrawBannerArt(dl, f, V(pos.x, y), w) + metrics::kSectionGap;
    y += DrawGreeting(dl, f, cfg, V(pos.x, y), w) + metrics::kSectionGap;
    y += DrawLoginSection(dl, f, cfg, V(pos.x, y), w, startStamp) + metrics::kSectionGap;
    y += DrawUptimeSection(dl, f, V(pos.x, y), w, uptime, nowStamp) + metrics::kSectionGap;
    y += DrawStatusSection(dl, f, V(pos.x, y), w) + metrics::kSectionGap;
    y += DrawActiveTabPanel(dl, f, st, V(pos.x, y), w, 0.0f) + metrics::kSectionGap;
    y += DrawDeviceSection(dl, f, cfg, V(pos.x, y), w) + metrics::kSectionGap;
    y += DrawModulesSection(dl, f, st, V(pos.x, y), w) + metrics::kSectionGap;
    y += DrawLogSection(dl, f, V(pos.x, y), w, kCompactLogH);

    // ---- touch scrolling: any drag that no widget claimed moves the page
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##page_scroll", V(w, viewH));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        g_ui.scroll -= ImGui::GetIO().MouseDelta.y;
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::GetIO().MouseWheel != 0.0f)
        g_ui.scroll -= ImGui::GetIO().MouseWheel * 48.0f;
    g_ui.scroll = ClampF(g_ui.scroll, 0.0f, maxScroll);

    // ---- thin scrollbar, only when there is something to scroll
    if (maxScroll > 1.0f) {
        const float trackX = pos.x + w + metrics::kPad - 6.0f;
        const float thumbH = Max(36.0f, viewH * (viewH / contentH));
        const float thumbY = pos.y + (viewH - thumbH) * (g_ui.scroll / maxScroll);
        dl->AddRectFilled(V(trackX, thumbY), V(trackX + 3.0f, thumbY + thumbH), IM_COL32(163, 255, 168, 110), 2.0f);
    }
    (void)y;
}

// ================================================================ menu =======
void DrawMenuInternal(Config& cfg, Fonts& fonts, Features& st)
{
    ImGuiIO& io = ImGui::GetIO();

    if (g_ui.startedAt == 0.0) {
        g_ui.startedAt = ImGui::GetTime();
        FormatDateTime(g_ui.startedDate, (int)sizeof(g_ui.startedDate));
        LogPush("spectre ui initialised");
        LogPush("imgui %s ready", IMGUI_VERSION);
        LogPush("waiting for match ...");
    }

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoSavedSettings |
        ((st.unlocked && !cfg.compact) ? 0 : ImGuiWindowFlags_NoResize);

    if (cfg.compact) {
        // Phone layout: the window covers the whole canvas and the content
        // scrolls inside it, so there is nothing to place or resize.
        g_ui.placed = true;
        ImGui::SetNextWindowPos(V(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(V(io.DisplaySize.x, st.folded ? TitleStripHeight(cfg) : io.DisplaySize.y), ImGuiCond_Always);
    } else {
        // First use: centre the window on the screen.
        if (!g_ui.placed) {
            g_ui.placed = true;
            ImGui::SetNextWindowPos(V(Max(0.0f, (io.DisplaySize.x - cfg.width) * 0.5f),
                                      Max(0.0f, (io.DisplaySize.y - cfg.height) * 0.5f)), ImGuiCond_Always);
        }

        // ---- size / folding
        const float foldedH = metrics::kTitleH + metrics::kPad * 2.0f;
        if (st.folded && !g_ui.wasFolded) {
            g_ui.unfoldedSize = ImGui::GetWindowSize();
            g_ui.wasFolded = true;
        }
        if (!st.folded && g_ui.wasFolded) {
            ImGui::SetNextWindowSize(g_ui.unfoldedSize.x > 1.0f ? g_ui.unfoldedSize : V(cfg.width, cfg.height), ImGuiCond_Always);
            g_ui.wasFolded = false;
        }
        if (st.folded) ImGui::SetNextWindowSize(V(cfg.width, foldedH), ImGuiCond_Always);
        else if (!st.unlocked) ImGui::SetNextWindowSize(V(cfg.width, cfg.height), ImGuiCond_Always);

        if (!st.folded) {
            const float minW = 900.0f, minH = 420.0f;
            ImGui::SetNextWindowSizeConstraints(V(minW, minH), V(FLT_MAX, FLT_MAX));
        }
    }

    // In the phone layout the window covers the whole canvas: nothing shows
    // through it, so it is drawn opaque (a straight store instead of a blend).
    if (cfg.compact) ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.031f, 0.067f, 0.039f, 1.0f));

    ImGui::Begin(cfg.windowTitle, nullptr, flags);

    const ImVec2 wpos = ImGui::GetWindowPos();
    const ImVec2 wsize = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ---- soft drop shadow, drawn behind everything
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    // Two layers instead of sixteen: a software rasterizer pays for every one
    // of those full-window fills, and on a phone that is the whole frame budget.
    if (!st.folded && !cfg.compact) {
        const float layer[2] = { 14.0f, 7.0f };
        const int   alpha[2] = { 7, 13 };
        for (int i = 0; i < 2; ++i) {
            bg->AddRectFilled(V(wpos.x - layer[i], wpos.y - layer[i] + 7.0f),
                              V(wpos.x + wsize.x + layer[i], wpos.y + wsize.y + layer[i] + 7.0f),
                              IM_COL32(0, 0, 0, alpha[i]), metrics::kRadiusWin + layer[i]);
        }
    }

    // ---- window body
    if (!st.folded) {
        const float stripH  = TitleStripHeight(cfg);
        const float bodyTop = wpos.y + stripH + metrics::kPad;
        const float bodyH   = wsize.y - stripH - metrics::kFooterH - metrics::kPad;

        char uptime[64];
        const int secs = (int)(ImGui::GetTime() - g_ui.startedAt);
        std::snprintf(uptime, sizeof(uptime), "%d hours, %d mins", secs / 3600, (secs / 60) % 60);

        char nowStamp[32];
        FormatDateTime(nowStamp, (int)sizeof(nowStamp));

        if (cfg.compact) {
            // ImGui::PushClipRect (not the draw list one) also clips the hit
            // testing, so rows scrolled out of view cannot be clicked.
            ImGui::PushClipRect(V(wpos.x, wpos.y + stripH),
                                V(wpos.x + wsize.x, wpos.y + wsize.y - metrics::kFooterH), true);
            DrawCompactBody(dl, fonts, cfg, st, V(wpos.x + metrics::kPad, bodyTop),
                            wsize.x - metrics::kPad * 2.0f, bodyH, uptime, g_ui.startedDate, nowStamp);
            ImGui::PopClipRect();
        } else {
            const float sideX  = wpos.x + metrics::kPad;
            const float rightX = sideX + metrics::kSidebarW + metrics::kColumnGap;
            const float rightW = wpos.x + wsize.x - metrics::kPad - rightX;

            dl->PushClipRect(wpos, V(wpos.x + wsize.x, wpos.y + wsize.y - metrics::kFooterH), true);
            DrawSidebar(dl, fonts, cfg, st, V(sideX, bodyTop), metrics::kSidebarW, bodyH);
            DrawDashboard(dl, fonts, cfg, st, V(rightX, bodyTop), rightW, bodyH, uptime, g_ui.startedDate, nowStamp);
            dl->PopClipRect();
        }
    }

    DrawTitleStrip(dl, fonts, cfg, st, wpos, wsize.x, wpos.y + metrics::kTitleH);

    if (!st.folded) DrawFooter(dl, fonts, cfg, wpos, wsize.x, wsize.y);

    ImGui::End();
    if (cfg.compact) ImGui::PopStyleColor();
    (void)io;
}

}  // namespace

// ---------------------------------------------------------------- public -----
void PushLog(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    // Reuse the internal buffer through a small trampoline.
    char text[96];
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    LogPush("%s", text);
}

Fonts LoadFonts(const Config& cfg)
{
    Fonts f;
    ImGuiIO& io = ImGui::GetIO();

    // Ranges: latin, cyrillic, arrows, box drawing, blocks, geometric shapes.
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,
        0x0400, 0x04FF,
        0x2190, 0x21FF,
        0x2500, 0x257F,
        0x2580, 0x259F,
        0x25A0, 0x25FF,
        0x2600, 0x26FF,
        0,
    };

    // The atlas is rasterized at `pixelDensity` times the design size while the
    // menu keeps drawing at the design size (see Menu.h). On a phone that means
    // big *and* crisp text instead of an upscaled framebuffer; on the desktop
    // density is 1 and nothing changes.
    const float density = cfg.pixelDensity > 0.05f ? cfg.pixelDensity : 1.0f;

    ImFontConfig fc;
    // Glyphs are already supersampled by `density`, so 1x oversampling is enough.
    fc.OversampleH = density > 1.25f ? 1 : 2;
    fc.OversampleV = density > 1.25f ? 1 : 2;
    fc.PixelSnapH = true;

    if (cfg.fontRegular) {
        f.small = io.Fonts->AddFontFromFileTTF(cfg.fontRegular, f.smallSize * density, &fc, ranges);
        f.body  = io.Fonts->AddFontFromFileTTF(cfg.fontRegular, f.bodySize  * density, &fc, ranges);
        f.art   = f.body;
    }
    if (cfg.fontBold) {
        ImFontConfig fb = fc;
        f.bold  = io.Fonts->AddFontFromFileTTF(cfg.fontBold, f.boldSize  * density, &fb, ranges);
        f.title = io.Fonts->AddFontFromFileTTF(cfg.fontBold, f.titleSize * density, &fb, ranges);
    }
    if (cfg.fontSmall) {
        f.small = io.Fonts->AddFontFromFileTTF(cfg.fontSmall, f.smallSize * density, &fc, ranges);
    }

    // Fallbacks: the built-in ImGui font has a fixed size of its own, so when it
    // is used the drawing size has to follow it.
    if (!f.body)  { f.body  = io.Fonts->AddFontDefault(); f.bodySize  = f.body->FontSize; }
    if (!f.bold)  { f.bold  = f.body;  f.boldSize  = f.bodySize; }
    if (!f.small) { f.small = f.body;  f.smallSize = f.bodySize * 0.85f; }
    if (!f.title) { f.title = f.bold;  f.titleSize = f.boldSize * 1.13f; }
    if (!f.art)   f.art = f.body;

    return f;
}

void DrawMenu(Config& cfg, Fonts& fonts, Features& features)
{
    if (!features.windowOpen) return;
    DrawMenuInternal(cfg, fonts, features);
}

}  // namespace mlbb
