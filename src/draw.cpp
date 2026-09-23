#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include "draw.h"
#include "GraphicsManager.h"
#include "zh_Font.h"
#include "cheat_state.h"
#include "terminal_theme.h"

std::unique_ptr<AndroidImgui> graphics;
ANativeWindow *window = nullptr;
android::ANativeWindowCreator::DisplayInfo displayInfo{};
int abs_ScreenX = 0, abs_ScreenY = 0;
int native_window_screen_x = 0, native_window_screen_y = 0;
float surface_screen_x = 0.0f, surface_screen_y = 0.0f;

CheatConfig g_config;

namespace {
using Clock = std::chrono::steady_clock;

// Dimensions of the logical overlay window matching the widescreen terminal aspect ratio
constexpr float kBaseWidth = 980.0f;
constexpr float kBaseHeight = 640.0f;
constexpr float kTitleHeight = 52.0f;
constexpr float kSideMargin = 32.0f;
constexpr float kTopMargin = 48.0f;
constexpr float kBottomMargin = 32.0f;
constexpr float kProducerWidth = kSideMargin + kBaseWidth + kSideMargin;
constexpr float kProducerHeight = kTopMargin + kBaseHeight + kBottomMargin;
constexpr float kMinScale = 0.50f;

Clock::time_point last_display_query{};
unsigned int last_sequence = 0;

// Dragging & Resizing state
bool is_dragging = false;
bool is_dragging_bubble = false;
enum class ResizeMode { None, LeftBottom, RightBottom };
ResizeMode resize_mode = ResizeMode::None;

float touch_x = -1.0f, touch_y = -1.0f;
bool touch_down = false;
float layer_scale = 1.0f;
float resize_start_scale = 1.0f;
float resize_touch_start_x = 0.0f, resize_touch_start_y = 0.0f;
float resize_fixed_x = 0.0f, resize_fixed_y = 0.0f;
ImVec2 drag_touch_start{};
ImVec2 drag_surface_start{};

// Bubble mode state
constexpr float kBubbleSize = 64.0f;
float bubble_x = 40.0f;
float bubble_y = 120.0f;

int LogicalLayerHeight() {
    if (g_config.minimized_bubble) {
        return (int)std::lround(kBubbleSize + 20.0f);
    }
    return (int)std::lround(kTopMargin + (g_config.collapsed ? kTitleHeight : kBaseHeight + kBottomMargin));
}

void ApplyLayerGeometry() {
    const int crop_h = LogicalLayerHeight();
    const int crop_w = g_config.minimized_bubble ? (int)std::lround(kBubbleSize + 20.0f) : (int)std::lround(kProducerWidth);
    const int layer_w = std::max(1, (int)std::lround(crop_w * layer_scale));
    const int layer_h = std::max(1, (int)std::lround(crop_h * layer_scale));
    native_window_screen_x = layer_w;
    native_window_screen_y = layer_h;
    android::ANativeWindowCreator::SetLayerGeometry(window, crop_w, crop_h, layer_scale);
}
}

void init_My_drawdata() {
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    io.FontDefault = io.Fonts->AddFontFromMemoryTTF((void *)OPPOSans_H, OPPOSans_H_size, 22.0f);
    if (!io.FontDefault) io.FontDefault = io.Fonts->AddFontDefault();

    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0, 0);
    style.WindowBorderSize = 0.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.ItemSpacing = ImVec2(10, 8);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_Border] = ImVec4(0.18f, 0.24f, 0.33f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.12f, 0.16f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.20f, 0.28f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.28f, 0.38f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.97f, 0.56f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 1.0f, 0.65f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.97f, 0.56f, 1.0f);

    layer_scale = std::min({1.0f,
        (abs_ScreenX - 32.0f) / kProducerWidth,
        (abs_ScreenY - 48.0f) / kProducerHeight});
    layer_scale = std::max(kMinScale, layer_scale);
    ApplyLayerGeometry();

    surface_screen_x = std::max(0.0f, (abs_ScreenX - native_window_screen_x) * 0.5f);
    surface_screen_y = std::max(0.0f, (abs_ScreenY - native_window_screen_y) * 0.5f);
    android::ANativeWindowCreator::SetPosition(window, surface_screen_x, surface_screen_y);

    // Initial console log entries
    g_config.console_log.push_back("[*] Android Root Native Overlay loaded.");
    g_config.console_log.push_back("[+] SurfaceFlinger overlay hooked via libgui.so.");
    g_config.console_log.push_back("[+] Evdev non-exclusive touch dispatcher initialized.");
    g_config.console_log.push_back("[*] Target: Mobile Legends Bang Bang (com.mobile.legends)");
    g_config.console_log.push_back("[✓] UID: 0 (Root Privileges Confirmed).");
    g_config.console_log.push_back("[✓] GUI Ready. Ready for command input.");
}

void screen_config() {
    const auto now = Clock::now();
    if (last_display_query != Clock::time_point{} &&
        now - last_display_query < std::chrono::milliseconds(250)) return;
    displayInfo = android::ANativeWindowCreator::GetDisplayInfo();
    last_display_query = now;
}

void drawBegin() {
    screen_config();
    Touch::setOrientation(displayInfo.orientation);
    ImGuiIO &io = ImGui::GetIO();
    Touch::TouchSnapshot snapshot{};
    while (Touch::GetSnapshot(&snapshot)) {
        if (!snapshot.valid || snapshot.sequence == last_sequence) continue;
        last_sequence = snapshot.sequence;
        touch_x = snapshot.x;
        touch_y = snapshot.y;
        touch_down = snapshot.down;
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent((snapshot.x - surface_screen_x) / layer_scale,
                            (snapshot.y - surface_screen_y) / layer_scale);
        io.AddMouseButtonEvent(0, snapshot.down);
    }
}

