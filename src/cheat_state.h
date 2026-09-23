#pragma once
#include <string>
#include <vector>
#include <chrono>

struct CheatConfig {
    // Tab Navigation: 0 = Terminal, 1 = Visuals / ESP, 2 = Aimbot, 3 = Misc, 4 = Settings
    int active_tab = 0;

    // Overlay State
    bool collapsed = false;
    bool minimized_bubble = false;
    float window_scale = 1.0f;
    float window_alpha = 0.94f;
    bool show_demo = false;

    // Visuals / ESP (Dummy Mockup)
    bool esp_enabled = true;
    bool esp_box = true;
    int esp_box_style = 0; // 0 = 2D Corner, 1 = 2D Box, 2 = 3D Wireframe
    bool esp_line = false;
    int esp_line_pos = 0;  // 0 = Top, 1 = Center, 2 = Bottom
    bool esp_skeleton = true;
    bool esp_health = true;
    bool esp_name = true;
    bool esp_distance = true;
    bool esp_radar = true;
    float esp_max_distance = 250.0f;
    float esp_box_color[4] = {0.35f, 0.97f, 0.56f, 1.0f};

    // Combat / Aimbot (Dummy Mockup)
    bool aim_enabled = false;
    bool aim_draw_fov = true;
    float aim_fov = 90.0f;
    float aim_smoothness = 6.5f;
    int aim_bone = 0; // 0 = Head, 1 = Neck, 2 = Chest, 3 = Pelvis
    bool aim_predict = true;
    bool aim_visible_only = true;
    bool aim_auto_fire = false;

    // Misc / Tweaks (Dummy Mockup)
    bool unlock_120fps = true;
    bool drone_view_enabled = true;
    float drone_view_zoom = 2.4f; // 1.0x to 5.0x
    bool show_enemy_cooldowns = true;
    bool unlock_skins_visual = false;
    bool streamer_mode = false;
    bool touch_bypass = false;

    // Terminal & System Info
    std::string user_name = "Vanil";
    std::string host_name = "Android-Root";
    std::string cpu_model = "AMD Ryzen 5 5600X (6C / 12T) @ 4.65 GHz";
    std::string gpu_model = "NVIDIA GeForce RTX 3060 [Discrete] // 11.83 GiB";
    float ram_used_gb = 12.61f;
    float ram_total_gb = 15.89f;
    float swap_used_mb = 338.86f;
    float swap_total_mb = 12800.0f;
    float drive_c_used = 313.56f;
    float drive_c_total = 464.79f;
    float drive_d_used = 541.77f;
    float drive_d_total = 931.51f;
    float drive_e_used = 267.97f;
    float drive_e_total = 443.23f;
    float drive_f_used = 148.94f;
    float drive_f_total = 488.28f;
    std::string login_timestamp = "2026-07-16 14:11:16";
    int uptime_hours = 6;
    int uptime_mins = 36;
    std::string quote_text = "Bonsoir! Elliot.";

    // Interactive Terminal Console
    char console_input_buf[128] = "";
    std::vector<std::string> console_log;
};

extern CheatConfig g_config;
