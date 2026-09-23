// -----------------------------------------------------------------------------
//  SurfaceSink.h - writes CPU-rendered frames into an Android surface.
//
//  The overlay draws with the software rasterizer, so all it needs from the
//  system is "here is a buffer, please show it". On Android that is the
//  ANativeWindow_lock()/unlockAndPost() pair (the same CPU producer path
//  SurfaceView's lockCanvas() uses). Those two functions live in libandroid.so,
//  which is opened with dlopen on purpose: the overlay then links against
//  nothing but libc, i.e. it builds with Termux's clang and runs from
//  /data/local/tmp without shipping a single .so next to it.
//
//  The buffer is expected in premultiplied RGBA8888 - the layout a
//  WINDOW_FORMAT_RGBA_8888 layer wants, and what SoftRenderer produces in
//  `SetPremultiplied(true)` mode. Pixels with alpha 0 are fully transparent, so
//  whatever is on screen underneath (the game) stays visible.
// -----------------------------------------------------------------------------
#pragma once

#include "ndk_compat.h"

namespace mlbb {
namespace overlay {

class SurfaceSink {
public:
    SurfaceSink() = default;
    ~SurfaceSink();

    SurfaceSink(const SurfaceSink&) = delete;
    SurfaceSink& operator=(const SurfaceSink&) = delete;

    // `nativeWindow` is the ANativeWindow* handed out by the surface helper.
    // Resolves the libandroid.so entry points and reads the buffer geometry;
    // `fallbackWidth`/`fallbackHeight` (the display size) are used when the
    // window does not report one yet.
    bool Open(void* nativeWindow, int fallbackWidth = 0, int fallbackHeight = 0);

    // Copies `pixels` (premultiplied RGBA8888, `pitch` bytes per row) into the
    // next buffer of the queue and posts it. Returns false when the queue
    // refuses the buffer - see Error().
    bool Present(const unsigned char* pixels, int pitch);

    int  Width()  const { return m_width; }
    int  Height() const { return m_height; }
    int  Format() const { return m_format; }
    // True when the layer honours per-pixel alpha (i.e. the game shows through
    // the transparent parts of the menu).
    bool AlphaCapable() const { return m_format == 1; }

    const char* Error() const { return m_error; }

    void Close();

private:
    void* m_nativeWindow = nullptr;
    void* m_libAndroid   = nullptr;

    int32_t (*m_lock)(void*, ANativeWindow_Buffer*, void*) = nullptr;
    int32_t (*m_unlockAndPost)(void*) = nullptr;
    int32_t (*m_getWidth)(void*) = nullptr;
    int32_t (*m_getHeight)(void*) = nullptr;
    int32_t (*m_getFormat)(void*) = nullptr;

    int  m_width = 0, m_height = 0, m_format = 0;
    bool m_loggedFormat = false;
    const char* m_error = "";
};

}  // namespace overlay
}  // namespace mlbb
