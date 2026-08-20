# Optional Vulkan layer chaining

This guide documents deliberate exceptions to MAKO Renderer's managed implicit-layer isolation. [WSI isolation](WSI-ISOLATION.md) remains the architectural source of truth: the supported default gives MAKO one deterministic presentation owner and exposes only MAKO's private Vulkan manifests.

## Default managed launch

MAKO Decky normally launches a native Steam or Proton game with:

```text
/home/deck/.local/bin/mako-run %command%
```

The wrapper selects MAKO's private `VK_IMPLICIT_LAYER_PATH`, removes additive implicit-layer discovery, disables Gamescope WSI and competing frame-generation identities, and keeps Gamescope itself active outside the game's Vulkan chain. Keep this default unless a specific additional layer has its own compatibility evidence.

## Validated MangoHud exception

For a host-installed MangoHud with a 64-bit native Vulkan or Proton game launched directly by Steam on SteamOS, use:

```text
/home/deck/.local/bin/mako-run env MANGOHUD=1 NODEVICE_SELECT=1 DISABLE_LAYER_MESA_ANTI_LAG=1 VK_IMPLICIT_LAYER_PATH=/home/deck/.local/share/mako-render/vulkan/implicit_layer.d:/usr/share/vulkan/implicit_layer.d %command%
```

Use the wrapper path displayed by MAKO Decky on systems whose user home is not `/home/deck`.

The position of `env` is intentional. `mako-run` first establishes MAKO's SDR, Gamescope, frame-generation, profile, and diagnostics policy; the child `env` then replaces only the manifest path and enables MangoHud before Steam Runtime Pressure Vessel constructs the game container.

The variables have separate responsibilities:

| Variable | Purpose |
| --- | --- |
| `MANGOHUD=1` | Satisfies MangoHud's manifest activation condition. This is the only MangoHud-specific setting. |
| `NODEVICE_SELECT=1` | Disables SteamOS's normally active `VK_LAYER_MESA_device_select` manifest after the system directory is exposed. |
| `DISABLE_LAYER_MESA_ANTI_LAG=1` | Prevents `VK_LAYER_MESA_anti_lag` from joining if another environment enables it. |
| `VK_IMPLICIT_LAYER_PATH=...` | Exposes MAKO's private manifests first and SteamOS's host implicit-layer manifests second. |

MAKO's generated wrapper separately keeps `DISABLE_GAMESCOPE_WSI=1`, removes `ENABLE_GAMESCOPE_WSI`, and disables the known LSFG-VK frame-generation identities. With the current SteamOS manifest set and the guards above, the verified call chain is:

```text
Application
    -> MAKO Renderer
        -> MangoHud
            -> Vulkan driver
```

MAKO is closer to the application. Each generated or original present that MAKO sends downward passes through MangoHud, so MangoHud reports generated output FPS rather than only the game's source FPS.

Do not use `mangohud /home/deck/.local/bin/mako-run %command%`. That launcher runs before `mako-run`; MAKO then replaces implicit discovery, while MangoHud's preload shim can propagate into Steam Runtime helper processes without loading the Vulkan overlay in the game.

## How general chaining works

`VK_IMPLICIT_LAYER_PATH` is a Vulkan-loader interface, not a MangoHud interface. It accepts colon-separated directories on Linux and replaces normal implicit-layer discovery with manifests from those directories. An implicit layer joins only when its manifest is compatible and its enable/disable conditions permit it.

Multiple layers can form one call chain, but discovery is not proof of compatibility. Directory order influences discovery order, while manifest enumeration within one directory is not a stable way to design an exact presentation order. A layer that intercepts swapchains, presents, synchronization, image ownership, or device selection can change MAKO's behavior even when the game starts successfully.

Use these compatibility rules:

| Layer class | MAKO policy |
| --- | --- |
| MangoHud | Limited opt-in evidence exists for the exact 64-bit SteamOS path above. |
| Gamescope WSI | Excluded from the managed SDR game chain because its upper FIFO policy can conflict with MAKO's generated/original sequence. Gamescope the compositor remains active. |
| Other frame generation | Never combine with MAKO. Two frame generators cannot own one swapchain. |
| Mesa device selection or anti-lag | Excluded by the documented MangoHud guards so the exception changes only the intended overlay boundary. |
| Capture, post-processing, vendor, or developer layers | Unsupported until their exact order and runtime matrix have real evidence. |
| Game-local integrations such as OptiScaler | Not admitted through this manifest path. Their files remain untouched, but their frame-generation implementation must stay disabled when MAKO is active. |

Do not broaden the path to a new manifest directory or remove a guard globally. Add one named compatibility path at a time, record its intended order, and validate native Vulkan, DXVK, VKD3D-Proton, Gamescope, focus/overlay transitions, swapchain recreation, and every supported architecture or sandbox boundary it claims.

## Verify the active chain

For one focused run, add loader and MAKO diagnostics before the working command:

```text
VK_LOADER_DEBUG=layer MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 /home/deck/.local/bin/mako-run env MANGOHUD=1 NODEVICE_SELECT=1 DISABLE_LAYER_MESA_ANTI_LAG=1 VK_IMPLICIT_LAYER_PATH=/home/deck/.local/share/mako-render/vulkan/implicit_layer.d:/usr/share/vulkan/implicit_layer.d %command%
```

Quit the game, then run in Desktop Mode:

```bash
/home/deck/.local/bin/mako-diagnostics layers --lines 2000
```

The game instance and device call stacks must contain `VK_LAYER_MAKO_render` above the architecture-correct `VK_LAYER_MANGOHUD_overlay_*` identity. Gamescope WSI, Mesa device selection, Mesa anti-lag, and other frame-generation identities must not appear in that game call stack. Remove `VK_LOADER_DEBUG` and presentation diagnostics after verification because synchronous diagnostic output can disturb frame pacing.

## Evidence and limits

The current evidence was collected on 2026-08-20 with Resident Evil 4 launched as a 64-bit Proton game through Steam Runtime Pressure Vessel under Gamescope on an AMD Radeon Graphics RADV NAVI33 device. MangoHud was visible, MAKO frame generation remained active, and the Vulkan loader recorded `Application -> MAKO Renderer -> MangoHud -> Vulkan driver` at instance and device creation.

That run proves activation and ordering for this lane, not universal performance or compatibility. The following remain untested:

- 32-bit games and complete 32-bit MangoHud runtime dependencies;
- Heroic, EmuDeck, and other Flatpak applications;
- non-SteamOS host manifest layouts;
- non-RADV drivers and other GPU vendors; and
- capture, post-processing, vendor, validation, and additional overlay layers.

## Roll back

If the overlay is absent, the game fails to start, frame pacing regresses, or the loader reports an unexpected layer, restore the normal launch option:

```text
/home/deck/.local/bin/mako-run %command%
```

Reproduce without the additional layer before attributing the problem to MAKO's scheduler or presentation path.
