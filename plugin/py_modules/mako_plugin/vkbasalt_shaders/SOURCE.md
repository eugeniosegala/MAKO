# Bundled vkBasalt shader sources

Vibrance.fx, Curves.fx, Technicolor.fx, Sepia.fx, Monochrome.fx, Vignette.fx, FakeHDR.fx, Technicolor2.fx, DPX.fx, FilmGrain.fx, Cartoon.fx, Nostalgia.fx, and ChromaticAberration.fx are derived from CeeJayDK/SweetFX commit `407c11562950195c1b45461fbb59f4bd6bbe7ba4` under the bundled MIT license. MAKO replaces their `ReShadeUI.fxh` UI macros with equivalent explicit annotations so that no separately licensed UI helper is required; their algorithms and defaults are otherwise unchanged.

Colourfulness.fx is copied from crosire/reshade-shaders commit `4fee10cdac28f0a6d4fa5ddd778faf0016ab7b91`. It is copyright 2016–2018 bacondither and carries its complete permissive BSD-style license in both the source header and `LICENSE-Colourfulness`. MAKO makes only the same `ReShadeUI.fxh` annotation substitution described above.

BleachBypass.fx and Noir.fx are original MAKO shaders distributed under GPL-3.0-or-later. They have no external textures or third-party shader dependencies beyond the bundled CC0 ReShade.fxh compatibility header.

ReShade.fxh is copied from crosire/reshade-shaders `slim` commit `6db142b4b1a05c764222e5b0bd9a644b7ccfe1dc` and declares `SPDX-License-Identifier: CC0-1.0`. The CC0-1.0 legal code is available from <https://creativecommons.org/publicdomain/zero/1.0/legalcode>.

MAKO installs these files into its user-owned configuration directory so native and prepared Flatpak games resolve the same immutable shader sources.
