# Optional graphics integrations

This guide documents deliberate exceptions to MAKO Renderer's managed implicit-layer isolation and the separate boundary for game-local integrations. [WSI isolation](WSI-ISOLATION.md) remains the architectural source of truth: the supported default gives MAKO one deterministic presentation owner and exposes only MAKO's private Vulkan manifests.

## Default managed launch

MAKO Decky normally launches a native Steam or Proton game with:

```text
/home/deck/.local/bin/mako-run %command%
```

The wrapper selects MAKO's private `VK_IMPLICIT_LAYER_PATH`, removes additive implicit-layer discovery, disables Gamescope WSI and competing frame-generation identities, and keeps Gamescope itself active outside the game's Vulkan chain. Keep this default unless a specific additional layer has its own compatibility evidence.

## MAKO Decky External Tools

MAKO Decky exposes mutually exclusive **Enable MangoHud** and **Enable vkBasalt** controls under **External Tools**. The selection is stored per profile and applied after restarting the game. Enable a tool in Default to use it for games without their own saved profile, or start a game and choose **Save profile for &lt;game&gt;** before enabling it to limit the selection to that title.

The MangoHud control activates the host layer but does not replace MangoHud's display settings. MangoHud continues to read the user's existing `~/.config/MangoHud/MangoHud.conf`. For a one-game override while the profile control is enabled, keep the normal wrapper and add only the desired options:

```text
/home/deck/.local/bin/mako-run env MANGOHUD_CONFIG=fps,frametime,cpu_stats,gpu_stats,position=top-right %command%
```

The vkBasalt control similarly leaves vkBasalt's own configuration under `~/.config/vkBasalt/` untouched. Both controls currently target a host-installed layer with a 64-bit native Vulkan or Proton game launched directly by Steam on SteamOS. They do not enable external layers inside Flatpak games. The manual commands below remain useful for expert diagnosis, loader tracing, and systems without MAKO Decky. Set both External Tools controls off before using a manual activation command so a profile cannot inject a second external layer.

## Validated MangoHud exception

For a host-installed MangoHud with a 64-bit native Vulkan or Proton game launched directly by Steam on SteamOS, the minimal manual setup is:

```text
/home/deck/.local/bin/mako-run env MANGOHUD=1 NODEVICE_SELECT=1 DISABLE_LAYER_MESA_ANTI_LAG=1 VK_IMPLICIT_LAYER_PATH=/home/deck/.local/share/mako-render/vulkan/implicit_layer.d:/usr/share/vulkan/implicit_layer.d %command%
```

For a more detailed overlay with FPS, frametime, CPU and GPU load and temperatures, RAM and VRAM use, and a top-right position, use:

```text
/home/deck/.local/bin/mako-run env MANGOHUD=1 MANGOHUD_CONFIG=fps,frametime,cpu_stats,cpu_temp,gpu_stats,gpu_temp,ram,vram,position=top-right NODEVICE_SELECT=1 DISABLE_LAYER_MESA_ANTI_LAG=1 VK_IMPLICIT_LAYER_PATH=/home/deck/.local/share/mako-render/vulkan/implicit_layer.d:/usr/share/vulkan/implicit_layer.d %command%
```

`MANGOHUD_CONFIG` accepts comma-separated MangoHud options and takes priority over matching config-file settings. See MangoHud's [example configuration](https://github.com/flightlessmango/MangoHud/blob/master/data/MangoHud.conf) for the complete option list. Changing these display options does not change the Vulkan layer order.

Use the wrapper path displayed by MAKO Decky on systems whose user home is not `/home/deck`.

The position of `env` is intentional. `mako-run` first establishes MAKO's SDR, Gamescope, frame-generation, profile, and diagnostics policy; the child `env` then replaces only the manifest path and enables MangoHud before Steam Runtime Pressure Vessel constructs the game container.

The variables have separate responsibilities:

| Variable | Purpose |
| --- | --- |
| `MANGOHUD=1` | Satisfies MangoHud's manifest activation condition. |
| `MANGOHUD_CONFIG=...` | Optionally customizes the overlay with comma-separated MangoHud settings. It is not required for activation. |
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

## Candidate Vulkan-layer tests

The same narrow manifest-path technique can expose another host Vulkan layer without changing MAKO Renderer. This establishes discovery, not compatibility: the candidate still needs the correct architecture, its own activation condition, a host-visible library, a usable position in the call chain, and real runtime validation. Only the MangoHud lane above is currently validated.

This Deck did not have host-visible vkBasalt, OBS Vulkan Capture, or RenderDoc manifests when this guide was written. Install one candidate at a time and confirm the directory containing its JSON manifest before testing. Do not combine these candidate commands with MangoHud or another unvalidated layer.

