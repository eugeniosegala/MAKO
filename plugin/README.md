# MAKO Decky

<p align="center">
  <img src="assets/mako-logo.webp" width="256" alt="MAKO Decky logo" />
</p>

<!-- prettier-ignore -->
> [!NOTE]
> **MAKO Decky succeeds <a href="https://github.com/eugeniosegala/decky-lsfg-vk-experimental" target="_blank" rel="noopener noreferrer">Decky LSFG-VK Experimental</a> under a separate product and package identity.** The <a href="https://github.com/eugeniosegala/MAKO" target="_blank" rel="noopener noreferrer">MAKO repository</a> continues its development lineage, but MAKO Decky imports state only from public MAKO 2.0.0 or newer; install it separately from the differently named predecessor.

MAKO Decky is the Decky Loader component of MAKO. It provides per-game controls, installation, updates, Flatpak preparation, and game launch integration for MAKO Renderer on Steam Deck, Steam Machine, SteamOS, and Linux more broadly.

MAKO is an independent community project bringing LSFG frame generation, LS1 scaling, and the built-in open MAKO Scaler to Linux. MAKO Decky does not contain or distribute Lossless Scaling, `Lossless.dll`, or extracted proprietary model payloads. LSFG and LS1 read selected resources at runtime from a lawful, user-supplied <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> installation; the open MAKO Scaler does not require it. MAKO does not alter the user's DLL file, and translated resources remain process-local. Users are responsible for complying with the terms applicable to their copy. See <a href="../THIRD_PARTY_NOTICES.md" target="_blank" rel="noopener noreferrer">Third-party notices</a>.

## Download

For frame generation or LS1 scaling, first install the **default public version** of <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> through Steam, with beta participation disabled. The open MAKO Scaler works without `Lossless.dll`.

Open the <a href="https://github.com/eugeniosegala/MAKO/releases/latest" target="_blank" rel="noopener noreferrer">latest MAKO Decky release</a> and download the ZIP under **Assets**. Previous Decky releases are available on the <a href="https://github.com/eugeniosegala/MAKO/releases" target="_blank" rel="noopener noreferrer">MAKO releases page</a>.

For direct Vulkan-layer installation without Decky, open the <a href="https://github.com/eugeniosegala/MAKO/releases/tag/render-v3.2.1" target="_blank" rel="noopener noreferrer">latest MAKO Renderer release</a> and download the Linux archive under **Assets**.

Published MAKO Renderer packages target x86_64 Linux hosts, with 64-bit and 32-bit x86 game-process layers. MAKO Decky safely refuses incompatible native AArch64/Armada installation; see <a href="docs/ARMADA.md" target="_blank" rel="noopener noreferrer">Armada and native AArch64 support</a> for that boundary.

## What it manages

- Installs and updates the per-user MAKO Renderer Vulkan layer and common `mako-run` wrapper.
- Saves per-game and per-process profiles, then selects them automatically by Steam application ID or process name.
- Groups Fixed and Adaptive Frame Generation, Spatial Scaling, performance, compatibility, external-tool, and manual controls. **Live Status** reports the active mode, scaler, resolutions, limits, fallbacks, and pending changes for the running game.
- Provides a per-profile Gamescope WSI compatibility option plus host-installed MangoHud or experimental vkBasalt. Scaling uses the combined Renderer by default; the independent WSI option selects the managed compatibility path inside a supported Gamescope session.
- Prepares matching Vulkan runtime extensions and application access for supported Flatpak workflows.
- Shares one active native Renderer version with the standalone archive installer. Installing either version selects it for both launch workflows; a later MAKO Decky installation adopts a valid standalone Renderer and offers its bundled update when the versions differ.
- Removes files supplied by either managed native Renderer installer when you select **Uninstall MAKO Renderer**, while preserving MAKO Decky and its profiles. Uninstalling MAKO Decky also removes the managed native Renderer; shared Flatpak runtime extensions remain installed.

Close games using MAKO before installing or updating the Renderer. Installation preserves valid profiles. If the existing configuration cannot be read or validated, including an unsupported format version, installation recreates `conf.toml` with defaults and replaces the profiles stored in that file. Read-only configurations still stop installation. A failed install restores the previous native files, selected Renderer identity, and configuration; if restoration encounters another filesystem error, the error identifies retained recovery backups. Generated files retain the owner's required permissions and respect a more restrictive host umask without repeated rewrites.

