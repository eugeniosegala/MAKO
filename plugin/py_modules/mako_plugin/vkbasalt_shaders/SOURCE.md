# Bundled vkBasalt shader sources

Vibrance.fx and Curves.fx are derived from CeeJayDK/SweetFX commit `407c11562950195c1b45461fbb59f4bd6bbe7ba4` under the bundled MIT license. MAKO replaces their `ReShadeUI.fxh` slider macro with the equivalent explicit `ui_type = "slider"` annotation so that no separately licensed UI helper is required; the shader algorithms and defaults are unchanged.

ReShade.fxh is copied from crosire/reshade-shaders `slim` commit `6db142b4b1a05c764222e5b0bd9a644b7ccfe1dc` and declares `SPDX-License-Identifier: CC0-1.0`. The CC0-1.0 legal code is available from <https://creativecommons.org/publicdomain/zero/1.0/legalcode>.

MAKO installs these files into its user-owned configuration directory so native and prepared Flatpak games resolve the same immutable shader sources.