For a 64-bit host-installed [vkBasalt](https://github.com/DadSchoorse/vkBasalt) manifest in `/usr/share/vulkan/implicit_layer.d`, test:

```text
/home/deck/.local/bin/mako-run env ENABLE_VKBASALT=1 NODEVICE_SELECT=1 DISABLE_LAYER_MESA_ANTI_LAG=1 VK_IMPLICIT_LAYER_PATH=/home/deck/.local/share/mako-render/vulkan/implicit_layer.d:/usr/share/vulkan/implicit_layer.d %command%
```

For a 64-bit host-visible [OBS Vulkan Capture](https://github.com/nowrep/obs-vkcapture) manifest in `/usr/share/vulkan/implicit_layer.d`, with the matching OBS plugin installed, test:

```text
/home/deck/.local/bin/mako-run env OBS_VKCAPTURE=1 NODEVICE_SELECT=1 DISABLE_LAYER_MESA_ANTI_LAG=1 VK_IMPLICIT_LAYER_PATH=/home/deck/.local/share/mako-render/vulkan/implicit_layer.d:/usr/share/vulkan/implicit_layer.d %command%
```

[RenderDoc](https://github.com/baldurk/renderdoc/blob/v1.x/docs/behind_scenes/vulkan_support.rst) registers a Vulkan capture layer and controls capture through its own launch or injection workflow. Keep RenderDoc's activation environment, add the actual directory containing its registered manifest after MAKO's private manifest directory, and inspect the loader order; there is no validated universal Steam launch command yet.

The provisional goal for vkBasalt and OBS Vulkan Capture is:

```text
Application
    -> MAKO Renderer
        -> candidate layer
            -> Vulkan driver
```

That position gives the candidate an opportunity to observe every original or generated present sent by MAKO, but it does not guarantee that post-processing or capture will understand MAKO-owned images and synchronization. RenderDoc's useful capture position depends on the debugging question, so record the measured order rather than assuming one.

Use these candidates for local experiments, not as a support claim:

| Integration | Mechanism | Current MAKO status |
| --- | --- | --- |
| vkBasalt | Host implicit Vulkan post-processing layer enabled by `ENABLE_VKBASALT=1` | Candidate command documented; not installed or validated on the recorded SteamOS system. |
| OBS Vulkan Capture | Host-visible implicit Vulkan capture layer enabled by `OBS_VKCAPTURE=1`, plus the matching OBS plugin | Candidate command documented; not installed or validated on the recorded SteamOS system. Flatpak OBS also needs the matching capture components at the sandbox boundary. |
| RenderDoc | Developer capture layer registered and activated by RenderDoc | Candidate procedure documented; no universal Steam launch command or validated order yet. Use for diagnosis, not performance evidence. |

## Game-local Proton integrations

ReShade and OptiScaler are normally injected into the Windows game through a proxy DLL and Wine/Proton DLL overrides. They are not admitted through `VK_IMPLICIT_LAYER_PATH`, so MAKO's host Vulkan-layer isolation does not remove their files or activation settings.

This makes coexistence possible in principle, but not automatically supported. Test each title independently with the normal `/home/deck/.local/bin/mako-run %command%` launch, preserve the integration's documented DLL override, and disable every other frame-generation implementation. [OptiScaler](https://github.com/optiscaler/OptiScaler/wiki/Manual-Installation) upscaling may remain active, but OptiScaler frame generation must be off while MAKO owns frame generation. For ReShade effects through a native Linux Vulkan layer rather than a game-local DLL, use vkBasalt's ReShade FX support and follow the candidate-layer test above.

## How general chaining works

`VK_IMPLICIT_LAYER_PATH` is a Vulkan-loader interface, not a MangoHud interface. It accepts colon-separated directories on Linux and replaces normal implicit-layer discovery with manifests from those directories. An implicit layer joins only when its manifest is compatible and its enable/disable conditions permit it.

Multiple layers can form one call chain, but discovery is not proof of compatibility. Directory order influences discovery order, while manifest enumeration within one directory is not a stable way to design an exact presentation order. A layer that intercepts swapchains, presents, synchronization, image ownership, or device selection can change MAKO's behavior even when the game starts successfully.

Use these compatibility rules:

| Layer class | MAKO policy |
| --- | --- |
| MangoHud | Limited opt-in evidence exists for the exact 64-bit SteamOS path above. |
| vkBasalt | Experimental candidate only. Add its manifest directory and activation variable for one local test; validate order, image quality, and pacing. |
| OBS Vulkan Capture | Experimental candidate only. The game and OBS must see their matching capture components; validate that the recording contains generated output and remains stable. |
| RenderDoc | Experimental diagnostic candidate only. Use RenderDoc's registration and activation workflow, expose its manifest deliberately, and measure the resulting order. |
| Gamescope WSI | Excluded from the managed SDR game chain because its upper FIFO policy can conflict with MAKO's generated/original sequence. Gamescope the compositor remains active. |
| Other frame generation | Never combine with MAKO. Two frame generators cannot own one swapchain. |
| Mesa device selection or anti-lag | Excluded by the documented MangoHud guards so the exception changes only the intended overlay boundary. |
| Other capture, post-processing, vendor, or developer layers | Unsupported until their exact activation, order, and runtime matrix have real evidence. |
| Game-local integrations such as ReShade or OptiScaler | Not admitted through this manifest path. Their files and DLL overrides remain untouched, but any other frame-generation implementation must stay disabled when MAKO is active. |

Do not broaden the path to a new manifest directory or remove a guard globally. Add one named compatibility path at a time, record its intended order, and validate native Vulkan, DXVK, VKD3D-Proton, Gamescope, focus/overlay transitions, swapchain recreation, and every supported architecture or sandbox boundary it claims.

For each candidate, start from the normal MAKO launch and verify the game first. Then enable only that candidate, turn on `VK_LOADER_DEBUG=layer` and MAKO presentation diagnostics for one short run, and check all of the following before considering the combination usable:

- the architecture-correct MAKO and candidate identities both appear in the instance and device call stacks;
- no Gamescope WSI, Mesa device-selection, Mesa anti-lag, competing frame-generation, or unrelated layer identity joins the game chain;
- the candidate visibly works or captures the intended original and generated frames;
- Fixed and Adaptive generation remain paced during normal play, hitches, focus changes, overlays, swapchain recreation, and shutdown; and
- removing the candidate restores the normal MAKO baseline.

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