// -------------------------------------------------------------
// Sub-views rendering
// -------------------------------------------------------------

static void RenderTerminalView() {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 content_start = ImGui::GetCursorScreenPos();
    float total_w = kBaseWidth - 48.0f;

    // Subtitle top-left
    dl->AddText(content_start, CyberTheme::ColLime, "PowerShell 7.6.3");

    float col1_w = 400.0f;
    float col2_x = content_start.x + col1_w + 24.0f;
    float col2_w = total_w - col1_w - 24.0f;

    // LEFT COLUMN: ASCII Art in Pink
    float art_start_y = content_start.y + 36.0f;
    for (int i = 0; i < CyberTheme::ASCII_ART_LINE_COUNT; ++i) {
        dl->AddText(ImVec2(content_start.x + 8.0f, art_start_y + i * 20.0f),
                    CyberTheme::ColPink, CyberTheme::ASCII_ART_LINES[i]);
    }

    // RIGHT COLUMN: Hey, Vanil and Info Boxes
    float cur_y = content_start.y + 12.0f;
    
    // Greeting
    std::string greeting = "Hey, " + g_config.user_name;
    dl->AddText(ImVec2(col2_x, cur_y), CyberTheme::ColLime, greeting.c_str());
    cur_y += 34.0f;

    // BOX 1: Hardware
    float box1_h = 192.0f;
    CyberTheme::BeginCyberBox(dl, ImVec2(col2_x, cur_y), ImVec2(col2_x + col2_w, cur_y + box1_h), "Hardware");
    
    float item_y = cur_y + 18.0f;
    float label_x = col2_x + 16.0f;
    float val_x = col2_x + 96.0f;
    float bar_x = col2_x + 280.0f;
    float bar_w = 110.0f;

    // CPU
    dl->AddText(ImVec2(label_x, item_y), CyberTheme::ColLime, "CPU");
    dl->AddText(ImVec2(val_x, item_y), CyberTheme::ColWhite, g_config.cpu_model.c_str());
    item_y += 24.0f;

    // GPU
    dl->AddText(ImVec2(label_x, item_y), CyberTheme::ColLime, "GPU");
    dl->AddText(ImVec2(val_x, item_y), CyberTheme::ColWhite, g_config.gpu_model.c_str());
    item_y += 24.0f;

    // RAM
    char ram_txt[64];
    std::snprintf(ram_txt, sizeof(ram_txt), "%.2f GiB / %.2f GiB", g_config.ram_used_gb, g_config.ram_total_gb);
    dl->AddText(ImVec2(label_x, item_y), CyberTheme::ColLime, "RAM");
    dl->AddText(ImVec2(val_x, item_y), CyberTheme::ColWhite, ram_txt);
    CyberTheme::RenderTerminalProgressBar(dl, ImVec2(bar_x, item_y + 2.0f), bar_w, 14.0f, g_config.ram_used_gb / g_config.ram_total_gb, "79%");
    item_y += 24.0f;

    // SWAP
    char swap_txt[64];
    std::snprintf(swap_txt, sizeof(swap_txt), "%.2f MiB / %.2f GiB", g_config.swap_used_mb, g_config.swap_total_mb / 1024.0f);
    dl->AddText(ImVec2(label_x, item_y), CyberTheme::ColLime, "SWAP");
    dl->AddText(ImVec2(val_x, item_y), CyberTheme::ColWhite, swap_txt);
    CyberTheme::RenderTerminalProgressBar(dl, ImVec2(bar_x, item_y + 2.0f), bar_w, 14.0f, 0.03f, "3%");
    item_y += 24.0f;

    // DRIVE C:\ and D:\
    dl->AddText(ImVec2(label_x, item_y), CyberTheme::ColLime, "DRIVE");
    dl->AddText(ImVec2(val_x, item_y), CyberTheme::ColWhite, "C:\\ 313.56 GiB / 464.79 GiB");
    CyberTheme::RenderTerminalProgressBar(dl, ImVec2(bar_x, item_y + 2.0f), bar_w, 14.0f, 0.67f, "67%");
    item_y += 24.0f;

    dl->AddText(ImVec2(label_x, item_y), CyberTheme::ColLime, "DRIVE");
    dl->AddText(ImVec2(val_x, item_y), CyberTheme::ColWhite, "D:\\ 541.77 GiB / 931.51 GiB");
    CyberTheme::RenderTerminalProgressBar(dl, ImVec2(bar_x, item_y + 2.0f), bar_w, 14.0f, 0.58f, "58%");
    item_y += 24.0f;

    dl->AddText(ImVec2(label_x, item_y), CyberTheme::ColLime, "DRIVE");
    dl->AddText(ImVec2(val_x, item_y), CyberTheme::ColWhite, "E:\\ 267.97 GiB / 443.23 GiB");
    CyberTheme::RenderTerminalProgressBar(dl, ImVec2(bar_x, item_y + 2.0f), bar_w, 14.0f, 0.60f, "60%");
    
    cur_y += box1_h + 14.0f;

    // BOX 2: Session
    float box2_h = 52.0f;
    CyberTheme::BeginCyberBox(dl, ImVec2(col2_x, cur_y), ImVec2(col2_x + col2_w, cur_y + box2_h), "Session");
    dl->AddText(ImVec2(label_x, cur_y + 16.0f), CyberTheme::ColLime, "LOGIN");
    std::string sess_str = g_config.user_name + " // " + g_config.login_timestamp;
    dl->AddText(ImVec2(val_x, cur_y + 16.0f), CyberTheme::ColWhite, sess_str.c_str());
    cur_y += box2_h + 14.0f;

    // BOX 3: Uptime / Date
    float box3_h = 74.0f;
    CyberTheme::BeginCyberBox(dl, ImVec2(col2_x, cur_y), ImVec2(col2_x + col2_w, cur_y + box3_h), "Uptime / Date");
    dl->AddText(ImVec2(label_x, cur_y + 16.0f), CyberTheme::ColYellow, "UPTIME");
    char up_str[64];
    std::snprintf(up_str, sizeof(up_str), "%d hours, %d mins", g_config.uptime_hours, g_config.uptime_mins);
    dl->AddText(ImVec2(val_x, cur_y + 16.0f), CyberTheme::ColWhite, up_str);

    dl->AddText(ImVec2(label_x, cur_y + 42.0f), CyberTheme::ColYellow, "DATE");
    dl->AddText(ImVec2(val_x, cur_y + 42.0f), CyberTheme::ColWhite, "2026-07-16 20:42:32");
    cur_y += box3_h + 14.0f;

    // Quote: Bonsoir! Elliot.
    dl->AddText(ImVec2(col2_x, cur_y), CyberTheme::ColOrange, g_config.quote_text.c_str());
    cur_y += 28.0f;

    // Color dots palette (7 dots)
    const ImU32 dot_cols[7] = {
        CyberTheme::ColWhite,
        CyberTheme::ColCyan,
        CyberTheme::ColMagenta,
        CyberTheme::ColBlue,
        CyberTheme::ColYellow,
        CyberTheme::ColLime,
        CyberTheme::ColRed
    };
    for (int d = 0; d < 7; ++d) {
        dl->AddCircleFilled(ImVec2(col2_x + 8.0f + d * 18.0f, cur_y + 4.0f), 5.0f, dot_cols[d]);
    }

    // Bottom prompt line
    float prompt_y = content_start.y + kBaseHeight - 146.0f;
    std::string prompt_str = "PS C:\\Users\\" + g_config.user_name + "> ";
    dl->AddText(ImVec2(content_start.x + 8.0f, prompt_y), CyberTheme::ColLime, prompt_str.c_str());
    
    // Interactive prompt input
    ImGui::SetCursorScreenPos(ImVec2(content_start.x + 190.0f, prompt_y - 4.0f));
    ImGui::PushItemWidth(380.0f);
    if (ImGui::InputText("##console_in", g_config.console_input_buf, sizeof(g_config.console_input_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (g_config.console_input_buf[0] != '\0') {
            g_config.console_log.push_back(std::string("> ") + g_config.console_input_buf);
            if (strcmp(g_config.console_input_buf, "help") == 0) {
                g_config.console_log.push_back("Commands: help, status, esp [on|off], aim [on|off], clear, exit");
            } else if (strcmp(g_config.console_input_buf, "status") == 0) {
                g_config.console_log.push_back("[✓] MLBB Memory: Hooked | Overlay: Active | Root: UID 0");
            } else if (strcmp(g_config.console_input_buf, "esp on") == 0) {
                g_config.esp_enabled = true;
                g_config.console_log.push_back("[+] ESP Visuals enabled.");
            } else if (strcmp(g_config.console_input_buf, "esp off") == 0) {
                g_config.esp_enabled = false;
                g_config.console_log.push_back("[-] ESP Visuals disabled.");
            } else if (strcmp(g_config.console_input_buf, "clear") == 0) {
                g_config.console_log.clear();
            } else {
                g_config.console_log.push_back(std::string("[*] Command executed: ") + g_config.console_input_buf);
            }
            g_config.console_input_buf[0] = '\0';
        }
    }
    ImGui::PopItemWidth();
}

static void RenderVisualsTab() {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();
    float total_w = kBaseWidth - 48.0f;
    float col_w = (total_w - 24.0f) * 0.5f;

    // LEFT CARD: ESP Features
    float card_h = 490.0f;
    CyberTheme::BeginCyberBox(dl, start, ImVec2(start.x + col_w, start.y + card_h), "ESP / Visuals (Mockup)");
    
    ImGui::SetCursorScreenPos(ImVec2(start.x + 18.0f, start.y + 24.0f));
    ImGui::BeginGroup();
    CyberTheme::CyberToggle("Master ESP", &g_config.esp_enabled);
    ImGui::Separator();
    CyberTheme::CyberToggle("Player 2D Box", &g_config.esp_box);
    CyberTheme::CyberToggle("Player Skeleton Bone", &g_config.esp_skeleton);
    CyberTheme::CyberToggle("Snaplines to Target", &g_config.esp_line);
    CyberTheme::CyberToggle("Health / Shield Bars", &g_config.esp_health);
    CyberTheme::CyberToggle("Hero Name & Distance", &g_config.esp_name);
    CyberTheme::CyberToggle("Mini-Map / 360° Radar", &g_config.esp_radar);
    
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.35f, 0.97f, 0.56f, 1.0f), "Max Render Distance: %.0fm", g_config.esp_max_distance);
    ImGui::PushItemWidth(col_w - 40.0f);
    ImGui::SliderFloat("##dist_slider", &g_config.esp_max_distance, 50.0f, 500.0f, "%.0f meters");
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    // RIGHT CARD: Preview / Visual Simulation
    ImVec2 right_start(start.x + col_w + 24.0f, start.y);
    CyberTheme::BeginCyberBox(dl, right_start, ImVec2(right_start.x + col_w, right_start.y + card_h), "Radar & Visual Preview");

    ImGui::SetCursorScreenPos(ImVec2(right_start.x + 18.0f, right_start.y + 24.0f));
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.34f, 0.78f, 1.0f, 1.0f), "Target Game: Mobile Legends Bang Bang");
    ImGui::Text("ESP Box Style:");
    ImGui::RadioButton("Corner Box", &g_config.esp_box_style, 0); ImGui::SameLine();
    ImGui::RadioButton("Full Box", &g_config.esp_box_style, 1); ImGui::SameLine();
    ImGui::RadioButton("3D Wireframe", &g_config.esp_box_style, 2);

    ImGui::Spacing();
    ImGui::Text("Snapline Origin:");
    ImGui::RadioButton("Top", &g_config.esp_line_pos, 0); ImGui::SameLine();
    ImGui::RadioButton("Center", &g_config.esp_line_pos, 1); ImGui::SameLine();
    ImGui::RadioButton("Bottom", &g_config.esp_line_pos, 2);

    ImGui::Spacing();
    ImGui::Text("Color Theme Accent:");
    ImGui::ColorEdit4("ESP Color", g_config.esp_box_color, ImGuiColorEditFlags_NoInputs);

    // Draw mockup radar circle
    ImVec2 radar_center(right_start.x + col_w * 0.5f, right_start.y + 350.0f);
    float radar_r = 75.0f;
    dl->AddCircleFilled(radar_center, radar_r, IM_COL32(18, 25, 34, 255));
    dl->AddCircle(radar_center, radar_r, CyberTheme::ColCyan, 32, 1.5f);
    dl->AddCircle(radar_center, radar_r * 0.5f, CyberTheme::ColBorder, 32, 1.0f);
    dl->AddLine(ImVec2(radar_center.x - radar_r, radar_center.y), ImVec2(radar_center.x + radar_r, radar_center.y), CyberTheme::ColBorder);
    dl->AddLine(ImVec2(radar_center.x, radar_center.y - radar_r), ImVec2(radar_center.x, radar_center.y + radar_r), CyberTheme::ColBorder);
    // Player dot (center)
    dl->AddCircleFilled(radar_center, 4.0f, CyberTheme::ColLime);
    // Simulated enemies
    dl->AddCircleFilled(ImVec2(radar_center.x + 35.0f, radar_center.y - 25.0f), 4.5f, CyberTheme::ColRed);
    dl->AddText(ImVec2(radar_center.x + 42.0f, radar_center.y - 32.0f), CyberTheme::ColRed, "Layla (12m)");

    dl->AddCircleFilled(ImVec2(radar_center.x - 40.0f, radar_center.y + 20.0f), 4.5f, CyberTheme::ColRed);
    dl->AddText(ImVec2(radar_center.x - 75.0f, radar_center.y + 25.0f), CyberTheme::ColRed, "Tigreal (28m)");
    ImGui::EndGroup();
}