## Development

MAKO Decky lives in the `plugin/` directory of the MAKO monorepo and consumes the sibling `engine/` source tree. Run these commands from `plugin/`:

[Native installation transactions](../INSTALLATION-TRANSACTIONS.md) documents atomic replacement, rollback, shared native identity, failure boundaries, and contract tests for both installers.

`components/FeatureSettings.tsx` and `components/ConfigurationSection.tsx` compose the editor. Independent performance, advanced rendering, compatibility, external-tool, and manual-override sections live under `components/settings/`, alongside the shared collapse control and editor prop types. Their renderers forward edits through the existing callbacks; `hooks/useProfileEditorModel.ts` and `hooks/useProfileConfigWriter.ts` remain the state and persistence owners. Keep translations in the source catalogs and keep save/debounce effects out of section components.

`components/InfoVisibility.tsx` owns the panel-local R1 shortcut, persistent controls-only view, and focus scrolling. On an info toggle it cancels queued navigation scrolling, refocuses the selected control after layout, and adjusts its scroll container to preserve its screen position within the available scroll range. A disappearing informational control moves focus to the next visible, enabled control at the same screen position, or the previous control when none follows; the ribbon is the last fallback when no other controls remain. It hides Decky's resolved field-description class; `components/MakoInfo.tsx` unmounts MAKO's informational content so hidden buttons cannot remain in Steam's controller navigation. Use `MakoInfo` for explanatory containers and `MakoInfo as={PanelSectionRow}` for whole informational rows, retaining `data-mako-info="true"` on the content for focus recovery. Keep settings and actions outside these containers, and keep collapse-state hooks above them so showing information restores the previous choices.

`hooks/usePersistentCollapseState.ts` persists hidden/collapsed preferences under each component's existing browser key. It accepts only saved booleans, falls back to each view's default for damaged or invalid values, and keeps the controls usable when storage is unavailable.

`components/ModelWarning.tsx` is exempt from the info toggle: its DLL/model failure guidance and update action stay mounted and visible. Its warning uses `MakoInlineTip alwaysVisible` and carries no hidden-info marker, so R1 also preserves focus on its update action. Other tips and optional warnings continue to follow the display preference.

```bash
pnpm install --frozen-lockfile
pnpm run test
pnpm run build
pnpm run package:local-engine
```

`pnpm run package:local-engine` builds and bundles the sibling MAKO Renderer checkout. Use `pnpm run package:local-engine-fast` for a native, 64-bit development package without Flatpak extensions.

The resulting ZIP is written under `plugin/out/`; local commands never publish. Use direct `dev:*` deployment for iteration, `package:local-engine` for a tester ZIP, and the documented release workflow only for a release candidate. See <a href="docs/PACKAGING.md" target="_blank" rel="noopener noreferrer">Packaging</a> and <a href="../TESTING.md" target="_blank" rel="noopener noreferrer">Testing</a> for the exact commands and validation gates.

## Using a local build

After installing the ZIP through Decky developer settings, open MAKO Decky and install MAKO Renderer. For a native Steam or Proton game, use:

```text
/home/deck/.local/bin/mako-run %command%
```

The wrapper enables MAKO for the launch. MAKO Renderer selects a saved profile by process identity and uses the Default profile when no saved match exists.

For Heroic, Lutris, EmuDeck, and other Flatpak applications, follow the [launcher setup guide](docs/LAUNCHERS.md).

See <a href="docs/CONFIGURATION.md" target="_blank" rel="noopener noreferrer">Configuration</a>, <a href="docs/ARMADA.md" target="_blank" rel="noopener noreferrer">Armada and native AArch64 support</a>, <a href="docs/TROUBLESHOOTING.md" target="_blank" rel="noopener noreferrer">Troubleshooting</a>, <a href="docs/COLLECT_DIAGNOSTICS.md" target="_blank" rel="noopener noreferrer">Collect MAKO Decky Diagnostics</a>, and <a href="docs/PACKAGING.md" target="_blank" rel="noopener noreferrer">Packaging</a> for detailed workflows.
