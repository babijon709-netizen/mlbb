#include "render/SoftRenderer.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace mlbb {
namespace render {

namespace {

inline int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int MinI(int a, int b) { return a < b ? a : b; }
inline int MaxI(int a, int b) { return a > b ? a : b; }
// x / 255 without an integer division - this is the hottest line of the whole
// renderer, and a divide costs an order of magnitude more than the shifts.
inline int Div255(int x) { return (x * 257 + 257) >> 16; }

}  // namespace

void SoftRenderer::Init(int width, int height, float pixelScale)
{
    m_width  = width  < 1 ? 1 : width;
    m_height = height < 1 ? 1 : height;
    m_scale  = pixelScale < 0.05f ? 0.05f : pixelScale;
    m_fbWidth  = (int)std::lround((double)m_width  * (double)m_scale);
    m_fbHeight = (int)std::lround((double)m_height * (double)m_scale);
    if (m_fbWidth  < 1) m_fbWidth  = 1;
    if (m_fbHeight < 1) m_fbHeight = 1;
    m_pixels.assign((size_t)m_fbWidth * (size_t)m_fbHeight, Color4());
}

void SoftRenderer::Clear(const Color4& c)
{
    Color4* p = m_pixels.data();
    const size_t n = m_pixels.size();
    for (size_t i = 0; i < n; ++i) p[i] = c;
}

bool SoftRenderer::LoadBackgroundPPM(const char* path)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    char magic[3] = { 0, 0, 0 };
    if (std::fscanf(f, "%2s", magic) != 1 || std::strcmp(magic, "P6") != 0) { std::fclose(f); return false; }

    auto skipJunk = [&]() {
        int ch;
        while ((ch = std::fgetc(f)) != EOF) {
            if (ch == '#') { while ((ch = std::fgetc(f)) != EOF && ch != '\n') {} }
            else if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') { std::ungetc(ch, f); break; }
        }
    };

    int iw = 0, ih = 0, maxv = 0;
    skipJunk(); if (std::fscanf(f, "%d", &iw) != 1) { std::fclose(f); return false; }
    skipJunk(); if (std::fscanf(f, "%d", &ih) != 1) { std::fclose(f); return false; }
    skipJunk(); if (std::fscanf(f, "%d", &maxv) != 1) { std::fclose(f); return false; }
    std::fgetc(f);  // exactly one whitespace after the header

    if (iw <= 0 || ih <= 0 || maxv <= 0) { std::fclose(f); return false; }

    // ImageMagick happily writes Q16 PPMs (two bytes per sample, maxval 65535),
    // so the sample size has to follow the header, not be assumed to be 1.
    const int sampleBytes = (maxv > 255) ? 2 : 1;
    std::vector<unsigned char> buf((size_t)iw * (size_t)ih * 3u * (size_t)sampleBytes);
    const size_t got = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (got != buf.size()) return false;

    auto sample = [&](size_t index) -> unsigned char {
        const unsigned char* p = buf.data() + index * (size_t)sampleBytes;
        if (sampleBytes == 1) return p[0];
        return (unsigned char)((((unsigned)p[0] << 8) | p[1]) * 255u / (unsigned)maxv);
    };

    for (int y = 0; y < m_fbHeight; ++y) {
        const int sy = ClampI(y * ih / m_fbHeight, 0, ih - 1);
        for (int x = 0; x < m_fbWidth; ++x) {
            const int sx = ClampI(x * iw / m_fbWidth, 0, iw - 1);
            const size_t si = ((size_t)sy * (size_t)iw + (size_t)sx) * 3u;
            Color4& d = m_pixels[(size_t)y * (size_t)m_fbWidth + (size_t)x];
            d.r = sample(si + 0);
            d.g = sample(si + 1);
            d.b = sample(si + 2);
            d.a = 255;
        }
    }
    return true;
}

void SoftRenderer::BlendPixel(int x, int y, const Color4& src)
{
    if (x < 0 || y < 0 || x >= m_fbWidth || y >= m_fbHeight) return;
    if (src.a == 0) return;

    Color4& dst = m_pixels[(size_t)y * (size_t)m_fbWidth + (size_t)x];
    if (src.a == 255) { dst.r = src.r; dst.g = src.g; dst.b = src.b; dst.a = 255; return; }

    const int a = src.a, ia = 255 - a;
    dst.r = (unsigned char)Div255(src.r * a + dst.r * ia);
    dst.g = (unsigned char)Div255(src.g * a + dst.g * ia);
    dst.b = (unsigned char)Div255(src.b * a + dst.b * ia);
    dst.a = 255;
}

