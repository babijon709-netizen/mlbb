#include "overlay/TouchReader.h"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

// Bionic ships the kernel input headers in its sysroot; a PC build (`make
// overlay`) has the full linux/input.h. Only when neither is around do the
// hand-written copies below kick in - they must match the kernel ABI exactly.
#if __has_include(<linux/input.h>) && !defined(MLBB_FORCE_INPUT_FALLBACK)
#include <linux/input.h>
#else
#define MLBB_OWN_INPUT_H 1
struct input_event {
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t  value;
};
struct input_absinfo {
    int32_t value, minimum, maximum, fuzz, flat, resolution;
};
enum {
    EV_SYN = 0x00, EV_KEY = 0x01, EV_ABS = 0x03,
    SYN_REPORT = 0,
    ABS_X = 0x00, ABS_Y = 0x01, ABS_MT_SLOT = 0x2f,
    ABS_MT_POSITION_X = 0x35, ABS_MT_POSITION_Y = 0x36, ABS_MT_TRACKING_ID = 0x39,
    BTN_TOUCH = 0x14a,
    KEY_VOLUMEDOWN = 114, KEY_VOLUMEUP = 115,
    INPUT_PROP_DIRECT = 0x01
};
#define EVIOCGNAME(len)     _IOC(_IOC_READ, 'E', 0x06, len)
#define EVIOCGPROP(len)     _IOC(_IOC_READ, 'E', 0x09, len)
#define EVIOCGBIT(ev, len)  _IOC(_IOC_READ, 'E', 0x20 + (ev), len)
#define EVIOCGABS(abs)      _IOC(_IOC_READ, 'E', 0x40 + (abs), sizeof(struct input_absinfo))
#define EVIOCGRAB           _IOW('E', 0x90, int)
#endif

namespace mlbb {
namespace overlay {

namespace {

constexpr int kMaxSlots = 10;

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Panel coordinates -> unit square of the surface. swap/mirror/rot is either
// the value from --touch-* or what Detect() guessed.
void ApplyMap(const TouchMap& map, float nx, float ny, float* outX, float* outY)
{
    float x = nx, y = ny;

    if (map.swapXY) std::swap(x, y);
    if (map.mirrorX) x = 1.0f - x;
    if (map.mirrorY) y = 1.0f - y;

    switch (((map.rot % 360) + 360) % 360) {
        case 90:  { const float t = x; x = y;         y = 1.0f - t; break; }
        case 180: { x = 1.0f - x; y = 1.0f - y;                     break; }
        case 270: { const float t = x; x = 1.0f - y;  y = t;        break; }
        default:  break;
    }

    *outX = Clamp01(x);
    *outY = Clamp01(y);
}

bool AbsSupported(int fd, int code)
{
    unsigned char bits[8] = { 0 };
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) < 0) return false;
    const int byte = code / 8, bit = code % 8;
    if (byte >= (int)sizeof(bits)) return false;
    return (bits[byte] & (1u << bit)) != 0;
}

bool KeySupported(int fd, int code)
{
    unsigned char bits[16] = { 0 };
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) return false;
    const int byte = code / 8, bit = code % 8;
    if (byte >= (int)sizeof(bits)) return false;
    return (bits[byte] & (1u << bit)) != 0;
}

}  // namespace

TouchInput::~TouchInput() { Close(); }