static void RenderAimbotTab() {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();
    float total_w = kBaseWidth - 48.0f;
    float col_w = (total_w - 24.0f) * 0.5f;

    // LEFT CARD: Combat Settings
    float card_h = 490.0f;
    CyberTheme::BeginCyberBox(dl, start, ImVec2(start.x + col_w, start.y + card_h), "Aim Assist / Combat (Mockup)");

    ImGui::SetCursorScreenPos(ImVec2(start.x + 18.0f, start.y + 24.0f));
    ImGui::BeginGroup();
    CyberTheme::CyberToggle("Master Aim Assist", &g_config.aim_enabled);
    CyberTheme::CyberToggle("Draw FOV Circle", &g_config.aim_draw_fov);
    CyberTheme::CyberToggle("Movement Prediction", &g_config.aim_predict);
    CyberTheme::CyberToggle("Visible Target Check", &g_config.aim_visible_only);
    CyberTheme::CyberToggle("Auto Trigger / Fire", &g_config.aim_auto_fire);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.35f, 0.97f, 0.56f, 1.0f), "Aim FOV Radius: %.1f°", g_config.aim_fov);
    ImGui::PushItemWidth(col_w - 40.0f);
    ImGui::SliderFloat("##fov_slider", &g_config.aim_fov, 10.0f, 180.0f, "%.1f deg");
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.35f, 0.97f, 0.56f, 1.0f), "Aim Smoothness: %.1f", g_config.aim_smoothness);
    ImGui::PushItemWidth(col_w - 40.0f);
    ImGui::SliderFloat("##smooth_slider", &g_config.aim_smoothness, 1.0f, 25.0f, "%.1f");
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    // RIGHT CARD: Hitbox and Priority
    ImVec2 right_start(start.x + col_w + 24.0f, start.y);
    CyberTheme::BeginCyberBox(dl, right_start, ImVec2(right_start.x + col_w, right_start.y + card_h), "Target Priority & Bone");

    ImGui::SetCursorScreenPos(ImVec2(right_start.x + 18.0f, right_start.y + 24.0f));
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.34f, 0.78f, 1.0f, 1.0f), "Target Priority Mode:");
    static int priority_mode = 0;
    ImGui::RadioButton("Lowest Health (HP)", &priority_mode, 0);
    ImGui::RadioButton("Closest Distance", &priority_mode, 1);
    ImGui::RadioButton("Nearest to Crosshair", &priority_mode, 2);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.34f, 0.78f, 1.0f, 1.0f), "Target Bone Selection:");
    ImGui::RadioButton("Head / Critical", &g_config.aim_bone, 0);
    ImGui::RadioButton("Neck", &g_config.aim_bone, 1);
    ImGui::RadioButton("Chest / Center Mass", &g_config.aim_bone, 2);
    ImGui::RadioButton("Pelvis / Base", &g_config.aim_bone, 3);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.42f, 1.0f), "Input Dispatch Method:");
    ImGui::BulletText("Kernel /dev/input Injection");
    ImGui::BulletText("Non-exclusive input grabber");
    ImGui::BulletText("Anti-cheat signature: NONE");
    ImGui::EndGroup();
}

