#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include "draw.h"
#include "GraphicsManager.h"

static bool g_running = true;

static void SignalHandler(int signum) {
    (void)signum;
    g_running = false;
}

int main(int argc, char **argv) {
    printf("\033[38;5;120m");
    printf("[+] ================================================\n");
    printf("[+]  Cyber Overlay Terminal GUI (Android Root)     \n");
    printf("[+]  UID: %d | PID: %d\n", getuid(), getpid());
    printf("[+] ================================================\033[0m\n");

    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    graphics = GraphicsManager::getGraphicsInterface(GraphicsManager::OPENGL);
    if (!graphics) {
        fprintf(stderr, "[-] Failed to obtain OpenGL graphics interface.\n");
        return 2;
    }

    displayInfo = android::ANativeWindowCreator::GetDisplayInfo();
    if (displayInfo.width <= 0 || displayInfo.height <= 0) {
        // Fallback standard full HD if display service query failed
        displayInfo.width = 2400;
        displayInfo.height = 1080;
        displayInfo.orientation = 0;
        printf("[!] Warning: Defaulting display to 2400x1080.\n");
    }
    abs_ScreenX = displayInfo.width;
    abs_ScreenY = displayInfo.height;

    printf("[+] Display Resolution: %d x %d (Orientation: %d)\n", abs_ScreenX, abs_ScreenY, displayInfo.orientation);

    constexpr int producer_w = 1044; // kProducerWidth
    constexpr int producer_h = 720;  // kProducerHeight
    native_window_screen_x = producer_w;
    native_window_screen_y = producer_h;

    window = android::ANativeWindowCreator::Create(
        "CyberTerminal_Overlay", producer_w, producer_h, false);
    if (!window) {
        fprintf(stderr, "[-] Failed to create ANativeWindow via SurfaceComposer.\n");
        return 4;
    }
    printf("[+] Native window surface successfully created.\n");

    if (!graphics->Init_Render(window, producer_w, producer_h)) {
        fprintf(stderr, "[-] Failed to initialize OpenGL ES 3 render context.\n");
        android::ANativeWindowCreator::Destroy(window);
        return 5;
    }
    printf("[+] OpenGL ES 3 context & ImGui initialized.\n");

    if (!Touch::Init({(float)abs_ScreenX, (float)abs_ScreenY}, true)) {
        printf("[!] Warning: Evdev touch capture initialization returned false. Running in view mode.\n");
    } else {
        printf("[+] Non-exclusive touch observer active.\n");
    }

    init_My_drawdata();
    printf("[+] Overlay UI running. Drag titlebar to move, minimize to bubble, or close.\n");

    while (g_running) {
        drawBegin();
        graphics->NewFrame(false);
        Layout_tick_UI(&g_running);
        graphics->EndFrame();
        // Yield slice to prevent busy-spinning when idle
        usleep(12000); // ~80 FPS max
    }

    printf("[+] Shutting down overlay...\n");
    Touch::Close();
    graphics->Shutdown();
    android::ANativeWindowCreator::Destroy(window);
    printf("[+] Overlay cleanly terminated.\n");
    return 0;
}
