# MAKO asset provenance

This register records the known origin and intended licensing treatment of public visual assets in MAKO. Update it in the same change whenever an asset is added, replaced, or derived from a new source. Keep original prompts, editable sources, commissions, permissions, and source links outside release archives when they contain private information, but retain enough evidence for the maintainer to substantiate each entry.

## MAKO project artwork

| Paths | Origin and processing | Distribution status |
| --- | --- | --- |
| `assets/leviathan-rising.png`, `assets/abyss-ascending.png`, `assets/abyssal-fang.png`, `assets/sea-rapture.png`, `assets/sea-rapture.webp` | Maintainer-directed release illustrations, selected and edited for MAKO, including AI-assisted creative tooling. `assets/sea-rapture.webp` is the web-optimized derivative of its PNG master. Repository history records their introduction and revisions by Eugenio Segala. No third-party source artwork is intentionally incorporated. | Distributed under GPL-3.0-or-later to the extent copyright or other licensable rights exist and are controlled by the MAKO contributors. |
| `plugin/assets/mako-logo.webp`, `plugin/assets/mako-wiz.webp`, `plugin/assets/mako-cleaner.webp`, `plugin/assets/mako-discord-icon.webp` | Maintainer-supplied MAKO logo and mascot artwork, selected and edited for the project, including AI-assisted creative tooling. Despite its filename, `mako-discord-icon.webp` is MAKO shark artwork rather than the Discord service logo. | Distributed under GPL-3.0-or-later to the extent copyright or other licensable rights exist and are controlled by the MAKO contributors. |
| `engine/mako-ui/rsc/io.github.eugeniosegala.mako.png`, `website/public/assets/mako-render-logo.webp` | MAKO Renderer application and website branding derived from the project's maintainer-supplied visual identity. | Distributed under GPL-3.0-or-later to the extent copyright or other licensable rights exist and are controlled by the MAKO contributors. |
| `website/public/assets/mako-logo.webp`, `website/public/assets/mako-discord-icon.webp` | Byte-identical website copies of the corresponding MAKO project assets under `plugin/assets/`. | Same terms as their source MAKO assets. |
| `website/public/og.png` | Maintainer-produced MAKO website social card assembled for the product website from the project's branding and page design. | Distributed under GPL-3.0-or-later to the extent copyright or other licensable rights exist and are controlled by the MAKO contributors. |

AI-assisted creation does not guarantee that an output is copyrightable in every jurisdiction. MAKO makes no claim beyond rights the contributors actually hold, and the GPL statement does not create rights in third-party material.

## Third-party visual material

| Location | Material | Treatment |
| --- | --- | --- |
| `plugin/src/` imports from `react-icons` | Game Icons, Font Awesome, Feather, Remix Icon, and Material Design icon data | Attributions and license links are recorded in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and upstream license files are shipped in MAKO Decky packages. |
| `website/app/social-icons.tsx` | GitHub and Discord service glyphs | Used only to identify outbound links. The marks remain with GitHub and Discord, are not licensed as MAKO artwork, and must be used in accordance with their current brand guidelines. |

## Maintainer checklist for new assets

- Record the creator, source, date, tool or commission, modifications, and license before committing the asset.
- Do not assume that downloading, prompting, or possessing an image grants redistribution rights.
- Do not include third-party logos, characters, screenshots, or source images in AI editing without permission or a documented legal basis.
- Preserve required attribution close to the asset or in `THIRD_PARTY_NOTICES.md`.
- Confirm that release and website packages contain the applicable notices.
