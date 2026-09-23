// -----------------------------------------------------------------------------
//  SoftRenderer.h - CPU rasterizer for ImGui draw data.
//
//  Renders ImGui's ImDrawData into a plain RGBA byte buffer without any GPU,
//  window system or driver. Two consumers:
//
//    * tools/preview  - writes the menu into a PPM image (documentation shots);
//    * src/app        - the actual app: the buffer is blitted to an SDL window
//                       surface, which is how the menu runs on a phone.
//
//  `pixelScale` is the framebuffer scale: the ImGui canvas stays in logical
//  units (so a 1120x680 menu keeps its layout), while everything is rasterized
//  at that multiple of the resolution. Combined with fonts rasterized at the
//  same density (Config::pixelDensity) it gives crisp text on a phone screen.
// -----------------------------------------------------------------------------
#pragma once

#include "imgui.h"

#include <vector>

namespace mlbb {
namespace render {

struct Color4 {
    unsigned char r = 0, g = 0, b = 0, a = 0;
};

class SoftRenderer {
public:
    // Sizes the framebuffer to (logicalWidth x logicalHeight) * pixelScale.
    void Init(int logicalWidth, int logicalHeight, float pixelScale = 1.0f);

    // Fills the whole framebuffer with one color.
    void Clear(const Color4& c);

    // Loads a PPM (P6) image and stretches it over the framebuffer.
    // Returns false if the file could not be read.
    bool LoadBackgroundPPM(const char* path);

    // Texture filtering. Bilinear (default) suits the preview tool, where
    // glyphs are rasterized at 1x and the frame is supersampled. Nearest is
    // crisper *and* ~2x faster when the atlas is rasterized at exactly the
    // framebuffer scale - which is what the app does on a phone.
    enum Filter { FilterBilinear, FilterNearest };
    void SetFilter(Filter f) { m_filter = f; }

    // Rasterizes ImGui draw data. `texels` is the font atlas (RGBA32).
    void Render(const ImDrawData* drawData, const unsigned char* texels, int texWidth, int texHeight);

    // Writes a PPM (P6) file. `downsample` averages blocks of that size, so
    // passing the pixel scale reproduces a logical-resolution image while
    // keeping the anti-aliasing. Pass 1 to store the framebuffer 1:1.
    bool SavePPM(const char* path, int downsample = 1) const;

    // Raw RGBA32 pixels, ready to be wrapped into an SDL_Surface.
    unsigned char*       Data()       { return reinterpret_cast<unsigned char*>(m_pixels.data()); }
    const unsigned char* Data() const { return reinterpret_cast<const unsigned char*>(m_pixels.data()); }
    int Pitch() const { return m_fbWidth * 4; }

    int FramebufferWidth()  const { return m_fbWidth; }
    int FramebufferHeight() const { return m_fbHeight; }
    int Width()  const { return m_width; }       // logical size
    int Height() const { return m_height; }
    float PixelScale() const { return m_scale; }

private:
    void   BlendPixel(int x, int y, const Color4& src);
    Color4 SampleTexture(float u, float v, int texWidth, int texHeight) const;
    void   RasterizeTriangle(const ImDrawVert& a, const ImDrawVert& b, const ImDrawVert& c,
                             const ImVec4& clip);

    int   m_width = 0, m_height = 0;            // logical size
    int   m_fbWidth = 0, m_fbHeight = 0;        // size of m_pixels
    float m_scale = 1.0f;
    std::vector<Color4> m_pixels;

    const unsigned char* m_texels = nullptr;
    int m_texWidth = 0, m_texHeight = 0;
    Filter m_filter = FilterBilinear;
};

}  // namespace render
}  // namespace mlbb
