// -----------------------------------------------------------------------------
//  TouchReader.h - reads the touch screen straight from /dev/input.
//
//  A surface that lives outside the input dispatch tree (which is what lets
//  touches reach the game while the menu is on top) also never receives any
//  input itself, so the menu has to read the raw events.  Everything here is
//  read-only: the devices are opened O_RDONLY, never grabbed by default and
//  never written to, so the game keeps working underneath.
//
//  Panel axes are reported in the panel's own coordinate system, which may be
//  rotated and/or mirrored relative to the screen we draw into.  TouchMap
//  describes that relation; TouchInput::Detect() guesses it from the reported
//  axis ranges and the surface aspect, and `--touch-*` flags override it.
// -----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <vector>

namespace mlbb {
namespace overlay {

struct TouchMap {
    bool swapXY  = false;   // the screen's x axis is the panel's y axis
    bool mirrorX = false;
    bool mirrorY = false;
    int  rot     = 0;       // extra 0/90/180/270 turn, applied after the swap
};

struct TouchDevice {
    int  fd   = -1;
    char name[64] = { 0 };
    int  minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool multitouch = false;   // reports ABS_MT_SLOT (protocol B)
    bool hasKeys    = false;   // volume keys are visible through this node too
    bool grabbed    = false;
};

struct TouchState {
    float x = 0.0f, y = 0.0f;   // screen pixels
    bool  down    = false;      // at least one finger is on the glass
    bool  changed = false;      // Poll() touched the state
};

class TouchInput {
public:
    ~TouchInput();

    // `screenWidth`/`screenHeight` are the pixels of the surface we draw into.
    // `hint` carries the --touch-* overrides and `setMask` says which of them
    // the user actually passed (bit 0 swap, 1 mirror-x, 2 mirror-y, 3 rot);
    // every field that is not selected is detected from the first touch device.
    // Returns false when no touch screen could be opened (no root, or a ROM
    // that hides /dev/input) - the caller then just runs without input.
    bool Init(int screenWidth, int screenHeight, const TouchMap& hint, unsigned setMask,
              bool grabDevices, bool verbose);
    void Close();

    // Waits up to `timeoutMs` for events, then folds them into the state.
    // Returns true when the pointer moved or changed state.
    bool Poll(int timeoutMs);

    const TouchState& State() const { return m_state; }
    float NormX() const { return m_normX; }
    float NormY() const { return m_normY; }

    // Volume-up + volume-down held together: a root overlay has no window
    // manager and no back button, so this is the "please stop" combo.
    bool ExitChord() const { return m_exitChord; }

    int  DeviceCount() const { return (int)m_devices.size(); }
    const TouchDevice& Device(int index) const { return m_devices[index].info; }
    void PrintDevices() const;

    // Guess the panel -> screen mapping from the raw axis ranges and the
    // surface aspect ratio. Correct on every phone seen so far; the flags are
    // there because "seen so far" is not "all of them".
    static TouchMap Detect(int screenWidth, int screenHeight, const TouchDevice& device);

    // Bits for TouchInput::Init()'s setMask.
    enum { MapSetSwap = 1u << 0, MapSetMirrorX = 1u << 1, MapSetMirrorY = 1u << 2, MapSetRot = 1u << 3 };

private:
    struct Slot {
        bool  down = false;
        int   rawX = 0, rawY = 0;
        int   order = 0;      // which finger touched down most recently
    };

    struct DeviceState {
        TouchDevice info;
        Slot slots[10];
        int  orderCounter = 0;
    };

    void CommitDevice(int deviceIndex);

    std::vector<DeviceState> m_devices;
    TouchMap m_map;
    TouchState m_state;
    float m_normX = 0.0f, m_normY = 0.0f;
    bool  m_exitChord = false;
    bool  m_volUp = false, m_volDown = false;
    int   m_screenW = 0, m_screenH = 0;
    int   m_activeDevice = -1, m_activeSlot = -1, m_activeOrder = -1;
};

}  // namespace overlay
}  // namespace mlbb
