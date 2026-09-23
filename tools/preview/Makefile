# -----------------------------------------------------------------------------
#  Отладочный рендерер меню (без GPU и без окна).
#
#    make -C tools/preview                 -> build/mlbb_preview
#    make -C tools/preview shot            -> build/esp.png (нужен ImageMagick)
#
#  ImGui подтягивается из сабмодуля external/imgui (v1.83):
#    git submodule update --init --recursive
# -----------------------------------------------------------------------------
IMGUI_DIR ?= ../../external/imgui
CXX       ?= g++
BUILD     ?= build

CXXFLAGS  ?= -std=c++17 -O2 -I../../include -I../../src -I$(IMGUI_DIR) -I. -Wall

# imgui_demo.cpp нужен не для демо-окна: в v1.83 именно в нём лежит
# ShowFontAtlas(), без которого imgui.cpp не линкуется.
IMGUI_SRC := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp \
             $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/imgui_tables.cpp \
             $(IMGUI_DIR)/imgui_demo.cpp
SRC       := ../../src/Menu.cpp ../../src/Theme.cpp ../../src/render/SoftRenderer.cpp main.cpp

VPATH     := .:../../src:../../src/render:$(IMGUI_DIR)
OBJ       := $(addprefix $(BUILD)/,$(notdir $(SRC:.cpp=.o))) \
             $(addprefix $(BUILD)/,$(notdir $(IMGUI_SRC:.cpp=.o)))
BIN       := $(BUILD)/mlbb_preview

# Шрифты для скриншотов: подставьте свои, если есть JetBrains Mono / Iosevka.
TAB       ?= 0
FONT      ?= /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
BOLD      ?= /usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf
BACKDROP  ?= build/backdrop.ppm

.PHONY: all clean shot backdrop

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

backdrop: $(BACKDROP)

$(BACKDROP): | $(BUILD)
	convert -size 1400x840 gradient:'#16301c'-'#050d07' \
	        \( -size 1400x840 xc:'#050d07' -fill '#2f7a3f' -draw 'circle 300,180 300,-40' -blur 0x110 \) \
	        -compose over -composite -depth 8 $@

shot: $(BIN) $(BACKDROP)
	$(BIN) --out $(BUILD)/menu.ppm --display 1400x840 --bg $(BACKDROP) \
	       --populate --tab $(TAB) --uptime 42 --height 680 --font $(FONT) --bold $(BOLD)
	convert $(BUILD)/menu.ppm $(BUILD)/menu.png
	@echo "готово: $(BUILD)/menu.png"

clean:
	rm -rf $(BUILD)
