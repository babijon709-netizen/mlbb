#include "overlay/SurfaceSink.h"

#include <dlfcn.h>

#include <cstdio>
#include <cstring>

namespace mlbb {
namespace overlay {

SurfaceSink::~SurfaceSink() { Close(); }

bool SurfaceSink::Open(void* nativeWindow, int fallbackWidth, int fallbackHeight)
{
    Close();

    if (!nativeWindow) { m_error = "no surface handle"; return false; }
    m_nativeWindow = nativeWindow;

    m_libAndroid = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!m_libAndroid) { m_error = "cannot dlopen libandroid.so"; return false; }

    m_lock          = reinterpret_cast<decltype(m_lock)>(dlsym(m_libAndroid, "ANativeWindow_lock"));
    m_unlockAndPost = reinterpret_cast<decltype(m_unlockAndPost)>(dlsym(m_libAndroid, "ANativeWindow_unlockAndPost"));
    m_getWidth     = reinterpret_cast<decltype(m_getWidth)>(dlsym(m_libAndroid, "ANativeWindow_getWidth"));
    m_getHeight    = reinterpret_cast<decltype(m_getHeight)>(dlsym(m_libAndroid, "ANativeWindow_getHeight"));
    m_getFormat    = reinterpret_cast<decltype(m_getFormat)>(dlsym(m_libAndroid, "ANativeWindow_getFormat"));

    if (!m_lock || !m_unlockAndPost || !m_getWidth || !m_getHeight) {
        m_error = "libandroid.so is missing the ANativeWindow entry points";
        Close();
        return false;
    }

    m_width  = m_getWidth(m_nativeWindow);
    m_height = m_getHeight(m_nativeWindow);
    m_format = m_getFormat ? m_getFormat(m_nativeWindow) : WINDOW_FORMAT_RGBA_8888;

    if (m_width <= 1 || m_height <= 1) {
        if (fallbackWidth > 1 && fallbackHeight > 1) {
            m_width  = fallbackWidth;
            m_height = fallbackHeight;
            m_error  = "";                       // the lock still tells us the real size
        } else {
            m_error = "surface has an empty size";
            Close();
            return false;
        }
    }

    return true;
}

bool SurfaceSink::Present(const unsigned char* pixels, int pitch)
{
    if (!m_nativeWindow || !m_lock || !pixels) { m_error = "sink is not open"; return false; }

    ANativeWindow_Buffer buffer;
    std::memset(&buffer, 0, sizeof(buffer));

    const int32_t result = m_lock(m_nativeWindow, &buffer, nullptr);
    if (result != 0) {
        // -11 (EAGAIN) just means the queue is still busy with a previous
        // frame, everything else is a real refusal (usually a buffer whose
        // usage bits do not allow CPU access).
        m_error = (result == -11) ? "buffer queue busy" : "ANativeWindow_lock refused the buffer";
        return false;
    }

    unsigned char* dst    = static_cast<unsigned char*>(buffer.bits);
    const int      width  = (m_width  < buffer.width)  ? m_width  : buffer.width;
    const int      height = (m_height < buffer.height) ? m_height : buffer.height;
    const int      stride = buffer.stride > 0 ? buffer.stride : width;

    if (!dst || stride < width) {
        m_unlockAndPost(m_nativeWindow);
        m_error = "surface handed back an unusable buffer";
        return false;
    }

    const bool premultiplied = (buffer.format == WINDOW_FORMAT_RGBA_8888);
    if (!m_loggedFormat) {
        m_loggedFormat = true;
        std::printf("overlay: surface %dx%d (stride %d) format %d%s\n",
                    buffer.width, buffer.height, stride, buffer.format,
                    premultiplied ? " rgba8888, per-pixel alpha" : " NOT rgba8888 - transparent areas will be black");
    }

    if (premultiplied) {
        for (int y = 0; y < height; ++y)
            std::memcpy(dst + (size_t)y * (size_t)stride * 4u,
                        pixels + (size_t)y * (size_t)pitch, (size_t)width * 4u);
    } else {
        // The layer ignores alpha, so undo the premultiplication - otherwise
        // the menu itself would come out darkened wherever it is translucent.
        unsigned char* out = dst;
        for (int y = 0; y < height; ++y) {
            const unsigned char* in = pixels + (size_t)y * (size_t)pitch;
            for (int x = 0; x < width; ++x, in += 4, out += 4) {
                const unsigned a = in[3];
                if (a == 255) { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; out[3] = 255; }
                else if (a == 0) { out[0] = out[1] = out[2] = out[3] = 0; }
                else {
                    out[0] = (unsigned char)((in[0] * 255u) / a);
                    out[1] = (unsigned char)((in[1] * 255u) / a);
                    out[2] = (unsigned char)((in[2] * 255u) / a);
                    out[3] = 255;
                }
            }
            out += (size_t)(stride - width) * 4u;
        }
    }

    if (m_unlockAndPost(m_nativeWindow) != 0) { m_error = "ANativeWindow_unlockAndPost failed"; return false; }

    m_error = "";
    return true;
}

void SurfaceSink::Close()
{
    // libandroid.so stays mapped for the lifetime of the process; dlclose()
    // would be pointless (and risky once a buffer is still in flight).
    m_nativeWindow = nullptr;
    m_lock = nullptr;
    m_unlockAndPost = nullptr;
    m_width = m_height = m_format = 0;
}

}  // namespace overlay
}  // namespace mlbb
