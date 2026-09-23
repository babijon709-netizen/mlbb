/*
 * MIT License
 *
 * Copyright (c) 2023 AFan4724
 * Project: https://github.com/AFan4724/AndroidSurfaceImgui-Enhanced
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * ---------------------------------------------------------------------------
 *  Vendored, with modifications, from Qimgui (jw forks of
 *  AFan4724/AndroidSurfaceImgui-Enhanced) - MIT, see the licence above.
 *
 *  Changes for this project (SPECTRE overlay target):
 *    1. the four NDK includes were replaced by "ndk_compat.h", so the file
 *       builds with Termux's clang, which has no NDK sysroot;
 *    2. mlbb_log_print() -> mlbb_log_print() (logcat + stderr, no liblog);
 *    3. __VA_OPT__(,) -> ##__VA_ARGS__ so the file stays C++17;
 *    4. std::max_align_t -> ::max_align_t (<cstddef> is included), which is
 *       what both libc++ and libstdc++ provide;
 *    5. everything else (symbol names, versions, call order) is untouched.
 *
 *  What it does: walks the mangled symbols of /system/lib64/libgui.so and
 *  /system/lib64/libutils.so, builds a SurfaceControl through
 *  SurfaceComposerClient and hands back the ANativeWindow of that surface.
 *  The layer is created with TrustedOverlay(true) + SetLayer(INT_MAX), i.e.
 *  topmost and *outside* the input dispatch tree - touches keep reaching the
 *  app underneath while we read them from /dev/input ourselves.
 *
 *  Requires root: creating a surface for a foreign layer stack is a
 *  system-only binder call.
 * ---------------------------------------------------------------------------
 */

#ifndef A_NATIVE_WINDOW_CREATOR_H // !A_NATIVE_WINDOW_CREATOR_H
#define A_NATIVE_WINDOW_CREATOR_H

#include "ndk_compat.h"

#include <dlfcn.h>
#include <elf.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <climits>
#include <cstddef>

// Log system configuration
#ifndef SURFACE_LOG_TAG
#define SURFACE_LOG_TAG "AImGui"
#endif

#ifndef SURFACE_LOG_ENABLE
#define SURFACE_LOG_ENABLE 1  // Set to 0 to completely disable logging
#endif

// Log level control
#ifndef SURFACE_LOG_LEVEL
#define SURFACE_LOG_LEVEL_ERROR   1
#define SURFACE_LOG_LEVEL_WARN    2
#define SURFACE_LOG_LEVEL_INFO    3
#define SURFACE_LOG_LEVEL_DEBUG   4
#define SURFACE_LOG_LEVEL         SURFACE_LOG_LEVEL_DEBUG  // Default DEBUG level
#endif

