# MAKO third-party notices

This document supplements [LICENSE.md](LICENSE.md). It identifies third-party material distributed with MAKO and proprietary software that MAKO can use but does not distribute. Each third-party copyright, license, and trademark remains with its owner.

## Lossless Scaling is not distributed by MAKO

MAKO is an independent open-source project. MAKO source archives, MAKO Renderer packages, MAKO Decky packages, Flatpak extensions, and the MAKO website do not contain or distribute Lossless Scaling, `Lossless.dll`, or extracted LS1 or LSFG model and shader payloads.

The open MAKO Scaler works without Lossless Scaling. A user who chooses LSFG frame generation or LS1 scaling must independently obtain a lawful copy of Lossless Scaling and select or permit discovery of its locally installed `Lossless.dll`. The user is responsible for complying with the terms and law applicable to that copy. MAKO grants no license or other rights in Lossless Scaling or its proprietary resources.

When a licensed model is selected, MAKO Renderer reads only the required resources from the user-supplied local DLL at runtime. Translated GPU representations remain process-local. MAKO does not install, upload, include in packages, or persist the DLL or extracted proprietary payloads, and it does not alter the user's DLL file. Repository and release gates reject DLLs, extracted models, raw shaders, and disguised protected binary payloads.

Lossless Scaling, LS1, and LSFG are used descriptively and remain the property of their respective owner. MAKO is not affiliated with or endorsed by Lossless Scaling.

## Incorporated Renderer and Decky source

The complete notices for the original Decky LSFG-VK plugin, lsfg-vk components, toml++, Bjoern Hoehrmann's UTF-8 decoder, and Steam Deck Homebrew components are reproduced in [LICENSE.md](LICENSE.md).

### lsfg-vk provenance boundary

The original implementation from which MAKO Renderer descends incorporated portions of an earlier lsfg-vk revision published under the MIT License. The upstream project subsequently adopted the GNU General Public License version 3 for its later development. MAKO preserves the historical MIT notice and attribution in [LICENSE.md](LICENSE.md); they apply only to source incorporated under those earlier terms and its descendants in MAKO, not to later upstream releases.

MAKO Renderer is now substantially different in architecture and implementation. As a project provenance policy, MAKO does not copy, merge, port, or otherwise import source from later GPLv3-licensed lsfg-vk revisions. Further development proceeds from MAKO's own codebase and project history. This statement is informational and does not impose an additional condition on MAKO recipients beyond the licenses otherwise identified in these documents.

## MAKO Decky frontend

MAKO Decky's compiled frontend includes the following third-party code and icon data. Exact dependency versions are recorded in `plugin/package.json` and `plugin/pnpm-lock.yaml`. MAKO Decky packages include copies of the upstream dependency license files under `third_party_licenses/` and retain a frontend source map with the exact bundled dependency source content.

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
