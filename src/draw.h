#pragma once
#include <memory>
#include "imgui.h"
#include "native_surface/ANativeWindowCreator.h"
#include "AndroidImgui.h"
#include "TouchHelperA.h"
#include "cheat_state.h"

extern std::unique_ptr<AndroidImgui> graphics;
extern ANativeWindow *window;
extern android::ANativeWindowCreator::DisplayInfo displayInfo;
extern int abs_ScreenX, abs_ScreenY;
extern int native_window_screen_x, native_window_screen_y;
extern float surface_screen_x, surface_screen_y;

void screen_config();
void drawBegin();
void Layout_tick_UI(bool *running);
void init_My_drawdata();