static void RenderMiscTab() {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();
    float total_w = kBaseWidth - 48.0f;
    float col_w = (total_w - 24.0f) * 0.5f;

    // LEFT CARD: In-Game Tweaks
    float card_h = 490.0f;
    CyberTheme::BeginCyberBox(dl, start, ImVec2(start.x + col_w, start.y + card_h), "Game Tweaks (Mockup)");

    ImGui::SetCursorScreenPos(ImVec2(start.x + 18.0f, start.y + 24.0f));
    ImGui::BeginGroup();
    CyberTheme::CyberToggle("Drone View Camera Zoom", &g_config.drone_view_enabled);
    ImGui::TextColored(ImVec4(0.35f, 0.97f, 0.56f, 1.0f), "Camera Height: %.1fx", g_config.drone_view_zoom);
    ImGui::PushItemWidth(col_w - 40.0f);
    ImGui::SliderFloat("##drone_slider", &g_config.drone_view_zoom, 1.0f, 5.0f, "%.1fx Zoom");
    ImGui::PopItemWidth();

    ImGui::Spacing();
    CyberTheme::CyberToggle("Unlock 120 Ultra FPS", &g_config.unlock_120fps);
    CyberTheme::CyberToggle("Show Enemy Skill Cooldowns", &g_config.show_enemy_cooldowns);
    CyberTheme::CyberToggle("Unlock All Skins (Visual Only)", &g_config.unlock_skins_visual);
    CyberTheme::CyberToggle("Streamer Mode (OBS Hidden)", &g_config.streamer_mode);
    CyberTheme::CyberToggle("Touch Pass-Through", &g_config.touch_bypass);
    ImGui::EndGroup();

    // RIGHT CARD: Terminal Console Log
    ImVec2 right_start(start.x + col_w + 24.0f, start.y);
    CyberTheme::BeginCyberBox(dl, right_start, ImVec2(right_start.x + col_w, right_start.y + card_h), "Overlay Event Log");

    ImGui::SetCursorScreenPos(ImVec2(right_start.x + 18.0f, right_start.y + 24.0f));
    ImGui::BeginGroup();
    ImGui::BeginChild("##log_scroll", ImVec2(col_w - 36.0f, card_h - 48.0f), false);
    for (const auto &line : g_config.console_log) {
        if (line.rfind("[✓]", 0) == 0 || line.rfind("[+]", 0) == 0) {
            ImGui::TextColored(ImVec4(0.35f, 0.97f, 0.56f, 1.0f), "%s", line.c_str());
        } else if (line.rfind("[-]", 0) == 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", line.c_str());
        } else {
            ImGui::TextUnformatted(line.c_str());
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::EndGroup();
}

static void RenderSettingsTab(bool *running) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();
    float total_w = kBaseWidth - 48.0f;
    float col_w = (total_w - 24.0f) * 0.5f;

    float card_h = 490.0f;
    CyberTheme::BeginCyberBox(dl, start, ImVec2(start.x + col_w, start.y + card_h), "Overlay Preferences");

    ImGui::SetCursorScreenPos(ImVec2(start.x + 18.0f, start.y + 24.0f));
    ImGui::BeginGroup();
    ImGui::Text("Window Opacity:");
    ImGui::PushItemWidth(col_w - 40.0f);
    ImGui::SliderFloat("##alpha_slider", &g_config.window_alpha, 0.35f, 1.0f, "%.2f");
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Text("Scale Multiplier:");
    ImGui::PushItemWidth(col_w - 40.0f);
    if (ImGui::SliderFloat("##scale_slider", &layer_scale, 0.55f, 1.40f, "%.2fx")) {
        ApplyLayerGeometry();
    }
    ImGui::PopItemWidth();

    ImGui::Spacing();
    if (ImGui::Button("Minimize to Floating Bubble", ImVec2(col_w - 40.0f, 42.0f))) {
        g_config.minimized_bubble = true;
        ApplyLayerGeometry();
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset Screen Center", ImVec2(col_w - 40.0f, 42.0f))) {
        surface_screen_x = std::max(0.0f, (abs_ScreenX - native_window_screen_x) * 0.5f);
        surface_screen_y = std::max(0.0f, (abs_ScreenY - native_window_screen_y) * 0.5f);
        android::ANativeWindowCreator::SetPosition(window, surface_screen_x, surface_screen_y);
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.22f, 0.22f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
    if (ImGui::Button("Exit & Terminate Overlay", ImVec2(col_w - 40.0f, 42.0f))) {
        *running = false;
    }
    ImGui::PopStyleColor(2);
    ImGui::EndGroup();

    // RIGHT CARD: Device & System Info
    ImVec2 right_start(start.x + col_w + 24.0f, start.y);
    CyberTheme::BeginCyberBox(dl, right_start, ImVec2(right_start.x + col_w, right_start.y + card_h), "Device Environment");

    ImGui::SetCursorScreenPos(ImVec2(right_start.x + 18.0f, right_start.y + 24.0f));
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.35f, 0.97f, 0.56f, 1.0f), "Screen Resolution: %d x %d", abs_ScreenX, abs_ScreenY);
    ImGui::TextColored(ImVec4(0.35f, 0.97f, 0.56f, 1.0f), "Window Surface: %d x %d (Scale: %.2f)",
                       native_window_screen_x, native_window_screen_y, layer_scale);
    ImGui::TextColored(ImVec4(0.35f, 0.97f, 0.56f, 1.0f), "Orientation: %d deg", displayInfo.orientation * 90);
    ImGui::Separator();
    ImGui::Text("Graphics Backend: OpenGL ES 3.0 + EGL");
    ImGui::Text("Surface API: SurfaceComposerClient");
    ImGui::Text("Touch Mode: Evdev Non-exclusive Observer");
    ImGui::Text("Security: TrustedOverlay + eSkipScreenshot");
    ImGui::EndGroup();
}

