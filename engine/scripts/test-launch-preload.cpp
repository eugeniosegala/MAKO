/* SPDX-License-Identifier: GPL-3.0-or-later */

// Synthetic preload hooks for the launcher's real exec-boundary contract.
#if defined(MAKO_TEST_OVERLAY)
extern "C" int mako_test_overlay_hook() { return 1; }
#elif defined(MAKO_TEST_RETAINED)
extern "C" int mako_test_retained_hook() { return 1; }
#else
#include <cstdio>
#include <dlfcn.h>

int main() {
    std::printf("overlay=%d retained=%d\n",
        dlsym(RTLD_DEFAULT, "mako_test_overlay_hook") != nullptr,
        dlsym(RTLD_DEFAULT, "mako_test_retained_hook") != nullptr);
}
#endif
