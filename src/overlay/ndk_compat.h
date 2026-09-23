// -----------------------------------------------------------------------------
//  ndk_compat.h - the handful of NDK declarations the overlay needs.
//
//  The overlay is a plain ELF that talks to SurfaceFlinger directly, so it only
//  needs three things from the NDK: the ANativeWindow handle type, a logger and
//  __system_property_get().  Termux's clang ships a bionic sysroot but no NDK
//  headers, so instead of <android/native_window.h>, <android/log.h> and
//  <sys/system_properties.h> the overlay translation units include this file.
//  It is deliberately self-contained: no NDK, no liblog, no <android/*>.
//
//  Nothing here is passed to a real NDK API, so the declarations cannot clash
//  with the platform headers.  The only entries that must match the platform
//  ABI exactly are ANativeWindow_Buffer (filled by ANativeWindow_lock) and the
//  window formats.
// -----------------------------------------------------------------------------
#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

// ------------------------------------------------ android/native_window.h ----
// Opaque everywhere it is used: the pointer is created by the surface helper
// and only handed to the ANativeWindow_* functions, which are resolved with
// dlsym (see SurfaceSink.cpp).
typedef struct ANativeWindow ANativeWindow;

// ANativeWindow_lock() fills this in.  The leading fields are what we read; the
// reserved tail is oversized on purpose, so a differing reserved[] length in
// the platform header can never make the call write past the struct.
typedef struct ANativeWindow_Buffer {
    int32_t  width;
    int32_t  height;
    int32_t  stride;
    int32_t  format;
    void*    bits;
    uint32_t reserved[16];
} ANativeWindow_Buffer;

enum {
    WINDOW_FORMAT_RGBA_8888 = 1,   // R,G,B,A in memory order, alpha honoured
    WINDOW_FORMAT_RGBX_8888 = 2,   // same layout, alpha ignored by the compositor
    WINDOW_FORMAT_RGB_565   = 4
};

// --------------------------------------------------------- android/log.h ----
typedef enum {
    ANDROID_LOG_UNKNOWN = 0,
    ANDROID_LOG_DEFAULT,
    ANDROID_LOG_VERBOSE,
    ANDROID_LOG_DEBUG,
    ANDROID_LOG_INFO,
    ANDROID_LOG_WARN,
    ANDROID_LOG_ERROR,
    ANDROID_LOG_FATAL,
    ANDROID_LOG_SILENT
} android_LogPriority;

// Logs to logcat *and* to stderr: the overlay is usually started from a Termux
// shell (so the shell shows the messages), but when it is started from another
// app or a Magisk service only logcat is readable.  liblog is resolved lazily
// with dlsym, which keeps the binary free of a link-time dependency on it.
inline int mlbb_log_print(int prio, const char* tag, const char* fmt, ...)
{
#if defined(__ANDROID__)
    typedef int (*LogPrintFn)(int, const char*, const char*, ...);
    static LogPrintFn androidLogPrint = []() -> LogPrintFn {
        void* handle = dlopen("liblog.so", RTLD_NOW | RTLD_LOCAL);
        if (!handle) handle = dlopen("liblog.so.0", RTLD_NOW | RTLD_LOCAL);
        return handle ? reinterpret_cast<LogPrintFn>(dlsym(handle, "__android_log_print")) : nullptr;
    }();

    va_list ap;
    va_start(ap, fmt);
    char line[1024];
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (androidLogPrint) {
        androidLogPrint(prio, tag, "%s", line);
        return 0;
    }

    fprintf(stderr, "[%s] %s\n", tag, line);
    return 0;
#else
    // Host build (`make overlay` on a PC): stderr only, no liblog anywhere.
    (void)prio;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s] ", tag);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    return 0;
#endif
}

// ------------------------------------------------- sys/system_properties.h ----
// Exported by bionic's libc, so no stub library is needed.  On a PC build the
// stub below keeps the surface helper linkable (it only ever asks for
// ro.build.version.release, and the volume check happens before that).
#if defined(__ANDROID__)
extern "C" int __system_property_get(const char* name, char* value);
#else
static inline int __system_property_get(const char* /*name*/, char* value)
{
    if (value) value[0] = '\0';
    return 0;
}
#endif