// -------------------------------------------------------------
// Floating Bubble Mode UI
// -------------------------------------------------------------

static void RenderBubbleUI(bool *running) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 b_pos(10.0f, 10.0f);
    float r = kBubbleSize * 0.5f;
    ImVec2 center(b_pos.x + r, b_pos.y + r);

    // Glowing circle badge
    dl->AddCircleFilled(center, r, IM_COL32(12, 16, 20, 240));
    dl->AddCircle(center, r, CyberTheme::ColLime, 32, 2.5f);
    dl->AddCircle(center, r - 4.0f, CyberTheme::ColCyan, 32, 1.0f);

    // Terminal prompt icon inside bubble: >_
    ImVec2 ts = ImGui::CalcTextSize(">_");
    dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), CyberTheme::ColLime, ">_");

    ImGui::SetCursorPos(b_pos);
    ImGui::InvisibleButton("##bubble_btn", ImVec2(kBubbleSize, kBubbleSize));
    
    // Drag bubble or tap to expand
    if (ImGui::IsItemActivated()) {
        is_dragging_bubble = true;
        drag_touch_start = ImVec2(touch_x, touch_y);
        drag_surface_start = ImVec2(surface_screen_x, surface_screen_y);
    }
    if (is_dragging_bubble && touch_down) {
        float dx = touch_x - drag_touch_start.x;
        float dy = touch_y - drag_touch_start.y;
        surface_screen_x = std::clamp(drag_surface_start.x + dx, 0.0f, (float)std::max(0, abs_ScreenX - native_window_screen_x));
        surface_screen_y = std::clamp(drag_surface_start.y + dy, 0.0f, (float)std::max(0, abs_ScreenY - native_window_screen_y));
        android::ANativeWindowCreator::SetPosition(window, surface_screen_x, surface_screen_y);
    }
    if (is_dragging_bubble && !touch_down) {
        // If movement was minimal, consider it a tap to expand!
        float moved = std::hypot(touch_x - drag_touch_start.x, touch_y - drag_touch_start.y);
        if (moved < 15.0f) {
            g_config.minimized_bubble = false;
            ApplyLayerGeometry();
        }
        is_dragging_bubble = false;
    }
}

