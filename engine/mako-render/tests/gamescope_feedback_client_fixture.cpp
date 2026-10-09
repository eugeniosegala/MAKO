/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <string_view>

namespace {
    constexpr std::array names{
        "GAMESCOPE_PID", "GAMESCOPE_XWAYLAND_SERVER_ID",
        "GAMESCOPE_DISPLAY_REFRESH_RATE_FEEDBACK", "GAMESCOPE_VRR_ENABLED",
        "GAMESCOPE_VRR_CAPABLE", "GAMESCOPE_VRR_FEEDBACK", "GAMESCOPE_ALLOW_TEARING",
        "GAMESCOPE_FOCUSED_APP_GFX", "GAMESCOPE_FOCUSED_APP",
        "GAMESCOPE_HDR_OUTPUT_FEEDBACK", "GAMESCOPE_COLOR_APP_HDR_METADATA_FEEDBACK",
        "GAMESCOPE_COLOR_APP_WANTS_HDR_FEEDBACK",
    };
    std::atomic<unsigned> hdrAtoms{}, hdrReads{}, refresh{90};
}

extern "C" {
    void mako_test_feedback_reset() { hdrAtoms = 0; hdrReads = 0; refresh = 90; }
    unsigned mako_test_feedback_hdr_atoms() { return hdrAtoms.load(); }
    unsigned mako_test_feedback_hdr_reads() { return hdrReads.load(); }
    void mako_test_feedback_refresh(unsigned value) { refresh = value; }
    Display* XOpenDisplay(const char*) { return reinterpret_cast<Display*>(1); }
    int XCloseDisplay(Display*) { return 0; }
    Window XDefaultRootWindow(Display*) { return 1; }
    int XGetWindowAttributes(Display*, Window, XWindowAttributes* attributes) {
        *attributes = {}; attributes->width = 1920; attributes->height = 1080;
        return 1;
    }
    Atom XInternAtom(Display*, const char* name, Bool) {
        for (size_t index = 0; index < names.size(); ++index) {
            if (std::string_view(name) != names[index]) continue;
            if (index >= 9) ++hdrAtoms;
            return index + 1;
        }
        return None;
    }
    int XGetWindowProperty(Display*, Window, Atom atom, long, long, Bool,
            Atom, Atom* type, int* format, unsigned long* count,
            unsigned long* remaining, unsigned char** data) {
        if (atom >= 10) ++hdrReads;
        const unsigned long value = atom == 1 ? 77 : atom == 2 ? 0 :
            atom == 3 ? refresh.load() : atom == 8 || atom == 9 ? 730 : 1;
        auto* result = static_cast<unsigned long*>(std::malloc(sizeof(unsigned long)));
        if (!result) return BadAlloc;
        *result = value; *type = XA_CARDINAL; *format = 32; *count = 1; *remaining = 0;
        *data = reinterpret_cast<unsigned char*>(result);
        return Success;
    }
    int XFree(void* data) { std::free(data); return 0; }
}