bool TouchInput::Init(int screenWidth, int screenHeight, const TouchMap& hint, unsigned setMask,
                      bool grabDevices, bool verbose)
{
    Close();

    m_screenW = screenWidth > 0 ? screenWidth : 1;
    m_screenH = screenHeight > 0 ? screenHeight : 1;

    DIR* dir = opendir("/dev/input");
    if (!dir) return false;
    closedir(dir);

    for (int index = 0; index < 32; ++index) {
        char path[64];
        std::snprintf(path, sizeof(path), "/dev/input/event%d", index);

        const int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        DeviceState device;
        device.info.fd = fd;

        input_absinfo absX{};
        input_absinfo absY{};
        const bool mt = AbsSupported(fd, ABS_MT_POSITION_X) && AbsSupported(fd, ABS_MT_POSITION_Y);
        const bool st = !mt && AbsSupported(fd, ABS_X) && AbsSupported(fd, ABS_Y) && KeySupported(fd, BTN_TOUCH);
        const bool isTouch = (mt || st);

        if (isTouch) {
            const int codeX = mt ? ABS_MT_POSITION_X : ABS_X;
            const int codeY = mt ? ABS_MT_POSITION_Y : ABS_Y;
            if (ioctl(fd, EVIOCGABS(codeX), &absX) != 0 || ioctl(fd, EVIOCGABS(codeY), &absY) != 0) {
                close(fd);
                continue;
            }
            device.info.multitouch = mt;
            device.info.minX = absX.minimum; device.info.maxX = absX.maximum;
            device.info.minY = absY.minimum; device.info.maxY = absY.maximum;
        }

        device.info.hasKeys = KeySupported(fd, KEY_VOLUMEUP) && KeySupported(fd, KEY_VOLUMEDOWN);

        if (!isTouch && !device.info.hasKeys) { close(fd); continue; }

        char name[sizeof(device.info.name)] = { 0 };
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) > 0)
            std::snprintf(device.info.name, sizeof(device.info.name), "%s", name);

        if (grabDevices && isTouch) {
            // Grabbing hands the device exclusively to us: the game stops
            // seeing touches while the menu is open. Off by default.
            const int one = 1;
            device.info.grabbed = ioctl(fd, EVIOCGRAB, &one) == 0;
        }

        if (verbose && isTouch) {
            std::printf("overlay: touch device %s [%s] x %d..%d y %d..%d%s\n", path, device.info.name,
                        device.info.minX, device.info.maxX, device.info.minY, device.info.maxY,
                        device.info.multitouch ? " (multitouch)" : " (single touch)");
        }

        m_devices.push_back(device);
    }

    // Fill in everything the caller did not pin down explicitly.
    TouchMap detected;
    for (const DeviceState& device : m_devices) {
        if (device.info.maxX > device.info.minX && device.info.maxY > device.info.minY) {
            detected = Detect(m_screenW, m_screenH, device.info);
            break;
        }
    }

    m_map.swapXY  = (setMask & MapSetSwap)    ? hint.swapXY  : detected.swapXY;
    m_map.mirrorX = (setMask & MapSetMirrorX) ? hint.mirrorX : detected.mirrorX;
    m_map.mirrorY = (setMask & MapSetMirrorY) ? hint.mirrorY : detected.mirrorY;
    m_map.rot     = (setMask & MapSetRot)     ? hint.rot     : detected.rot;

    if (verbose && !m_devices.empty()) {
        std::printf("overlay: touch mapping %s%s%s rot %d (detected: %s%s%s rot %d)\n",
                    m_map.swapXY ? "swap-xy " : "", m_map.mirrorX ? "mirror-x " : "",
                    m_map.mirrorY ? "mirror-y " : "", m_map.rot,
                    detected.swapXY ? "swap-xy " : "", detected.mirrorX ? "mirror-x " : "",
                    detected.mirrorY ? "mirror-y " : "", detected.rot);
    }

    return !m_devices.empty();
}

void TouchInput::Close()
{
    for (DeviceState& device : m_devices) {
        if (device.info.fd >= 0) {
            if (device.info.grabbed) {
                const int zero = 0;
                ioctl(device.info.fd, EVIOCGRAB, &zero);
            }
            close(device.info.fd);
        }
    }
    m_devices.clear();
    m_activeDevice = m_activeSlot = -1;
    m_activeOrder = -1;
    m_state = TouchState{};
}

void TouchInput::PrintDevices() const
{
    if (m_devices.empty()) {
        std::printf("overlay: no /dev/input device could be opened (root needed, or the ROM hides them)\n");
        return;
    }

    for (size_t i = 0; i < m_devices.size(); ++i) {
        const TouchDevice& d = m_devices[i].info;
        std::printf("overlay: %s[%d] \"%s\" touch=%s keys=%s grab=%s x %d..%d y %d..%d\n",
                    "/dev/input/event", (int)i, d.name,
                    (d.maxX > d.minX) ? (d.multitouch ? "mt" : "single") : "no",
                    d.hasKeys ? "yes" : "no", d.grabbed ? "yes" : "no",
                    d.minX, d.maxX, d.minY, d.maxY);
    }
}

