# -----------------------------------------------------------------------------
#  top-level build
#
#    make app      -> build/spectre        окно с меню (нужен SDL2)
#    make run      -> собрать и запустить окно на этой машине
#    make phone    -> собрать и запустить в терминале/на телефоне (см. run.sh)
#    make shot     -> tools/preview/build/menu.png (софтверный рендер, GPU не нужен)
#    make overlay  -> build/spectre-overlay: меню поверх других приложений (root,
#                     Android, без SDL/NDK/EGL - читает /dev/input и рисует в
#                     SurfaceFlinger-слой через ANativeWindow_lock)
#    make clean
#
#  SDL2 в Termux:      pkg install sdl2 clang make
#  SDL2 в Debian:      sudo apt install libsdl2-dev
#  SDL2 в macOS:       brew install sdl2 pkg-config
# -----------------------------------------------------------------------------
IMGUI_DIR ?= external/imgui
CXX       ?= c++
BUILD     ?= build
JOBS      ?= $(shell nproc 2>/dev/null || echo 4)
# 1: the overlay and the app log what they do (logcat + stderr), 0: silence.
SURFACE_LOG ?= 1

SDL_CFLAGS ?= $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL_LIBS   ?= $(shell pkg-config --libs   sdl2 2>/dev/null || sdl2-config --libs   2>/dev/null)

CXXFLAGS  ?= -std=c++17 -O2 -Iinclude -Isrc -I$(IMGUI_DIR) -Wall $(SDL_CFLAGS)

# imgui_demo.cpp нужен не для демо-окна: в v1.83 именно в нём лежит
# ShowFontAtlas(), без которого imgui.cpp не линкуется.
IMGUI_SRC := imgui.cpp imgui_draw.cpp imgui_widgets.cpp imgui_tables.cpp imgui_demo.cpp
SRC       := src/app/main_sdl.cpp src/Menu.cpp src/Theme.cpp src/render/SoftRenderer.cpp \
             $(addprefix $(IMGUI_DIR)/,$(IMGUI_SRC))
OBJ       := $(addprefix $(BUILD)/,$(notdir $(SRC:.cpp=.o)))
BIN       := $(BUILD)/spectre

.PHONY: all app run phone shot preview overlay overlay-phone clean check-sdl check-imgui

all: app

app: check-sdl $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(SDL_LIBS) -lpthread

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: src/app/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: src/render/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: $(IMGUI_DIR)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

check-sdl:
	@if [ -z "$(SDL_CFLAGS)$(SDL_LIBS)" ]; then \
	  echo 'SDL2 не найден. Установите:'; \
	  echo '  Termux:  pkg install sdl2 clang make'; \
	  echo '  Debian:  sudo apt install libsdl2-dev'; \
	  echo '  macOS:   brew install sdl2 pkg-config'; \
	  echo 'Или передайте пути вручную: make app SDL_CFLAGS=... SDL_LIBS=...'; \
	  exit 1; \
	fi
	@if [ ! -f $(IMGUI_DIR)/imgui.h ]; then \
	  echo "ImGui не найден в $(IMGUI_DIR)."; \
	  echo '  git submodule update --init --recursive'; \
	  exit 1; \
	fi

run: app
	$(BIN)

phone: app
	./run.sh

# The overlay needs no SDL, no EGL and no NDK: it links against libc and finds
# libgui.so/libutils.so/libandroid.so on the device at runtime.
OVL_FLAGS := -std=c++17 -O2 -Iinclude -Isrc -I$(IMGUI_DIR) -Wall -fPIE -pie \
             -DSURFACE_LOG_TAG='"spectre"' -DSURFACE_LOG_ENABLE=$(SURFACE_LOG)
OVL_SRC   := src/app/main_overlay.cpp src/overlay/SurfaceSink.cpp src/overlay/TouchReader.cpp \
             src/Menu.cpp src/Theme.cpp src/render/SoftRenderer.cpp \
             $(addprefix $(IMGUI_DIR)/,$(IMGUI_SRC))
OVL_OBJ   := $(addprefix $(BUILD)/ovl-,$(notdir $(OVL_SRC:.cpp=.o)))
OVL_BIN   := $(BUILD)/spectre-overlay

overlay: $(OVL_BIN)

$(OVL_BIN): $(OVL_OBJ)
	$(CXX) $(OVL_FLAGS) $^ -o $@

$(BUILD)/ovl-%.o: src/%.cpp | $(BUILD)
	$(CXX) $(OVL_FLAGS) -c $< -o $@

$(BUILD)/ovl-%.o: src/app/%.cpp | $(BUILD)
	$(CXX) $(OVL_FLAGS) -c $< -o $@

$(BUILD)/ovl-%.o: src/overlay/%.cpp | $(BUILD)
	$(CXX) $(OVL_FLAGS) -c $< -o $@

$(BUILD)/ovl-%.o: src/render/%.cpp | $(BUILD)
	$(CXX) $(OVL_FLAGS) -c $< -o $@

$(BUILD)/ovl-%.o: $(IMGUI_DIR)/%.cpp | $(BUILD)
	$(CXX) $(OVL_FLAGS) -c $< -o $@

# Build it on the phone and start it as root (see run.sh --overlay).
overlay-phone: overlay
	./run.sh --overlay

preview:
	$(MAKE) -C tools/preview

shot:
	$(MAKE) -C tools/preview shot

clean:
	rm -rf $(BUILD)
	$(MAKE) -C tools/preview clean