Color4 SoftRenderer::SampleTexture(float u, float v, int texWidth, int texHeight) const
{
    if (!m_texels || texWidth <= 0 || texHeight <= 0) return Color4{ 255, 255, 255, 255 };

    const float x = u * (float)texWidth - 0.5f;
    const float y = v * (float)texHeight - 0.5f;
    const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    const float fx = x - (float)x0, fy = y - (float)y0;

    auto fetch = [&](int ix, int iy) -> Color4 {
        ix = ClampI(ix, 0, texWidth - 1);
        iy = ClampI(iy, 0, texHeight - 1);
        const unsigned char* p = m_texels + ((size_t)iy * (size_t)texWidth + (size_t)ix) * 4u;
        return Color4{ p[0], p[1], p[2], p[3] };
    };

    const Color4 c00 = fetch(x0, y0), c10 = fetch(x0 + 1, y0);
    const Color4 c01 = fetch(x0, y0 + 1), c11 = fetch(x0 + 1, y0 + 1);

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    Color4 out;
    out.r = (unsigned char)ClampF(lerp(lerp(c00.r, c10.r, fx), lerp(c01.r, c11.r, fx), fy), 0.0f, 255.0f);
    out.g = (unsigned char)ClampF(lerp(lerp(c00.g, c10.g, fx), lerp(c01.g, c11.g, fx), fy), 0.0f, 255.0f);
    out.b = (unsigned char)ClampF(lerp(lerp(c00.b, c10.b, fx), lerp(c01.b, c11.b, fx), fy), 0.0f, 255.0f);
    out.a = (unsigned char)ClampF(lerp(lerp(c00.a, c10.a, fx), lerp(c01.a, c11.a, fx), fy), 0.0f, 255.0f);
    return out;
}

void SoftRenderer::RasterizeTriangle(const ImDrawVert& va, const ImDrawVert& vb, const ImDrawVert& vc,
                                     const ImVec4& clip)
{
    const float S = m_scale;
    const float ax = va.pos.x * S, ay = va.pos.y * S;
    const float bx = vb.pos.x * S, by = vb.pos.y * S;
    const float cx = vc.pos.x * S, cy = vc.pos.y * S;

    const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (area > -0.0001f && area < 0.0001f) return;

    const int minX = MaxI((int)std::floor(std::fmin(ax, std::fmin(bx, cx))), (int)std::floor(clip.x * S));
    const int maxX = MinI((int)std::ceil (std::fmax(ax, std::fmax(bx, cx))), (int)std::ceil (clip.z * S));
    const int minY = MaxI((int)std::floor(std::fmin(ay, std::fmin(by, cy))), (int)std::floor(clip.y * S));
    const int maxY = MinI((int)std::ceil (std::fmax(ay, std::fmax(by, cy))), (int)std::ceil (clip.w * S));

    const float invArea = 1.0f / area;

    // ---- flat fill fast path -------------------------------------------------
    // ImGui draws every filled rectangle/rounded rect as a solid colour pair of
    // triangles using the atlas' white pixel, so this path covers most of the
    // screen: no texture fetch, no per-pixel interpolation.
    const bool solidUV  = (va.uv.x == vb.uv.x && va.uv.y == vb.uv.y && va.uv.x == vc.uv.x && va.uv.y == vc.uv.y);
    const bool solidCol = (va.col == vb.col && vb.col == vc.col);
    if (solidUV && solidCol) {
        const ImU32 c = va.col;
        const Color4 src{ (unsigned char)((c >> IM_COL32_R_SHIFT) & 0xFF),
                          (unsigned char)((c >> IM_COL32_G_SHIFT) & 0xFF),
                          (unsigned char)((c >> IM_COL32_B_SHIFT) & 0xFF),
                          (unsigned char)((c >> IM_COL32_A_SHIFT) & 0xFF) };
        const bool opaque = (src.a == 255);

        for (int y = MaxI(minY, 0); y <= MinI(maxY, m_fbHeight - 1); ++y) {
            const float py = (float)y + 0.5f;
            Color4* row = &m_pixels[(size_t)y * (size_t)m_fbWidth];
            for (int x = MaxI(minX, 0); x <= MinI(maxX, m_fbWidth - 1); ++x) {
                const float px = (float)x + 0.5f;
                const float l0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) * invArea;
                if (l0 < 0.0f) continue;
                const float l1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) * invArea;
                if (l1 < 0.0f) continue;
                if (l0 + l1 > 1.0f) continue;

                if (opaque) { row[x] = src; continue; }
                const int a = src.a, ia = 255 - a;
                Color4& dst = row[x];
                dst.r = (unsigned char)Div255(src.r * a + dst.r * ia);
                dst.g = (unsigned char)Div255(src.g * a + dst.g * ia);
                dst.b = (unsigned char)Div255(src.b * a + dst.b * ia);
                dst.a = 255;
            }
        }
        return;
    }

    for (int y = MaxI(minY, 0); y <= MinI(maxY, m_fbHeight - 1); ++y) {
        for (int x = MaxI(minX, 0); x <= MinI(maxX, m_fbWidth - 1); ++x) {
            const float px = (float)x + 0.5f;
            const float py = (float)y + 0.5f;

            const float l0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) * invArea;
            const float l1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) * invArea;
            const float l2 = 1.0f - l0 - l1;
            if (l0 < 0.0f || l1 < 0.0f || l2 < 0.0f) continue;

            const float u = l0 * va.uv.x + l1 * vb.uv.x + l2 * vc.uv.x;
            const float v = l0 * va.uv.y + l1 * vb.uv.y + l2 * vc.uv.y;
            const Color4 tex = SampleTexture(u, v, m_texWidth, m_texHeight);

            const ImU32 ca = va.col, cb = vb.col, cc = vc.col;
            const float cr  = ((ca >> IM_COL32_R_SHIFT) & 0xFF) * l0 + ((cb >> IM_COL32_R_SHIFT) & 0xFF) * l1 + ((cc >> IM_COL32_R_SHIFT) & 0xFF) * l2;
            const float cg  = ((ca >> IM_COL32_G_SHIFT) & 0xFF) * l0 + ((cb >> IM_COL32_G_SHIFT) & 0xFF) * l1 + ((cc >> IM_COL32_G_SHIFT) & 0xFF) * l2;
            const float cbb = ((ca >> IM_COL32_B_SHIFT) & 0xFF) * l0 + ((cb >> IM_COL32_B_SHIFT) & 0xFF) * l1 + ((cc >> IM_COL32_B_SHIFT) & 0xFF) * l2;
            const float calpha = ((ca >> IM_COL32_A_SHIFT) & 0xFF) * l0 + ((cb >> IM_COL32_A_SHIFT) & 0xFF) * l1 + ((cc >> IM_COL32_A_SHIFT) & 0xFF) * l2;

            Color4 src;
            src.r = (unsigned char)ClampF(cr  * (float)tex.r / 255.0f, 0.0f, 255.0f);
            src.g = (unsigned char)ClampF(cg  * (float)tex.g / 255.0f, 0.0f, 255.0f);
            src.b = (unsigned char)ClampF(cbb * (float)tex.b / 255.0f, 0.0f, 255.0f);
            src.a = (unsigned char)ClampF(calpha * (float)tex.a / 255.0f, 0.0f, 255.0f);

            BlendPixel(x, y, src);
        }
    }
}