// -------------------------------------------------------------
// Main UI Tick
// -------------------------------------------------------------

void Layout_tick_UI(bool *running) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kProducerWidth, (float)LogicalLayerHeight()), ImGuiCond_Always);
    ImGui::Begin("##root_overlay_host", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);

    if (g_config.minimized_bubble) {
        RenderBubbleUI(running);
        ImGui::End();

        float sc = layer_scale;
        float bx = surface_screen_x + 10.0f * sc;
        float by = surface_screen_y + 10.0f * sc;
        My_Vector2 b_pos(bx, by);
        My_Vector2 b_size(kBubbleSize * sc, kBubbleSize * sc);
        Touch::SetTouchObstacle(&b_pos, &b_size, 1);
        Touch::SetTouchCircles(nullptr, nullptr, 0);
        return;
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float win_h = g_config.collapsed ? kTitleHeight : kBaseHeight;
    const ImVec2 win_min(kSideMargin, kTopMargin);
    const ImVec2 win_max(kSideMargin + kBaseWidth, kTopMargin + win_h);

    // Terminal Dark Background with rounded corners
    ImU32 bg_color = IM_COL32(12, 16, 20, (int)(255 * g_config.window_alpha));
    dl->AddRectFilled(win_min, win_max, bg_color, 14.0f);
    dl->AddRect(win_min, win_max, CyberTheme::ColBorder, 14.0f, 0, 1.2f);

    // Titlebar Bar Background
    dl->AddRectFilled(win_min, ImVec2(win_max.x, win_min.y + kTitleHeight),
        CyberTheme::ColHeader, 14.0f,
        g_config.collapsed ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(win_min.x, win_min.y + kTitleHeight), ImVec2(win_max.x, win_min.y + kTitleHeight), CyberTheme::ColBorder);

    // -------------------------------------------------------------
    // Titlebar Navigation: Tabs (pwsh reference, ESP, Aim, Misc, Settings)
    // -------------------------------------------------------------
    struct TabItem { const char* label; const char* title; };
    const TabItem tabs[] = {
        { "pwsh", "PowerShell 7.6.3" },
        { "Visuals", "ESP / Radar" },
        { "Combat", "Aim Assist" },
        { "Misc", "Game Mods" },
        { "Settings", "Overlay Config" }
    };

    float tab_x = win_min.x + 16.0f;
    for (int t = 0; t < 5; ++t) {
        bool active = (g_config.active_tab == t);
        float tab_w = (t == 0) ? 96.0f : 100.0f;
        float tab_h = 36.0f;
        ImVec2 tmin(tab_x, win_min.y + 8.0f);
        ImVec2 tmax(tab_x + tab_w, win_min.y + 8.0f + tab_h);

        ImU32 tbg = active ? CyberTheme::ColTabActive : CyberTheme::ColHeader;
        dl->AddRectFilled(tmin, tmax, tbg, 6.0f);
        if (active) {
            dl->AddRect(tmin, tmax, CyberTheme::ColCyan, 6.0f, 0, 1.2f);
            // Small active tab bottom indicator
            dl->AddLine(ImVec2(tmin.x + 10.0f, tmax.y), ImVec2(tmax.x - 10.0f, tmax.y), CyberTheme::ColLime, 2.5f);
        }

        // Tab icon + text
        ImVec2 ts = ImGui::CalcTextSize(tabs[t].label);
        ImU32 text_col = active ? CyberTheme::ColLime : CyberTheme::ColMuted;
        dl->AddText(ImVec2(tmin.x + (tab_w - ts.x) * 0.5f, tmin.y + (tab_h - ts.y) * 0.5f), text_col, tabs[t].label);

        ImGui::SetCursorScreenPos(tmin);
        char btn_id[32];
        std::snprintf(btn_id, sizeof(btn_id), "##tab_%d", t);
        if (ImGui::InvisibleButton(btn_id, ImVec2(tab_w, tab_h))) {
            g_config.active_tab = t;
        }

        tab_x += tab_w + 8.0f;
    }

    // Drag Handle area on the titlebar
    float drag_area_w = (win_max.x - tab_x) - 160.0f;
    if (drag_area_w > 50.0f) {
        ImGui::SetCursorScreenPos(ImVec2(tab_x + 10.0f, win_min.y + 6.0f));
        ImGui::InvisibleButton("##title_drag", ImVec2(drag_area_w, 40.0f));
        if (ImGui::IsItemActivated()) {
            is_dragging = true;
            drag_touch_start = ImVec2(touch_x, touch_y);
            drag_surface_start = ImVec2(surface_screen_x, surface_screen_y);
        }
        if (is_dragging && touch_down) {
            surface_screen_x = std::clamp(
                drag_surface_start.x + touch_x - drag_touch_start.x,
                0.0f, (float)std::max(0, abs_ScreenX - native_window_screen_x));
            surface_screen_y = std::clamp(
                drag_surface_start.y + touch_y - drag_touch_start.y,
                0.0f, (float)std::max(0, abs_ScreenY - native_window_screen_y));
            android::ANativeWindowCreator::SetPosition(window, surface_screen_x, surface_screen_y);
        }
        if (is_dragging && !touch_down) is_dragging = false;
    }

    // Titlebar Right Controls: Bubble Minimize, Collapse/Expand, Close
    float btn_w = 42.0f;
    float btn_h = 36.0f;
    float right_ctrl_x = win_max.x - 146.0f;
    float ctrl_y = win_min.y + 8.0f;

    // 1. Bubble minimize button
    ImVec2 bmin(right_ctrl_x, ctrl_y);
    ImVec2 bmax(right_ctrl_x + btn_w, ctrl_y + btn_h);
    dl->AddRectFilled(bmin, bmax, IM_COL32(28, 38, 52, 255), 6.0f);
    dl->AddCircle(ImVec2((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f), 7.0f, CyberTheme::ColCyan, 16, 2.0f);
    ImGui::SetCursorScreenPos(bmin);
    if (ImGui::InvisibleButton("##bubble_min_btn", ImVec2(btn_w, btn_h))) {
        g_config.minimized_bubble = true;
        ApplyLayerGeometry();
    }

    // 2. Collapse / Fold button
    right_ctrl_x += btn_w + 6.0f;
    ImVec2 cmin(right_ctrl_x, ctrl_y);
    ImVec2 cmax(right_ctrl_x + btn_w, ctrl_y + btn_h);
    dl->AddRectFilled(cmin, cmax, IM_COL32(28, 38, 52, 255), 6.0f);
    ImVec2 cc((cmin.x + cmax.x) * 0.5f, (cmin.y + cmax.y) * 0.5f);
    if (g_config.collapsed) {
        dl->AddTriangleFilled(ImVec2(cc.x - 8, cc.y - 5), ImVec2(cc.x + 8, cc.y - 5), ImVec2(cc.x, cc.y + 6), CyberTheme::ColWhite);
    } else {
        dl->AddTriangleFilled(ImVec2(cc.x, cc.y - 6), ImVec2(cc.x - 8, cc.y + 5), ImVec2(cc.x + 8, cc.y + 5), CyberTheme::ColWhite);
    }
    ImGui::SetCursorScreenPos(cmin);
    if (ImGui::InvisibleButton("##collapse_btn", ImVec2(btn_w, btn_h))) {
        g_config.collapsed = !g_config.collapsed;
        ApplyLayerGeometry();
    }

    // 3. Close button
    right_ctrl_x += btn_w + 6.0f;
    ImVec2 xmin(right_ctrl_x, ctrl_y);
    ImVec2 xmax(right_ctrl_x + btn_w, ctrl_y + btn_h);
    dl->AddRectFilled(xmin, xmax, IM_COL32(65, 30, 36, 255), 6.0f);
    ImVec2 xc((xmin.x + xmax.x) * 0.5f, (xmin.y + xmax.y) * 0.5f);
    dl->AddLine(ImVec2(xc.x - 7, xc.y - 7), ImVec2(xc.x + 7, xc.y + 7), CyberTheme::ColRed, 2.5f);
    dl->AddLine(ImVec2(xc.x + 7, xc.y - 7), ImVec2(xc.x - 7, xc.y + 7), CyberTheme::ColRed, 2.5f);
    ImGui::SetCursorScreenPos(xmin);
    if (ImGui::InvisibleButton("##close_btn", ImVec2(btn_w, btn_h))) {
        *running = false;
    }

    // -------------------------------------------------------------
    // Body Content (when not collapsed)
    // -------------------------------------------------------------
    if (!g_config.collapsed) {
        ImGui::SetCursorScreenPos(ImVec2(win_min.x + 24.0f, win_min.y + kTitleHeight + 16.0f));
        ImGui::BeginChild("##tab_content", ImVec2(kBaseWidth - 48.0f, kBaseHeight - kTitleHeight - 32.0f),
                          false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);

        switch (g_config.active_tab) {
            case 0: RenderTerminalView(); break;
            case 1: RenderVisualsTab(); break;
            case 2: RenderAimbotTab(); break;
            case 3: RenderMiscTab(); break;
            case 4: RenderSettingsTab(running); break;
        }

        ImGui::EndChild();

        // Corner Resize Affordances (Bottom Left & Bottom Right)
        constexpr float hit = 54.0f;
        const float corner_y = win_max.y + 2.0f;
        const ImVec2 left_center(win_min.x - 2.0f, corner_y);
        const ImVec2 right_center(win_max.x + 2.0f, corner_y);

        ImGui::SetCursorScreenPos(ImVec2(left_center.x - hit * 0.5f, left_center.y - hit * 0.5f));
        ImGui::InvisibleButton("##resize_l", ImVec2(hit, hit));
        bool left_hit = ImGui::IsItemActivated();
        dl->AddTriangleFilled(ImVec2(left_center.x - 12, left_center.y + 12),
                              ImVec2(left_center.x - 12, left_center.y - 12),
                              ImVec2(left_center.x + 12, left_center.y + 12),
                              CyberTheme::ColCyan);

        ImGui::SetCursorScreenPos(ImVec2(right_center.x - hit * 0.5f, right_center.y - hit * 0.5f));
        ImGui::InvisibleButton("##resize_r", ImVec2(hit, hit));
        bool right_hit = ImGui::IsItemActivated();
        dl->AddTriangleFilled(ImVec2(right_center.x + 12, right_center.y + 12),
                              ImVec2(right_center.x - 12, right_center.y + 12),
                              ImVec2(right_center.x + 12, right_center.y - 12),
                              CyberTheme::ColCyan);

        if (left_hit || right_hit) {
            resize_mode = left_hit ? ResizeMode::LeftBottom : ResizeMode::RightBottom;
            resize_start_scale = layer_scale;
            resize_touch_start_x = touch_x;
            resize_touch_start_y = touch_y;
            resize_fixed_x = (resize_mode == ResizeMode::LeftBottom)
                ? surface_screen_x + (kSideMargin + kBaseWidth) * layer_scale
                : surface_screen_x + kSideMargin * layer_scale;
            resize_fixed_y = surface_screen_y;
        }

        if (resize_mode != ResizeMode::None && touch_down) {
            float dx = touch_x - resize_touch_start_x;
            float dy = touch_y - resize_touch_start_y;
            float sign = (resize_mode == ResizeMode::LeftBottom) ? -1.0f : 1.0f;
            float projected = ((sign * dx) * kBaseWidth + dy * kBaseHeight) /
                              (kBaseWidth * kBaseWidth + kBaseHeight * kBaseHeight);
            float maxx = (float)abs_ScreenX / kProducerWidth;
            float maxy = (float)(abs_ScreenY - resize_fixed_y) / kProducerHeight;
            layer_scale = std::clamp(resize_start_scale + projected, kMinScale,
                                     std::max(kMinScale, std::min(maxx, maxy)));
            if (resize_mode == ResizeMode::LeftBottom)
                surface_screen_x = resize_fixed_x - (kSideMargin + kBaseWidth) * layer_scale;
            else
                surface_screen_x = resize_fixed_x - kSideMargin * layer_scale;
            surface_screen_x = std::clamp(surface_screen_x, 0.0f, (float)abs_ScreenX - kProducerWidth * layer_scale);
            surface_screen_y = resize_fixed_y;
            ApplyLayerGeometry();
            android::ANativeWindowCreator::SetPosition(window, surface_screen_x, surface_screen_y);
        }
        if (resize_mode != ResizeMode::None && !touch_down) {
            resize_mode = ResizeMode::None;
        }
    }

    if (resize_mode != ResizeMode::None) {
        char sz_label[32];
        std::snprintf(sz_label, sizeof(sz_label), "%d × %d",
                      (int)std::lround(kBaseWidth * layer_scale),
                      (int)std::lround(kBaseHeight * layer_scale));
        ImVec2 ts = ImGui::CalcTextSize(sz_label);
        ImVec2 p(win_min.x + (kBaseWidth - ts.x) * 0.5f, 16.0f);
        dl->AddRectFilled(ImVec2(p.x - 14, p.y - 6), ImVec2(p.x + ts.x + 14, p.y + ts.y + 6), IM_COL32(18, 24, 34, 230), 12.0f);
        dl->AddText(p, CyberTheme::ColWhite, sz_label);
    }

    ImGui::End();

    // Set Touch Obstacles for non-exclusive touch dispatcher
    float sc = layer_scale;
    float glass_y = surface_screen_y + kTopMargin * sc;
    float glass_h_px = (g_config.collapsed ? kTitleHeight : kBaseHeight) * sc;
    float glass_x = surface_screen_x + kSideMargin * sc;
    My_Vector2 glass_pos(glass_x, glass_y);
    My_Vector2 glass_size(kBaseWidth * sc, glass_h_px);
    Touch::SetTouchObstacle(&glass_pos, &glass_size, 1);

    My_Vector2 circle_center[2] = {
        { glass_x - 2.0f * sc, glass_y + glass_h_px + 2.0f * sc },
        { glass_x + (kBaseWidth + 2.0f) * sc, glass_y + glass_h_px + 2.0f * sc }
    };
    float circle_radius[2] = { 28.0f * sc, 28.0f * sc };
    Touch::SetTouchCircles(circle_center, circle_radius, g_config.collapsed ? 0 : 2);
}
