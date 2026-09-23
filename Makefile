CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O3 -fvisibility=hidden -ffunction-sections -fdata-sections -fno-rtti -fno-exceptions -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -Wl,--gc-sections -Wl,--strip-all
LIBS ?= -llog -landroid -lEGL -lGLESv3 -lz -ldl

INCLUDES = \
    -Isrc \
    -Itemplate/include \
    -Itemplate/include/native_surface \
    -Itemplate/include/Android_Graphics \
    -Itemplate/include/Android_my_imgui \
    -Itemplate/include/Android_touch \
    -Itemplate/include/My_Utils \
    -Itemplate/include/My_Utils/stb_image \
    -Itemplate/include/ImGui \
    -Itemplate/include/ImGui/My_font \
    -Itemplate/include/ImGui/backends

SRCS = \
    src/main.cpp \
    src/draw.cpp \
    template/src/Android_touch/TouchHelperA.cpp \
    template/src/Android_Graphics/GraphicsManager.cpp \
    template/src/Android_Graphics/OpenGLGraphics.cpp \
    template/src/Android_my_imgui/AndroidImgui.cpp \
    template/src/Android_my_imgui/my_imgui.cpp \
    template/src/Android_my_imgui/my_imgui_impl_android.cpp \
    template/src/ImGui/imgui.cpp \
    template/src/ImGui/imgui_draw.cpp \
    template/src/ImGui/imgui_tables.cpp \
    template/src/ImGui/imgui_widgets.cpp \
    template/src/ImGui/backends/imgui_impl_opengl3.cpp \
    template/src/My_Utils/stb_image/stb_image.cpp

OBJS = $(SRCS:.cpp=.o)
TARGET = dist/cyber_overlay

all: $(TARGET) package

$(TARGET): $(OBJS)
	@mkdir -p dist
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "[+] Built native binary: $@"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

package: $(TARGET)
	@chmod +x tools/package.sh
	tools/package.sh $(TARGET) dist/cyber_overlay.sh
	@echo "[+] Packaged self-extracting shell asset: dist/cyber_overlay.sh"

clean:
	rm -f $(OBJS) $(TARGET) dist/cyber_overlay.sh dist/cyber_overlay.sh.sha256

.PHONY: all package clean