void SoftRenderer::Render(const ImDrawData* drawData, const unsigned char* texels, int texWidth, int texHeight)
{
    if (!drawData) return;
    m_texels = texels;
    m_texWidth = texWidth;
    m_texHeight = texHeight;

    const ImVec2 disp = drawData->DisplayPos;

    for (int n = 0; n < drawData->CmdListsCount; ++n) {
        const ImDrawList* list = drawData->CmdLists[n];
        const ImDrawVert* vtxBuffer = list->VtxBuffer.Data;
        const ImDrawIdx*  idxBuffer = list->IdxBuffer.Data;

        for (int ci = 0; ci < list->CmdBuffer.Size; ++ci) {
            const ImDrawCmd* cmd = &list->CmdBuffer[ci];
            if (cmd->UserCallback || cmd->ElemCount == 0) continue;

            ImVec4 clip = cmd->ClipRect;
            clip.x -= disp.x; clip.y -= disp.y; clip.z -= disp.x; clip.w -= disp.y;
            if (clip.z <= clip.x || clip.w <= clip.y) continue;

            const ImDrawIdx* idx = idxBuffer + cmd->IdxOffset;
            const int vtxOff = cmd->VtxOffset;

            for (unsigned int e = 0; e + 2 < cmd->ElemCount; e += 3) {
                RasterizeTriangle(vtxBuffer[vtxOff + idx[e + 0]],
                                  vtxBuffer[vtxOff + idx[e + 1]],
                                  vtxBuffer[vtxOff + idx[e + 2]],
                                  clip);
            }
        }
    }
}

bool SoftRenderer::SavePPM(const char* path, int downsample) const
{
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    const int S = downsample < 1 ? 1 : downsample;
    const int outW = m_fbWidth / S;
    const int outH = m_fbHeight / S;
    if (outW < 1 || outH < 1) { std::fclose(f); return false; }

    std::fprintf(f, "P6\n%d %d\n255\n", outW, outH);

    const int samples = S * S;
    std::vector<unsigned char> out((size_t)outW * (size_t)outH * 3u);

    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            int r = 0, g = 0, b = 0;
            for (int sy = 0; sy < S; ++sy) {
                const Color4* row = &m_pixels[(size_t)(y * S + sy) * (size_t)m_fbWidth + (size_t)(x * S)];
                for (int sx = 0; sx < S; ++sx) { r += row[sx].r; g += row[sx].g; b += row[sx].b; }
            }
            const size_t o = ((size_t)y * (size_t)outW + (size_t)x) * 3u;
            out[o + 0] = (unsigned char)(r / samples);
            out[o + 1] = (unsigned char)(g / samples);
            out[o + 2] = (unsigned char)(b / samples);
        }
    }

    const size_t written = std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return written == out.size();
}

}  // namespace render
}  // namespace mlbb
