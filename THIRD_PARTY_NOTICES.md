# MAKO third-party notices

This document supplements [LICENSE.md](LICENSE.md). It identifies third-party material distributed with MAKO and proprietary software that MAKO can use but does not distribute. Each third-party copyright, license, and trademark remains with its owner.

## Lossless Scaling is not distributed by MAKO

MAKO is an independent open-source project. MAKO source archives, MAKO Renderer packages, MAKO Decky packages, Flatpak extensions, and the MAKO website do not contain or distribute Lossless Scaling, `Lossless.dll`, or extracted LS1 or LSFG model and shader payloads.

The open MAKO Scaler works without Lossless Scaling. A user who chooses LSFG frame generation or LS1 scaling must independently obtain a lawful copy of Lossless Scaling and select or permit discovery of its locally installed `Lossless.dll`. The user is responsible for complying with the terms and law applicable to that copy. MAKO grants no license or other rights in Lossless Scaling or its proprietary resources.

When a licensed model is selected, MAKO Renderer reads the user-supplied local DLL at runtime and uses the resources needed for the selected LSFG or LS1 feature. MAKO does not install, upload, package, or alter the DLL, or save extracted resources as standalone files.

Lossless Scaling, LS1, and LSFG are used descriptively and remain the property of their respective owner. MAKO is not affiliated with or endorsed by Lossless Scaling.

## Incorporated Renderer and Decky source

The complete notices for the original Decky LSFG-VK plugin, the GPL-3.0-or-later lsfg-vk version 2 Renderer lineage, toml++, Bjoern Hoehrmann's UTF-8 decoder, and Steam Deck Homebrew components are reproduced in [LICENSE.md](LICENSE.md).

### lsfg-vk Renderer provenance

MAKO Renderer descends directly from PancakeTAS's GPL-3.0-or-later lsfg-vk version 2 tree at upstream commit [`8b0da2661c6f3473a7fccc8ba643880050e71642`](https://github.com/PancakeTAS/lsfg-vk/commit/8b0da2661c6f3473a7fccc8ba643880050e71642). MAKO's experimental predecessor carried that baseline to `v2.0.0-dev28-experimental.25` at [`276030d4925c40038a61ecd66bd49ce777faec8c`](https://github.com/eugeniosegala/lsfg-vk-experimental/commit/276030d4925c40038a61ecd66bd49ce777faec8c), and that Renderer lineage entered this monorepo in [`8ed1cdbe7f5496fade8ad01d85ac3d671957fcac`](https://github.com/eugeniosegala/MAKO/commit/8ed1cdbe7f5496fade8ad01d85ac3d671957fcac).

Copyright in the incorporated portions remains with the respective lsfg-vk copyright holders. The incorporated source files identify themselves as `GPL-3.0-or-later`, and MAKO remains distributed under that license. The MIT License used for lsfg-vk version 1 does not describe MAKO Renderer's version 2 lineage.

## Gamescope surface protocol

MAKO Renderer's optional scaling surface adapter declares the association/destruction requests and version-one events from Valve's [Gamescope swapchain protocol at commit `2d217a16c7e5b56c7417257279bf102320cff024`](https://github.com/ValveSoftware/gamescope/blob/2d217a16c7e5b56c7417257279bf102320cff024/protocol/gamescope-swapchain.xml). This protocol material carries the following MIT notice:

Copyright © 2023 Joshua Ashton for Valve Software

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next paragraph) shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## Bundled vkBasalt Vulkan layer

MAKO Renderer native archives and Flatpak extensions include 64-bit and 32-bit vkBasalt libraries from MAKO's maintained fork release [`mako-v0.3.2.10-1`](https://github.com/eugeniosegala/vkBasalt/releases/tag/mako-v0.3.2.10-1), source commit [`e6d9ad459bd6872166fcc31f921c940fb694ceae`](https://github.com/eugeniosegala/vkBasalt/commit/e6d9ad459bd6872166fcc31f921c940fb694ceae), based on upstream vkBasalt commit [`4f97f09ffe91900e6ca136cc26cf7966f8f6970d`](https://github.com/DadSchoorse/vkBasalt/commit/4f97f09ffe91900e6ca136cc26cf7966f8f6970d). [`engine/vkbasalt-release.json`](engine/vkbasalt-release.json) records the immutable asset and checksum used by packaging.

vkBasalt is copyright © 2019–2022 Georg Lehmann and is distributed under the zlib License. Its bundled ReShade shader headers are copyright © 2014 Patrick Mours and distributed under the BSD 3-Clause License. MAKO packages the complete notices as `share/doc/mako-render/vkbasalt/LICENSE` and `share/doc/mako-render/vkbasalt/RESHade-LICENSE.md`, plus exact source provenance and the MAKO pin. Those files remain part of every archive and runtime extension containing vkBasalt.

## MAKO Decky frontend

MAKO Decky's compiled frontend includes the following third-party code and icon data. Declared dependencies are in `plugin/package.json`, and exact resolutions are in `plugin/pnpm-lock.yaml`. MAKO Decky packages include the upstream license files under `third_party_licenses/` and a frontend source map with the bundled dependency source content.

- **@decky/api**, Steam Deck Homebrew, GNU Lesser General Public License version 2.1. Source: <https://github.com/SteamDeckHomebrew/decky-frontend-lib>.
- **React Icons**, copyright 2018 kamijin_fanta, MIT License. Source: <https://github.com/react-icons/react-icons>.
- **tslib**, copyright Microsoft Corporation, 0BSD License. Source: <https://github.com/microsoft/tslib>.

React Icons retains the licenses of its source icon projects. MAKO Decky uses these packs:

- **Game Icons:** “Shark fin” by Delapouite, <https://game-icons.net/1x1/delapouite/shark-fin.html>, licensed under [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/). MAKO uses the icon through React Icons and does not intentionally modify the artwork.
- **Font Awesome Free icons:** copyright Fonticons, Inc., licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Source and license information: <https://fontawesome.com/license/free>.
- **Feather icons:** copyright Cole Bemis, licensed under the MIT License. Source: <https://github.com/feathericons/feather>.
- **Remix Icon:** copyright Remix Design, licensed under the Apache License 2.0. Source: <https://github.com/Remix-Design/RemixIcon>.
- **Material Design icons:** copyright Google LLC, licensed under the Apache License 2.0. Source: <https://github.com/google/material-design-icons>.

The upstream React Icons license file shipped in MAKO Decky identifies these icon-pack licenses and their license locations.

## MAKO website

The deployed website includes notices for its shipped frontend dependencies in `third-party-notices.txt`. These include React, React DOM, Scheduler, React Server DOM Webpack, Next.js, Vinext, and Tailwind CSS under their respective MIT licenses. Build-only tools are not part of the deployed website unless their code is emitted into the production artifact.

GitHub and Discord names and glyphs on the website identify links to the corresponding services. They are trademarks of their respective owners and do not imply sponsorship or endorsement.

## Project artwork

MAKO's project artwork and the provenance limits recorded for it are documented in [ASSET_PROVENANCE.md](ASSET_PROVENANCE.md). Third-party brand assets are not relicensed by MAKO.