// Unified log macro definitions
#if SURFACE_LOG_ENABLE
    #define SURFACE_LOG_ERROR(fmt, ...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_ERROR) \
                mlbb_log_print(ANDROID_LOG_ERROR, SURFACE_LOG_TAG, "[-] " fmt , ##__VA_ARGS__); \
        } while(0)
    
    #define SURFACE_LOG_WARN(fmt, ...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_WARN) \
                mlbb_log_print(ANDROID_LOG_WARN, SURFACE_LOG_TAG, "[!] " fmt , ##__VA_ARGS__); \
        } while(0)
    
    #define SURFACE_LOG_INFO(fmt, ...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_INFO) \
                mlbb_log_print(ANDROID_LOG_INFO, SURFACE_LOG_TAG, "[+] " fmt , ##__VA_ARGS__); \
        } while(0)
    
    #define SURFACE_LOG_DEBUG(fmt, ...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_DEBUG) \
                mlbb_log_print(ANDROID_LOG_DEBUG, SURFACE_LOG_TAG, "[*] " fmt , ##__VA_ARGS__); \
        } while(0)
    
    #define SURFACE_LOG_TRACE(fmt, ...) \
        do { \
            if (SURFACE_LOG_LEVEL >= SURFACE_LOG_LEVEL_DEBUG) \
                mlbb_log_print(ANDROID_LOG_DEBUG, SURFACE_LOG_TAG, "[=] " fmt , ##__VA_ARGS__); \
        } while(0)
#else
    #define SURFACE_LOG_ERROR(fmt, ...)   ((void)0)
    #define SURFACE_LOG_WARN(fmt, ...)    ((void)0)
    #define SURFACE_LOG_INFO(fmt, ...)    ((void)0)
    #define SURFACE_LOG_DEBUG(fmt, ...)   ((void)0)
    #define SURFACE_LOG_TRACE(fmt, ...)   ((void)0)
#endif

#define ResolveMethod(ClassName, MethodName, Handle, MethodSignature)                                                                    \
    ClassName##__##MethodName = reinterpret_cast<decltype(ClassName##__##MethodName)>(symbolMethod.Find(Handle, MethodSignature));       \
    if (nullptr == ClassName##__##MethodName)                                                                                            \
    {                                                                                                                                    \
        SURFACE_LOG_ERROR("Method not found: %s -> %s::%s", MethodSignature, #ClassName, #MethodName); \
    }

// AImGui 加固说明（不改变任何公开签名）：
//
//   - ProcessMirrorDisplay()  / EnableAutoMirrorDisplay() 变为空操作。
//     原先它们每秒 popen("dumpsys display") 解析输出并创建镜像 Surface，
//     这是本文件里唯一调用外部命令、唯一会额外创建 SurfaceControl 的路径。
//     现在覆盖层不再镜像到其他 layerStack；屏幕录制 / 投屏时看不到覆盖层。
//
//   - skipScrenshot 参数（CreateSurface / Create）被忽略。
//     原先会设 eSkipScreenshot 让覆盖层在录屏里隐身；现在无论调用方传
//     什么，覆盖层都正常参与录屏。
//
//   - TrustedOverlay(true) + SetLayer(INT_MAX) 保留 —— 触摸穿透与置顶是
//     本程序可用的前提条件。
//
//   - 所有函数签名、命名空间、调用顺序、返回值语义均与加固前一致。

namespace android {
    namespace detail {
        namespace ui {
            // A LayerStack identifies a Z-ordered group of layers. A layer can only be associated to a single
            // LayerStack, but a LayerStack can be associated to multiple displays, mirroring the same content.
            struct LayerStack
            {
                uint32_t id = UINT32_MAX;
            };

            enum class Rotation
            {
                Rotation0 = 0,
                Rotation90 = 1,
                Rotation180 = 2,
                Rotation270 = 3
            };

            // A simple value type representing a two-dimensional size.
            struct Size
            {
                int32_t width = -1;
                int32_t height = -1;
            };

            // Transactional state of physical or virtual display. Note that libgui defines
            // android::DisplayState as a superset of android::ui::DisplayState.
            struct DisplayState
            {
                LayerStack layerStack;
                Rotation orientation = Rotation::Rotation0;
                Size layerStackSpaceRect;
            };

            typedef int64_t nsecs_t; // nano-seconds
            struct DisplayInfo
            {
                uint32_t w{0};
                uint32_t h{0};
                float xdpi{0};
                float ydpi{0};
                float fps{0};
                float density{0};
                uint8_t orientation{0};
                bool secure{false};
                nsecs_t appVsyncOffset{0};
                nsecs_t presentationDeadline{0};
                uint32_t viewportW{0};
                uint32_t viewportH{0};
            };

            enum class DisplayType
            {
                DisplayIdMain = 0,
                DisplayIdHdmi = 1
            };

            struct PhysicalDisplayId
            {
                uint64_t value;
            };

            struct Rect
            {
                int32_t left;
                int32_t top;
                int32_t right;
                int32_t bottom;
            };
        }

        struct String8;

        struct LayerMetadata;

        struct Surface;

        struct SurfaceControl;

        struct SurfaceComposerClientTransaction;

        struct SurfaceComposerClient;

        template <typename any_t>
        struct StrongPointer
        {
            union
            {
                any_t *pointer;
                char padding[sizeof(::max_align_t)];
            };

            inline any_t *operator->() const { return pointer; }
            inline any_t *get() const { return pointer; }
            inline explicit operator bool() const { return nullptr != pointer; }
        };

        // Walk the dynamic symbol table of a shared object on disk and invoke
        // visit(name) for every *defined* exported symbol. visit returns true
        // to stop early. Reads straight off disk so it works regardless of how
        // the .so was loaded.
        template <typename Fn>
        inline void EnumerateDynSyms(const char *libPath, Fn &&visit)
        {
            FILE *fp = fopen(libPath, "rb");
            if (!fp)
                return;

            auto readAt = [&](void *dst, size_t size, long off) -> bool {
                if (0 != fseek(fp, off, SEEK_SET))
                    return false;
                return fread(dst, 1, size, fp) == size;
            };

#ifdef __LP64__
            using Ehdr = Elf64_Ehdr; using Shdr = Elf64_Shdr; using Sym = Elf64_Sym;
#else
            using Ehdr = Elf32_Ehdr; using Shdr = Elf32_Shdr; using Sym = Elf32_Sym;
#endif

            Ehdr ehdr{};
            if (!readAt(&ehdr, sizeof(ehdr), 0) ||
                0 != memcmp(ehdr.e_ident, ELFMAG, SELFMAG) ||
                sizeof(Shdr) != ehdr.e_shentsize || 0 == ehdr.e_shnum)
            {
                fclose(fp);
                return;
            }

            std::vector<Shdr> sections(ehdr.e_shnum);
            if (!readAt(sections.data(), sizeof(Shdr) * ehdr.e_shnum, static_cast<long>(ehdr.e_shoff)))
            {
                fclose(fp);
                return;
            }

            for (const auto &sh : sections)
            {
                if (SHT_DYNSYM != sh.sh_type || 0 == sh.sh_entsize)
                    continue;
                if (sh.sh_link >= sections.size())
                    continue;

                const Shdr &strtab = sections[sh.sh_link];

                std::string strbuf(strtab.sh_size, '\0');
                if (!readAt(strbuf.data(), strtab.sh_size, static_cast<long>(strtab.sh_offset)))
                    continue;

                std::vector<Sym> syms(sh.sh_size / sh.sh_entsize);
                if (!readAt(syms.data(), sh.sh_size, static_cast<long>(sh.sh_offset)))
                    continue;

                for (const auto &sym : syms)
                {
                    if (0 == sym.st_name || sym.st_name >= strbuf.size())
                        continue;
                    if (SHN_UNDEF == sym.st_shndx) // skip undefined imports
                        continue;

                    if (visit(strbuf.data() + sym.st_name))
                    {
                        fclose(fp);
                        return;
                    }
                }
            }

            fclose(fp);
        }

        // Return the first defined export whose mangled name contains `token`
        // and (if given) ends with `requiredSuffix`. The suffix lets callers
        // demand a specific ABI (parameter mangling) so we never bind an
        // overload we don't know how to call.
        inline std::string FindDynSymContaining(const char *libPath, const char *token,
                                                const char *requiredSuffix = nullptr)
        {
            std::string result;
            const size_t suffixLen = requiredSuffix ? strlen(requiredSuffix) : 0;

            EnumerateDynSyms(libPath, [&](const char *name) -> bool {
                if (!strstr(name, token))
                    return false;
                if (requiredSuffix)
                {
                    const size_t n = strlen(name);
                    if (n < suffixLen || 0 != strcmp(name + n - suffixLen, requiredSuffix))
                        return false;
                }
                result = name;
                return true;
            });

            return result;
        }

        struct Functionals
        {
            struct SymbolMethod
            {
                void *(*Open)(const char *filename, int flag) = nullptr;
                void *(*Find)(void *handle, const char *symbol) = nullptr;
                int (*Close)(void *handle) = nullptr;
            };

            size_t systemVersion = 13;

            void (*RefBase__IncStrong)(void *thiz, void *id) = nullptr;
            void (*RefBase__DecStrong)(void *thiz, void *id) = nullptr;

            void (*String8__Constructor)(void *thiz, const char *const data) = nullptr;
            void (*String8__Destructor)(void *thiz) = nullptr;

            void (*LayerMetadata__Constructor)(void *thiz) = nullptr;
            void (*LayerMetadata__setInt32)(void *thiz, uint32_t key, int32_t value) = nullptr;

            void (*SurfaceComposerClient__Constructor)(void *thiz) = nullptr;
            void (*SurfaceComposerClient__Destructor)(void *thiz) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__CreateSurface)(void *thiz, void *name, uint32_t w, uint32_t h, int32_t format, uint32_t flags, void *parentHandle, void *layerMetadata, uint32_t *outTransformHint) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__CreateSurface_and8)(void *thiz, void *name, uint32_t w, uint32_t h, int32_t format, uint32_t flags, void *parentHandle, uint32_t windowType, uint32_t ownerUid) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__CreateSurface_and9)(void *thiz, void *name, uint32_t w, uint32_t h, int32_t format, uint32_t flags, void *parentHandle, int32_t windowType, int32_t ownerUid) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__MirrorSurface)(void *thiz, void *mirrorFromSurface) = nullptr;
            // Android 14+/16: mirrorSurface gained a second SurfaceControl* parent.
            StrongPointer<void> (*SurfaceComposerClient__MirrorSurface2)(void *thiz, void *mirrorFromSurface, void *parent) = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__GetInternalDisplayToken)() = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__GetBuiltInDisplay)(ui::DisplayType type) = nullptr;
            int32_t (*SurfaceComposerClient__GetDisplayState)(StrongPointer<void> &display, ui::DisplayState *displayState) = nullptr;
            int32_t (*SurfaceComposerClient__GetDisplayInfo)(StrongPointer<void> &display, ui::DisplayInfo *displayInfo) = nullptr;
            std::vector<ui::PhysicalDisplayId> (*SurfaceComposerClient__GetPhysicalDisplayIds)() = nullptr;
            StrongPointer<void> (*SurfaceComposerClient__GetPhysicalDisplayToken)(ui::PhysicalDisplayId displayId) = nullptr;

            void (*SurfaceComposerClient__OpenGlobalTransaction)() = nullptr;
            void (*SurfaceComposerClient__CloseGlobalTransaction)(bool synchronous) = nullptr;

            void (*SurfaceComposerClient__Transaction__Constructor)(void *thiz) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetLayer)(void *thiz, StrongPointer<void> &surfaceControl, int32_t z) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetTrustedOverlay)(void *thiz, StrongPointer<void> &surfaceControl, bool isTrustedOverlay) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetLayerStack)(void *thiz, StrongPointer<void> &surfaceControl, uint32_t layerStack) = nullptr;
            void *(*SurfaceComposerClient__Transaction__Show)(void *thiz, StrongPointer<void> &surfaceControl) = nullptr;
            void *(*SurfaceComposerClient__Transaction__Hide)(void *thiz, StrongPointer<void> &surfaceControl) = nullptr;
            void *(*SurfaceComposerClient__Transaction__Reparent)(void *thiz, StrongPointer<void> &surfaceControl, StrongPointer<void> &newParentHandle) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetMatrix)(void *thiz, StrongPointer<void> &surfaceControl, float dsdx, float dtdx, float dtdy, float dsdy) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetPosition)(void *thiz, StrongPointer<void> &surfaceControl, float x, float y) = nullptr;
            int32_t (*SurfaceComposerClient__Transaction__Apply)(void *thiz, bool synchronous, bool oneWay) = nullptr;

            int32_t (*SurfaceControl__Validate)(void *thiz) = nullptr;
            StrongPointer<Surface> (*SurfaceControl__GetSurface)(void *thiz) = nullptr;
            void (*SurfaceControl__DisConnect)(void *thiz) = nullptr;
            void *(*SurfaceControl__SetLayer)(void *thiz, int32_t z) = nullptr;
            
            // Surface related methods
            void (*Surface__DisConnect)(void *thiz, int32_t api) = nullptr;

            Functionals(const SymbolMethod &symbolMethod)
            {
                std::string systemVersionString(128, 0);

                systemVersionString.resize(__system_property_get("ro.build.version.release", systemVersionString.data()));
                if (!systemVersionString.empty())
                    systemVersion = std::stoi(systemVersionString);

                if (5 > systemVersion)
                {
                    SURFACE_LOG_ERROR("Unsupported system version: %zu", systemVersion);
                    return;
                }

#ifdef __LP64__
                const char *libguiPath = "/system/lib64/libgui.so";
                auto libgui = symbolMethod.Open(libguiPath, RTLD_LAZY);
                auto libutils = symbolMethod.Open("/system/lib64/libutils.so", RTLD_LAZY);
#else
                const char *libguiPath = "/system/lib/libgui.so";
                auto libgui = symbolMethod.Open(libguiPath, RTLD_LAZY);
                auto libutils = symbolMethod.Open("/system/lib/libutils.so", RTLD_LAZY);
#endif
                //libutils
                ResolveMethod(RefBase, IncStrong, libutils, "_ZNK7android7RefBase9incStrongEPKv");
                ResolveMethod(RefBase, DecStrong, libutils, "_ZNK7android7RefBase9decStrongEPKv");

                ResolveMethod(String8, Constructor, libutils, "_ZN7android7String8C2EPKc");
                ResolveMethod(String8, Destructor, libutils, "_ZN7android7String8D2Ev");
                
                //libgui
                if (10 <= systemVersion && 13 >= systemVersion) {
                    ResolveMethod(LayerMetadata, Constructor, libgui, "_ZN7android13LayerMetadataC2Ev");
                    ResolveMethod(LayerMetadata, setInt32, libgui, "_ZN7android13LayerMetadata8setInt32Eji");
                } else if (14 <= systemVersion) {
                    ResolveMethod(LayerMetadata, Constructor, libgui, "_ZN7android3gui13LayerMetadataC2Ev");
                }

                ResolveMethod(SurfaceComposerClient, Constructor, libgui, "_ZN7android21SurfaceComposerClientC2Ev");

                // Select the correct CreateSurface API based on Android version
                if (5 <= systemVersion && 7 >= systemVersion) {
                    // Android 5-7
                    ResolveMethod(SurfaceComposerClient, CreateSurface, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8Ejjij");
                } else if (8 == systemVersion) {
                    // Android 8
                    ResolveMethod(SurfaceComposerClient, CreateSurface_and8, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlEjj");
                } else if (9 == systemVersion) {
                    // Android 9
                    ResolveMethod(SurfaceComposerClient, CreateSurface_and9, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlEii");
                } else if (10 == systemVersion) {
                    // Android 10
                    ResolveMethod(SurfaceComposerClient, CreateSurface, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlENS_13LayerMetadataE");
                } else if (11 == systemVersion) {
                    // Android 11
                    ResolveMethod(SurfaceComposerClient, CreateSurface, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlENS_13LayerMetadataEPj");
                } else if (12 <= systemVersion && 13 >= systemVersion) {
                    // Android 12-13
                    ResolveMethod(SurfaceComposerClient, CreateSurface, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEENS_13LayerMetadataEPj");
                } else if (14 <= systemVersion) {
                    // Android 14+
                    ResolveMethod(SurfaceComposerClient, CreateSurface, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj");
                }
                
                // MirrorSurface method - Android 11+
                //
                // Two ABIs exist across versions, both non-static members:
                //   Android 11-13: mirrorSurface(SurfaceControl*)
                //                  ... mangled tail "EPNS_14SurfaceControlE"
                //   Android 14-16: mirrorSurface(SurfaceControl*, SurfaceControl* parent)
                //                  ... mangled tail "EPNS_14SurfaceControlES2_"
                // We resolve whichever the ROM exports and call it with the
                // matching number of arguments (parent = nullptr). Calling the
                // 2-arg overload with the 1-arg prototype segfaults the moment a
                // screen recorder's VirtualDisplay appears, so the prototype must
                // match exactly.
                //
                // AImGui 加固：mirrorSurface 的调用方（ProcessMirrorDisplay）已
                // 被禁用，但符号解析保留（它只读 libgui 的 ELF，不调用）。
                if (11 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient, MirrorSurface, libgui, "_ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControlE");
                    SurfaceComposerClient__MirrorSurface2 = reinterpret_cast<decltype(SurfaceComposerClient__MirrorSurface2)>(
                        symbolMethod.Find(libgui, "_ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControlES2_"));

                    // ROM-specific mangling fallback: scan libgui's dynsym for
                    // the SurfaceComposerClient::mirrorSurface method token,
                    // selecting by ABI (parameter mangling) so each pointer only
                    // ever binds a function we know how to call.
                    if (nullptr == SurfaceComposerClient__MirrorSurface) {
                        std::string n = FindDynSymContaining(libguiPath,
                            "21SurfaceComposerClient13mirrorSurface", "EPNS_14SurfaceControlE");
                        if (!n.empty())
                            SurfaceComposerClient__MirrorSurface = reinterpret_cast<decltype(SurfaceComposerClient__MirrorSurface)>(symbolMethod.Find(libgui, n.c_str()));
                    }
                    if (nullptr == SurfaceComposerClient__MirrorSurface2) {
                        std::string n = FindDynSymContaining(libguiPath,
                            "21SurfaceComposerClient13mirrorSurface", "EPNS_14SurfaceControlES2_");
                        if (!n.empty())
                            SurfaceComposerClient__MirrorSurface2 = reinterpret_cast<decltype(SurfaceComposerClient__MirrorSurface2)>(symbolMethod.Find(libgui, n.c_str()));
                    }

                    if (nullptr == SurfaceComposerClient__MirrorSurface &&
                        nullptr == SurfaceComposerClient__MirrorSurface2) {
                        SURFACE_LOG_WARN("No callable mirrorSurface on this ROM; recordings won't capture overlay (no crash)");
                    } else {
                        SURFACE_LOG_INFO("mirrorSurface resolved (1-arg=%p 2-arg=%p)",
                                         (void *)SurfaceComposerClient__MirrorSurface,
                                         (void *)SurfaceComposerClient__MirrorSurface2);
                    }
                }
                
                // Display related methods - version specific selection
                if (5 <= systemVersion && 9 >= systemVersion) {
                    // Android 5-9 uses GetBuiltInDisplay
                    ResolveMethod(SurfaceComposerClient, GetBuiltInDisplay, libgui, "_ZN7android21SurfaceComposerClient17getBuiltInDisplayEi");
                }
                if (10 <= systemVersion && 13 >= systemVersion) {
                    // Android 10-13 uses GetInternalDisplayToken
                    ResolveMethod(SurfaceComposerClient, GetInternalDisplayToken, libgui, "_ZN7android21SurfaceComposerClient23getInternalDisplayTokenEv");
                }
                if (10 <= systemVersion) {
                    // Android 10+ uses GetPhysicalDisplayIds
                    ResolveMethod(SurfaceComposerClient, GetPhysicalDisplayIds, libgui, "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv");
                }
                if (12 <= systemVersion) {
                    // Android 12+ uses GetPhysicalDisplayToken
                    ResolveMethod(SurfaceComposerClient, GetPhysicalDisplayToken, libgui, "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE");
                }
                
                // Display state and info retrieval methods
                if (5 <= systemVersion && 11 >= systemVersion) {
                    // Android 5-11 uses GetDisplayInfo
                    ResolveMethod(SurfaceComposerClient, GetDisplayInfo, libgui, "_ZN7android21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_11DisplayInfoE");
                }
                if (11 <= systemVersion) {
                    // Android 11+ uses GetDisplayState
                    ResolveMethod(SurfaceComposerClient, GetDisplayState, libgui, "_ZN7android21SurfaceComposerClient15getDisplayStateERKNS_2spINS_7IBinderEEEPNS_2ui12DisplayStateE");
                }

                // GlobalTransaction methods - Android 5-8 only
                if (5 <= systemVersion && 8 >= systemVersion) {
                    ResolveMethod(SurfaceComposerClient, OpenGlobalTransaction, libgui, "_ZN7android21SurfaceComposerClient21openGlobalTransactionEv");
                    ResolveMethod(SurfaceComposerClient, CloseGlobalTransaction, libgui, "_ZN7android21SurfaceComposerClient22closeGlobalTransactionEb");
                }

                // Transaction related methods - Android 9+
                if (12 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient__Transaction, Constructor, libgui, "_ZN7android21SurfaceComposerClient11TransactionC2Ev");
                }
                if (9 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient__Transaction, SetLayer, libgui, "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi");
                    ResolveMethod(SurfaceComposerClient__Transaction, Show, libgui, "_ZN7android21SurfaceComposerClient11Transaction4showERKNS_2spINS_14SurfaceControlEEE");
                    ResolveMethod(SurfaceComposerClient__Transaction, Hide, libgui, "_ZN7android21SurfaceComposerClient11Transaction4hideERKNS_2spINS_14SurfaceControlEEE");
                }
                if (12 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient__Transaction, SetTrustedOverlay, libgui, "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb");
                    ResolveMethod(SurfaceComposerClient__Transaction, Reparent, libgui, "_ZN7android21SurfaceComposerClient11Transaction8reparentERKNS_2spINS_14SurfaceControlEEES6_");
                }
                if (9 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient__Transaction, SetMatrix, libgui, "_ZN7android21SurfaceComposerClient11Transaction9setMatrixERKNS_2spINS_14SurfaceControlEEEffff");
                }
                if (5 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient__Transaction, SetPosition, libgui, "_ZN7android21SurfaceComposerClient11Transaction11setPositionERKNS_2spINS_14SurfaceControlEEEff");
                }
                if (13 <= systemVersion) {
                    ResolveMethod(SurfaceComposerClient__Transaction, SetLayerStack, libgui, "_ZN7android21SurfaceComposerClient11Transaction13setLayerStackERKNS_2spINS_14SurfaceControlEEENS_2ui10LayerStackE");
                }
                
                // Transaction Apply method - version specific selection
                if (9 <= systemVersion && 12 >= systemVersion) {
                    // Android 9-12 uses two-parameter version
                    ResolveMethod(SurfaceComposerClient__Transaction, Apply, libgui, "_ZN7android21SurfaceComposerClient11Transaction5applyEb");
                }
                if (13 <= systemVersion) {
                    // Android 13+ uses three-parameter version
                    ResolveMethod(SurfaceComposerClient__Transaction, Apply, libgui, "_ZN7android21SurfaceComposerClient11Transaction5applyEbb");
                }

                // SurfaceControl related methods
                if (5 <= systemVersion) {
                    ResolveMethod(SurfaceControl, Validate, libgui, "_ZNK7android14SurfaceControl8validateEv");
                }
                
                // SurfaceControl GetSurface method - version specific selection
                if (5 <= systemVersion && 11 >= systemVersion) {
                    // Android 5-11 uses const version
                    ResolveMethod(SurfaceControl, GetSurface, libgui, "_ZNK7android14SurfaceControl10getSurfaceEv");
                }
                if (12 <= systemVersion) {
                    // Android 12+ uses non-const version
                    ResolveMethod(SurfaceControl, GetSurface, libgui, "_ZN7android14SurfaceControl10getSurfaceEv");
                }
                
                // DisConnect method - version specific selection
                if (5 <= systemVersion && 6 >= systemVersion) {
                    // Android 5-6 uses Surface::disconnect
                    ResolveMethod(Surface, DisConnect, libgui, "_ZN7android7Surface10disconnectEi");
                }
                if (7 <= systemVersion) {
                    // Android 7+ uses SurfaceControl::disconnect
                    ResolveMethod(SurfaceControl, DisConnect, libgui, "_ZN7android14SurfaceControl10disconnectEv");
                }
                
                // SetLayer method - version specific selection
                if (5 == systemVersion || 8 == systemVersion) {
                    // Android 5 and 8+ use int version
                    ResolveMethod(SurfaceControl, SetLayer, libgui, "_ZN7android14SurfaceControl8setLayerEi");
                }
                if (6 <= systemVersion && 7 >= systemVersion) {
                    // Android 6-7 use uint version
                    ResolveMethod(SurfaceControl, SetLayer, libgui, "_ZN7android14SurfaceControl8setLayerEj");
                }

                symbolMethod.Close(libutils);
                symbolMethod.Close(libgui);
            }

            static const Functionals &GetInstance(const SymbolMethod &symbolMethod = {.Open = dlopen, .Find = dlsym, .Close = dlclose}) {
                static Functionals functionals(symbolMethod);
                return functionals;
            }
        };

        struct String8
        {
            char data[1024];

            String8(const char *const string)
            {
                Functionals::GetInstance().String8__Constructor(data, string);
            }

            ~String8()
            {
                Functionals::GetInstance().String8__Destructor(data);
            }

            operator void *()
            {
                return reinterpret_cast<void *>(data);
            }
        };

        struct LayerMetadata {
            char data[1024];

            LayerMetadata() {
                if (9 < Functionals::GetInstance().systemVersion) {
                    Functionals::GetInstance().LayerMetadata__Constructor(data);
                }
            }
            
            void setInt32(uint32_t key, int32_t value) {
                Functionals::GetInstance().LayerMetadata__setInt32(data, key, value);            
            }
            
            operator void *() {
                if (9 < Functionals::GetInstance().systemVersion)
                    return reinterpret_cast<void *>(data);
                else
                    return nullptr;
            }
        };

        struct Surface {
        };

        struct SurfaceControl {
            void *data;

            SurfaceControl() : data(nullptr) {}
            SurfaceControl(void *data) : data(data) {}

            int32_t Validate() {
                if (nullptr == data)
                    return 0;

                return Functionals::GetInstance().SurfaceControl__Validate(data);
            }

            Surface *GetSurface() {
                if (nullptr == data)
                    return nullptr;

                auto result = Functionals::GetInstance().SurfaceControl__GetSurface(data);

                return reinterpret_cast<Surface *>(reinterpret_cast<size_t>(result.pointer) + sizeof(::max_align_t) / 2);
            }

            void DisConnect() {
                if (nullptr == data)
                    return;

                Functionals::GetInstance().SurfaceControl__DisConnect(data);
            }

            void SetLayer(int32_t z) {
                if (nullptr == data)
                    return;

                Functionals::GetInstance().SurfaceControl__SetLayer(data, z);
            }

            void DestroySurface(Surface *surface) {
                if (nullptr == data || nullptr == surface)
                    return;

                Functionals::GetInstance().RefBase__DecStrong(reinterpret_cast<Surface *>(reinterpret_cast<size_t>(surface) - sizeof(::max_align_t) / 2), this);
                DisConnect();
                Functionals::GetInstance().RefBase__DecStrong(data, this);
            }
        };

        struct SurfaceComposerClientTransaction {
            char data[1024];

            SurfaceComposerClientTransaction() {
                Functionals::GetInstance().SurfaceComposerClient__Transaction__Constructor(data);
            }

            void *SetLayer(StrongPointer<void> &surfaceControl, int32_t z) {
                return Functionals::GetInstance().SurfaceComposerClient__Transaction__SetLayer(data, surfaceControl, z);
            }

            void *SetTrustedOverlay(StrongPointer<void> &surfaceControl, bool isTrustedOverlay) {
                return Functionals::GetInstance().SurfaceComposerClient__Transaction__SetTrustedOverlay(data, surfaceControl, isTrustedOverlay);
            }

            void *SetLayerStack(StrongPointer<void> &surfaceControl, uint32_t layerStack) {
                return Functionals::GetInstance().SurfaceComposerClient__Transaction__SetLayerStack(data, surfaceControl, layerStack);
            }

            void Show(StrongPointer<void> &surfaceControl) {
                Functionals::GetInstance().SurfaceComposerClient__Transaction__Show(data, surfaceControl);
            }

            void Hide(StrongPointer<void> &surfaceControl) {
                Functionals::GetInstance().SurfaceComposerClient__Transaction__Hide(data, surfaceControl);
            }

            void Reparent(StrongPointer<void> &surfaceControl, StrongPointer<void> &newParentHandle) {
                Functionals::GetInstance().SurfaceComposerClient__Transaction__Reparent(data, surfaceControl, newParentHandle);
            }

            void *SetMatrix(StrongPointer<void> &surfaceControl, float dsdx, float dtdx, float dtdy, float dsdy) {
                return Functionals::GetInstance().SurfaceComposerClient__Transaction__SetMatrix(data, surfaceControl, dsdx, dtdx, dtdy, dsdy);
            }

            void SetPosition(StrongPointer<void> &surfaceControl, float x, float y) {
                Functionals::GetInstance().SurfaceComposerClient__Transaction__SetPosition(data, surfaceControl, x, y);
            }

            int32_t Apply(bool synchronous, bool oneWay) {
                if (12 >= Functionals::GetInstance().systemVersion)
                    return reinterpret_cast<int32_t (*)(void *, bool)>(Functionals::GetInstance().SurfaceComposerClient__Transaction__Apply)(data, synchronous);
                else
                    return Functionals::GetInstance().SurfaceComposerClient__Transaction__Apply(data, synchronous, oneWay);
            }
        };

        struct SurfaceComposerClient {
            char data[1024];

            SurfaceComposerClient() {
                Functionals::GetInstance().SurfaceComposerClient__Constructor(data);
                Functionals::GetInstance().RefBase__IncStrong(data, this);
            }

            // AImGui 加固：
            //   - skipScrenshot 参数被忽略。无论调用方传什么，覆盖层都不设
            //     eSkipScreenshot 位，因此始终会出现在录屏 / 投屏里。
            //   - 函数签名、参数类型与默认值保持不变。
            SurfaceControl CreateSurface(const char *name, int32_t width, int32_t height, uint32_t windowFlags = 0, bool /*skipScrenshot 忽略*/ = false) {
                static void *parentHandle = nullptr;
                parentHandle = nullptr;
                
                String8 windowName(name);
                int32_t pixelFormat = 1; // RGBA_8888
                LayerMetadata layerMetadata{};
                auto systemVersion = Functionals::GetInstance().systemVersion;

                StrongPointer<void> result{};
                
                switch (systemVersion) {
                case 5:
                case 6:
                case 7:
                {
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, layerMetadata, nullptr);
                    break;
                }
                case 8:
                {
                    uint32_t windowType = 0;
                    uint32_t ownerUid = 0;
                    // skipScrenshot 被忽略（见上方说明）
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface_and8(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, windowType, ownerUid);
                    break;
                }
                case 9:
                {
                    int32_t windowType = -1;
                    int32_t ownerUid = -1;
                    // skipScrenshot 被忽略（见上方说明）
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface_and9(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, windowType, ownerUid);
                    break;
                }
                case 10:
                {
                    // skipScrenshot 被忽略（见上方说明）
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, layerMetadata, nullptr);
                    break;
                }
                case 11:
                {
                    // skipScrenshot 被忽略（见上方说明）
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface(data, windowName, width, height, pixelFormat, windowFlags, parentHandle, layerMetadata, nullptr);
                    break;
                }
                case 12:
                case 13:
                {
                    // skipScrenshot 被忽略（见上方说明）
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface(data, windowName, width, height, pixelFormat, windowFlags, &parentHandle, layerMetadata, nullptr);
                    break;
                }
                default: // Android 14+
                {
                    // skipScrenshot 被忽略（见上方说明）
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface(data, windowName, width, height, pixelFormat, windowFlags, &parentHandle, layerMetadata, nullptr);
                    break;
                }
                }

                // Check if Surface creation was successful
                if (nullptr == result.get()) {
                    SURFACE_LOG_ERROR("Failed to create surface: %s", name);
                    return {};
                }

                // Apply permission fixes
                if (12 <= systemVersion) {
                    // Android 12+: Use Transaction mechanism to set trusted overlay and highest layer.
                    // TrustedOverlay does double duty: it excludes the
                    // layer from MediaProjection captures *and* from the
                    // input dispatch tree, which is what lets touches
                    // pass through to apps below our full-screen surface.
                    // We need the input pass-through unconditionally —
                    // so trusted overlay stays on.
                    static SurfaceComposerClientTransaction transaction;
                    transaction.SetTrustedOverlay(result, true);
                    transaction.SetLayer(result, INT_MAX);
                    auto applyResult = transaction.Apply(false, true);
                } else if (8 >= systemVersion) {
                    // Android 8 and below: Use global transaction to set layer
                    OpenGlobalTransaction();
                    SurfaceControl{result.get()}.SetLayer(INT_MAX);
                    CloseGlobalTransaction(false);
                }

                return {result.get()};
            }

            bool GetDisplayInfo(ui::DisplayState *displayInfo) {
                static StrongPointer<void> defaultDisplay;

                if (nullptr == defaultDisplay.get()) {
                    if (9 >= Functionals::GetInstance().systemVersion) { // Android 9 and below
                        defaultDisplay = Functionals::GetInstance().SurfaceComposerClient__GetBuiltInDisplay(ui::DisplayType::DisplayIdMain);
                    } else {
                        if (14 > Functionals::GetInstance().systemVersion) { // Android 10-13
                            defaultDisplay = Functionals::GetInstance().SurfaceComposerClient__GetInternalDisplayToken();
                        } else { // Android 14 and above
                            auto displayIds = Functionals::GetInstance().SurfaceComposerClient__GetPhysicalDisplayIds();
                            if (displayIds.empty())
                                return false;

                            defaultDisplay = Functionals::GetInstance().SurfaceComposerClient__GetPhysicalDisplayToken(displayIds[0]);
                        }
                    }
                }

                if (nullptr == defaultDisplay.get())
                    return false;

                if (11 <= Functionals::GetInstance().systemVersion) { // Android 11 and above
                    return 0 == Functionals::GetInstance().SurfaceComposerClient__GetDisplayState(defaultDisplay, displayInfo);
                } else { // Android 10 and below
                    ui::DisplayInfo realDisplayInfo{};
                    if (0 != Functionals::GetInstance().SurfaceComposerClient__GetDisplayInfo(defaultDisplay, &realDisplayInfo))
                        return false;

                    displayInfo->layerStackSpaceRect.width = realDisplayInfo.w;
                    displayInfo->layerStackSpaceRect.height = realDisplayInfo.h;
                    displayInfo->orientation = static_cast<ui::Rotation>(realDisplayInfo.orientation);

                    return true;
                }
            }

            void OpenGlobalTransaction() {
                Functionals::GetInstance().SurfaceComposerClient__OpenGlobalTransaction();
            }

            void CloseGlobalTransaction(bool synchronous) {
                Functionals::GetInstance().SurfaceComposerClient__CloseGlobalTransaction(synchronous);
            }
        };
    }

    class ANativeWindowCreator {
    public:
        struct DisplayInfo {
            int32_t orientation;
            int32_t width;
            int32_t height;
        };

    public:
        static detail::SurfaceComposerClient &GetComposerInstance() {
            static detail::SurfaceComposerClient surfaceComposerClient;
            return surfaceComposerClient;
        }

        static DisplayInfo GetDisplayInfo() {
            auto &surfaceComposerClient = GetComposerInstance();
            detail::ui::DisplayState displayInfo{};

            if (!surfaceComposerClient.GetDisplayInfo(&displayInfo))
                return {};
            
            DisplayInfo local_displayInfo{0};   
            int32_t local_orientation = static_cast<int32_t>(displayInfo.orientation);  
            int32_t local_abs_x = (displayInfo.layerStackSpaceRect.width > displayInfo.layerStackSpaceRect.height ? displayInfo.layerStackSpaceRect.width : displayInfo.layerStackSpaceRect.height);
            int32_t local_abs_y = (displayInfo.layerStackSpaceRect.width < displayInfo.layerStackSpaceRect.height ? displayInfo.layerStackSpaceRect.width : displayInfo.layerStackSpaceRect.height);          
            if (local_orientation == 1 || local_orientation == 3) {
                local_displayInfo.width = local_abs_x;
                local_displayInfo.height = local_abs_y;
            } else {
                local_displayInfo.width = local_abs_y;
                local_displayInfo.height = local_abs_x;
            }
            local_displayInfo.orientation = local_orientation;
            return local_displayInfo;
        }

        // AImGui 加固：skipScrenshot_ 参数被忽略（见 CreateSurface 顶部说明）。
        // 函数签名、参数默认值保持不变。
        static ANativeWindow *Create(const char *name, int32_t width = -1, int32_t height = -1, bool /*skipScrenshot_ 忽略*/ = false) {
            auto &surfaceComposerClient = GetComposerInstance();
            
            // Auto-retrieve display dimensions
            while (-1 == width || -1 == height) {
                detail::ui::DisplayState displayInfo{};
                if (!surfaceComposerClient.GetDisplayInfo(&displayInfo))
                    break;

                width = displayInfo.layerStackSpaceRect.width;
                height = displayInfo.layerStackSpaceRect.height;

                break;
            }

            // Create Surface (skipScrenshot_ 不再透传；CreateSurface 内部已忽略)
            auto surfaceControl = surfaceComposerClient.CreateSurface(name, width, height, 0, false);
            if (!surfaceControl.data) {
                SURFACE_LOG_ERROR("Failed to create surface control for: %s", name);
                return nullptr;
            }

            auto nativeWindow = reinterpret_cast<ANativeWindow *>(surfaceControl.GetSurface());
            if (!nativeWindow) {
                SURFACE_LOG_ERROR("Failed to get native window from surface control");
                return nullptr;
            }

            // Cache Surface controller
            m_cachedSurfaceControl.emplace(nativeWindow, std::move(surfaceControl));
            
            SURFACE_LOG_INFO("ANativeWindow created successfully: %p", nativeWindow);
            return nativeWindow;
        }

        static void Destroy(ANativeWindow *nativeWindow) {
            auto it = m_cachedSurfaceControl.find(nativeWindow);
            if (it == m_cachedSurfaceControl.end())
                return;

            SURFACE_LOG_INFO("Destroying ANativeWindow: %p", nativeWindow);
            
            // Destroy main Surface
            m_cachedSurfaceControl[nativeWindow].DestroySurface(reinterpret_cast<detail::Surface *>(nativeWindow));
            m_cachedSurfaceControl.erase(nativeWindow);
        }

        // AImGui 加固：原实现每秒 popen("dumpsys display") 并创建镜像
        // Surface；现改为空操作。方法签名、调用点保持不变。
        static void ProcessMirrorDisplay() {
            // no-op
        }

        // AImGui 加固：原实现会立刻触发一次 ProcessMirrorDisplay。
        // 现改为空操作。方法签名、默认参数保持不变。
        static void EnableAutoMirrorDisplay(bool enable = true) {
            (void)enable;
            // no-op
        }

        // Get current cached Surface count
        static size_t GetCachedSurfaceCount() {
            return m_cachedSurfaceControl.size();
        }

        // AImGui 加固：镜像路径已停用，以下镜像相关方法统一为空操作
        // 或返回"无镜像"，签名与返回值语义保持不变。
        static void ClearAllMirrorSurfaces() {
            // no-op
        }

        static void ClearMirrorSurfaceForLayerStack(const std::string& layerStack) {
            (void)layerStack;
            // no-op
        }

        static size_t GetMirrorSurfaceCount() {
            return 0;
        }

        static bool HasMirrorForLayerStack(const std::string& layerStack) {
            (void)layerStack;
            return false;
        }

        // Complete cleanup when application exits
        static void Cleanup() {
            SURFACE_LOG_INFO("Performing complete cleanup...");
            
            // Clean up all main surfaces
            for (auto& [nativeWindow, surfaceControl] : m_cachedSurfaceControl) {
                SURFACE_LOG_DEBUG("Cleaning up surface: %p", nativeWindow);
                surfaceControl.DestroySurface(reinterpret_cast<detail::Surface *>(nativeWindow));
            }
            m_cachedSurfaceControl.clear();
            
            SURFACE_LOG_INFO("Complete cleanup finished");
        }

    private:
        inline static std::unordered_map<ANativeWindow *, detail::SurfaceControl> m_cachedSurfaceControl;
    };
}

#undef ResolveMethod

#endif // !A_NATIVE_WINDOW_CREATOR_H