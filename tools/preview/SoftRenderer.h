// -----------------------------------------------------------------------------
//  SoftRenderer.h - CPU rasterizer for ImGui draw data.
//
//  The preview tool renders the menu without any GPU / window system: ImGui
//  produces ImDrawData, this renders it into a plain RGBA buffer which is then
//  written out as an image. Handy on machines (or CI) where SDL2/GL is not
//  available.
// -----------------------------------------------------------------------------
#pragma once

#include "imgui.h"

#include <vector>

namespace mlbb_preview {

struct Color4 {
    unsigned char r = 0, g = 0, b = 0, a = 0;
};

class SoftRenderer {
public:
    // `scale` = supersampling factor (2 renders at twice the resolution).
    void Init(int width, int height, int scale = 1);

    // Fills the whole target with one color.
    void Clear(const Color4& c);

    // Loads a PPM (P6) image as the background. Returns false if it failed.
    bool LoadBackgroundPPM(const char* path);

    // Rasterizes ImGui draw data. `texels` is the font atlas (RGBA32).
    void Render(const ImDrawData* drawData, const unsigned char* texels, int texWidth, int texHeight);

    // Writes a PPM (P6) file, downsampled back to the logical size.
    bool SavePPM(const char* path) const;

    int Width() const { return m_width; }
    int Height() const { return m_height; }

private:
    void BlendPixel(int x, int y, const Color4& src);
    Color4 SampleTexture(float u, float v, int texWidth, int texHeight) const;
    void RasterizeTriangle(const ImDrawVert& a, const ImDrawVert& b, const ImDrawVert& c,
                           const ImVec4& clip, int fbWidth, int fbHeight);

    int m_width = 0, m_height = 0;
    int m_scale = 1;
    std::vector<Color4> m_pixels;     // m_width*scale x m_height*scale
    const unsigned char* m_texels = nullptr;
    int m_texWidth = 0, m_texHeight = 0;
};

}  // namespace mlbb_preview