bool TouchInput::Poll(int timeoutMs)
{
    if (m_devices.empty()) return false;

    m_state.changed = false;

    std::vector<pollfd> fds;
    fds.reserve(m_devices.size());
    for (const DeviceState& device : m_devices) {
        pollfd entry{};
        entry.fd = device.info.fd;
        entry.events = POLLIN;
        fds.push_back(entry);
    }

    poll(fds.data(), (nfds_t)fds.size(), timeoutMs);

    bool touched = false;
    for (size_t i = 0; i < fds.size(); ++i) {
        if (!(fds[i].revents & (POLLIN | POLLERR))) continue;

        input_event events[64];
        for (;;) {
            const ssize_t got = read(m_devices[i].info.fd, events, sizeof(events));
            if (got <= 0) break;
            if (got % (ssize_t)sizeof(input_event) != 0) break;

            const size_t count = (size_t)got / sizeof(input_event);
            DeviceState& device = m_devices[i];

            for (size_t e = 0; e < count; ++e) {
                const input_event& ev = events[e];

                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_MT_SLOT) {
                        int slot = ev.value;
                        if (slot < 0) slot = 0;
                        if (slot >= kMaxSlots) slot = kMaxSlots - 1;
                        m_activeSlot = slot;
                        continue;
                    }
                    const int slot = (device.info.multitouch && m_activeSlot >= 0) ? m_activeSlot : 0;
                    Slot& target = device.slots[slot];

                    if (ev.code == ABS_MT_TRACKING_ID) {
                        if (ev.value == -1) target.down = false;
                        else { target.down = true; target.order = ++device.orderCounter; }
                    } else if (ev.code == ABS_MT_POSITION_X || (!device.info.multitouch && ev.code == ABS_X)) {
                        target.rawX = ev.value;
                    } else if (ev.code == ABS_MT_POSITION_Y || (!device.info.multitouch && ev.code == ABS_Y)) {
                        target.rawY = ev.value;
                    }
                    continue;
                }

                if (ev.type == EV_KEY) {
                    if (ev.code == BTN_TOUCH && !device.info.multitouch) {
                        device.slots[0].down = (ev.value != 0);
                        if (ev.value) device.slots[0].order = ++device.orderCounter;
                    } else if (ev.code == KEY_VOLUMEUP) {
                        m_volUp = (ev.value != 0);
                    } else if (ev.code == KEY_VOLUMEDOWN) {
                        m_volDown = (ev.value != 0);
                    }
                    m_exitChord = m_volUp && m_volDown;
                    continue;
                }

                if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    CommitDevice((int)i);
                    touched = true;
                }
            }
        }
    }

    return m_state.changed || touched;
}

void TouchInput::CommitDevice(int deviceIndex)
{
    DeviceState& device = m_devices[deviceIndex];

    int bestSlot = -1, bestOrder = -1;
    for (int slot = 0; slot < kMaxSlots; ++slot) {
        if (device.slots[slot].down && device.slots[slot].order > bestOrder) {
            bestOrder = device.slots[slot].order;
            bestSlot = slot;
        }
    }

    if (bestSlot < 0) {
        // All fingers up: remember the position, report the release.
        if (m_state.down) { m_state.down = false; m_state.changed = true; }
        if (m_activeDevice == deviceIndex) { m_activeSlot = -1; m_activeOrder = -1; }
        return;
    }

    const Slot& slot = device.slots[bestSlot];
    const int rangeX = device.info.maxX - device.info.minX;
    const int rangeY = device.info.maxY - device.info.minY;
    if (rangeX <= 0 || rangeY <= 0) return;

    const float nx = Clamp01((float)(slot.rawX - device.info.minX) / (float)rangeX);
    const float ny = Clamp01((float)(slot.rawY - device.info.minY) / (float)rangeY);

    float mx = nx, my = ny;
    ApplyMap(m_map, nx, ny, &mx, &my);

    const float sx = mx * (float)m_screenW;
    const float sy = my * (float)m_screenH;

    if (sx != m_state.x || sy != m_state.y || !m_state.down) m_state.changed = true;

    m_state.x = sx;
    m_state.y = sy;
    m_state.down = true;
    m_normX = nx;
    m_normY = ny;

    m_activeDevice = deviceIndex;
    m_activeSlot = bestSlot;
    m_activeOrder = bestOrder;
}

TouchMap TouchInput::Detect(int screenWidth, int screenHeight, const TouchDevice& device)
{
    TouchMap map;

    const int rawW = device.maxX - device.minX;
    const int rawH = device.maxY - device.minY;
    if (rawW <= 0 || rawH <= 0) return map;

    const bool rawLandscape    = rawW > rawH;
    const bool screenLandscape = screenWidth >= screenHeight;

    if (rawLandscape != screenLandscape) {
        // The panel reports the axes the other way round: the screen's x axis
        // is the panel's y axis, and the panel's x axis runs from the top of
        // the screen down - the same 90 degree turn Android itself applies.
        map.swapXY  = true;
        map.mirrorY = true;
    }

    return map;
}

}  // namespace overlay
}  // namespace mlbb
