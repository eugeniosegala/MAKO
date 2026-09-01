# Optional graphics integrations

This guide covers deliberate exceptions to MAKO's managed implicit-layer isolation and the separate boundary for game-local integrations. [WSI isolation](WSI-ISOLATION.md) owns the default: profiles without Scaling, explicit Gamescope WSI compatibility, or a selected post-process tool expose only MAKO's private manifests. Scaling uses its guarded WSI split even when the live method is Native; it does not admit unrelated host layers.

Commands using `/home/deck/.local/bin/mako-run` are MAKO Decky workflows. Standalone MAKO Renderer uses `mako-launch`; external-layer chaining through it is not validated.

## Default MAKO Decky managed launch

MAKO Decky normally launches a native Steam or Proton game with:

```text
/home/deck/.local/bin/mako-run %command%
```

The default wrapper exposes only MAKO's private manifests, removes additive discovery, disables Gamescope WSI and competing frame generation, and leaves the Gamescope compositor active. Keep it unless a named exception has evidence.

## Gamescope WSI compatibility

MAKO Decky exposes **Gamescope WSI (Restart)** under **Compatibility Settings**. The independently enabled FG-only lane is limited to supported 64-bit host launches and exists for reports such as [#7](https://github.com/eugeniosegala/MAKO/issues/7) and [#12](https://github.com/eugeniosegala/MAKO/issues/12), where users observed coloured or pixelated artifacts during camera movement on the default isolated path and reported that older WSI-enabled configurations changed the result. Scaling instead requires and locks its validated managed WSI path.

The per-profile toggle is off by default and independent from the post-process selector; Scaling automatically locks the same WSI requirement. HDR stays disabled. Inside an active Gamescope session, MAKO preserves the exact frame-generation → WSI → spatial-scaling layer order and places a selected 64-bit MangoHud or vkBasalt layer last by exporting that managed prefix through `VK_INSTANCE_LAYERS`. The spatial role below WSI owns capability virtualization and physical lower-swapchain extent expansion, while the upper combined role owns extent-selected pre/post reconstruction with Frame Generation; role order and resource ownership are deliberately distinct. It never admits the complete host directory. Desktop Mode, a mismatched nested Wayland session, invalid WSI evidence, or invalid lower-role evidence fails closed to top-only MAKO with scaling suppressed; session fallback is written to stderr without loading the external WSI layer or showing its modal hooking-error dialog, and other optional layers fail closed independently.

MAKO Decky validates and stages the host manifest and its exact 64-bit library during Renderer installation, then repairs that managed copy automatically when an existing installation loads after a plugin upgrade. The lane applies inside an active Gamescope session on a host with the SteamOS-style `VK_LAYER_FROG_gamescope_wsi_x86_64` payload: directly launched 64-bit native Vulkan or Proton games use the managed host path, while prepared 64-bit Heroic and EmuDeck Flatpak launches receive the same bounded WSI payload plus the mounted MAKO runtime extension. It does not apply to Desktop Mode, mismatched nested Wayland sessions, unprepared Flatpaks, 32-bit WSI presentation, or HDR.

Gamescope WSI remains a compatibility boundary even in the split chain. The real compositor lane must prove both positive generated delivery and active scaling, not merely loader discovery or a visual change. Test Fixed 2× before Adaptive, confirm that a final-output counter reports generated FPS, and disable Scaling or the explicit compatibility toggle if pacing or output regresses.

## Recommended setup: MAKO Decky External Tools

MAKO Decky exposes mutually exclusive **Enable MangoHud** and **Enable vkBasalt** controls under **External Tools**. This post-process selection is independent from **Gamescope WSI** and Scaling, so either tool can run after the last active MAKO role while WSI remains between frame generation and spatial scaling. This is the recommended way to use either external tool because MAKO generates the guarded activation and ordered layer path automatically.

1. Install MangoHud or vkBasalt on the SteamOS host.
2. Keep the normal Steam launch option: `/home/deck/.local/bin/mako-run %command%`.
3. Select Default to apply the tool to games without a saved profile, or start a game and choose **Save profile for &lt;game&gt;** to create a profile for that title.
4. Under **External Tools**, enable either MangoHud or vkBasalt. Only one can be selected.
5. Restart the game after changing the selection.

Both controls stage the selected host tool's exact available 64-bit and 32-bit manifest identities. Runtime ordering evidence currently covers a 64-bit native Vulkan or Proton game launched directly by Steam on SteamOS; the 32-bit discovery contract is preserved but still needs runtime evidence. The controls do not enable external layers inside Flatpak games.

### Customize MangoHud while its toggle is enabled

The toggle leaves MangoHud's settings in `~/.config/MangoHud/MangoHud.conf` untouched. To override a few options for one game, keep the toggle enabled and use:

```text
/home/deck/.local/bin/mako-run env MANGOHUD_CONFIG=fps,frametime,cpu_stats,gpu_stats,position=top-right %command%
```

`MANGOHUD_CONFIG` overrides matching config-file values. See MangoHud's [example configuration](https://github.com/flightlessmango/MangoHud/blob/master/data/MangoHud.conf). Do not add activation or layer-path variables when the Decky toggle is enabled.

### Configure experimental vkBasalt while its toggle is enabled

The vkBasalt toggle activates the host layer and leaves vkBasalt's effects and settings under `~/.config/vkBasalt/` untouched. Configure effects there using vkBasalt's normal workflow; MAKO Decky only controls whether the layer is admitted for the selected profile.

## Manual MAKO Decky activation without the toggles

These expert diagnostic commands keep Decky's profile and launch policy but bypass **External Tools**. Turn Gamescope WSI compatibility and both External Tools controls off first so the profile cannot add a second optional layer.

### MangoHud through MAKO Decky without its toggle

For a host-installed MangoHud with a 64-bit native Vulkan or Proton game launched directly by Steam on SteamOS, the minimal manual setup is:

```text
/home/deck/.local/bin/mako-run env MANGOHUD=1 NODEVICE_SELECT=1 DISABLE_LAYER_MESA_ANTI_LAG=1 VK_IMPLICIT_LAYER_PATH=/home/deck/.local/share/mako-render/vulkan/implicit_layer.d:/usr/share/vulkan/implicit_layer.d %command%
```

Add `MANGOHUD_CONFIG=...` after `MANGOHUD=1` for per-game display options. This changes the overlay, not layer order.

Use the wrapper path displayed by MAKO Decky on systems whose user home is not `/home/deck`.

The position of `env` is intentional: `mako-run` establishes MAKO policy first, then the child overrides only the candidate manifest path before Pressure Vessel constructs the game container.

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

### vkBasalt through MAKO Decky without its toggle (experimental)

For a 64-bit host-installed [vkBasalt](https://github.com/DadSchoorse/vkBasalt) manifest in `/usr/share/vulkan/implicit_layer.d`, with both MAKO Decky External Tools toggles off, the manual candidate command is:

```text
/home/deck/.local/bin/mako-run env ENABLE_VKBASALT=1 NODEVICE_SELECT=1 DISABLE_LAYER_MESA_ANTI_LAG=1 VK_IMPLICIT_LAYER_PATH=/home/deck/.local/share/mako-render/vulkan/implicit_layer.d:/usr/share/vulkan/implicit_layer.d %command%
```

This command exposes vkBasalt without using its MAKO Decky toggle, but the integration remains experimental and was not installed or validated on the recorded SteamOS system.

## Candidate Vulkan-layer tests through MAKO Decky

The same narrow manifest-path technique can expose another host Vulkan layer without changing MAKO Renderer. This establishes discovery, not compatibility: the candidate still needs the correct architecture, its own activation condition, a host-visible library, a usable position in the call chain, and real runtime validation. Only the MangoHud lane above is currently validated.

Install and validate one candidate at a time. Confirm its actual manifest directory and do not combine candidates.

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

That order lets a candidate observe original and generated presents but does not prove it understands MAKO-owned images or synchronization. RenderDoc's useful position depends on the debugging goal, so record the measured order.

Use these candidates for local experiments, not as a support claim:

| Integration | Mechanism | Current MAKO status |
| --- | --- | --- |
| vkBasalt | Host implicit Vulkan post-processing layer selected through MAKO Decky's per-profile **External Tools** control or enabled manually inside its `mako-run` workflow with `ENABLE_VKBASALT=1` | Experimental UI and manual activation paths are available; the recorded SteamOS system did not have vkBasalt installed, so runtime compatibility remains unvalidated. |
| OBS Vulkan Capture | Host-visible implicit Vulkan capture layer enabled by `OBS_VKCAPTURE=1`, plus the matching OBS plugin | Candidate command documented; not installed or validated on the recorded SteamOS system. Flatpak OBS also needs the matching capture components at the sandbox boundary. |
| RenderDoc | Developer capture layer registered and activated by RenderDoc | Candidate procedure documented; no universal Steam launch command or validated order yet. Use for diagnosis, not performance evidence. |

## Game-local Proton integrations

ReShade and OptiScaler are normally injected into the Windows game through a proxy DLL and Wine/Proton DLL overrides. They are not admitted through `VK_IMPLICIT_LAYER_PATH`, so MAKO's host Vulkan-layer isolation does not remove their files or activation settings.

This makes coexistence possible in principle, but not automatically supported. Test each title independently with the normal `/home/deck/.local/bin/mako-run %command%` launch, preserve the integration's documented DLL override, and disable every other frame-generation implementation. [OptiScaler](https://github.com/optiscaler/OptiScaler/wiki/Manual-Installation) upscaling may remain active, but OptiScaler frame generation must be off while MAKO owns frame generation. For ReShade effects through a native Linux Vulkan layer rather than a game-local DLL, use vkBasalt's ReShade FX support and follow the candidate-layer test above.

## How general chaining works

`VK_IMPLICIT_LAYER_PATH` is a Vulkan-loader interface, not a MangoHud interface. It accepts colon-separated directories on Linux and replaces normal implicit-layer discovery with manifests from those directories. An implicit layer joins only when its manifest is compatible and its enable/disable conditions permit it. MAKO Decky's native managed WSI/scaling path separately exports `VK_INSTANCE_LAYERS` to make the supported 64-bit role order deterministic; the discovery path alone is not an ordering contract.

Multiple layers can form one call chain, but discovery is not proof of compatibility. Directory order influences discovery order, while manifest enumeration within one directory is not a stable way to design an exact presentation order. A layer that intercepts swapchains, presents, synchronization, image ownership, or device selection can change MAKO's behavior even when the game starts successfully.

Use these compatibility rules:

| Layer class | MAKO policy |
| --- | --- |
| MangoHud | Available through the per-profile **Enable MangoHud** toggle, with a guarded MAKO Decky manual command for use while the toggle is disabled. The managed path stages exact available 64-bit and 32-bit identities; activation and ordering are verified only for the exact 64-bit SteamOS path above. |
| vkBasalt | Available through the per-profile **Enable vkBasalt** experimental toggle or the guarded MAKO Decky manual candidate command. Activation wiring is implemented, but runtime order, image quality, and pacing still need vkBasalt hardware validation. |
| OBS Vulkan Capture | Experimental candidate only. The game and OBS must see their matching capture components; validate that the recording contains generated output and remains stable. |
| RenderDoc | Experimental diagnostic candidate only. Use RenderDoc's registration and activation workflow, expose its manifest deliberately, and measure the resulting order. |
| Gamescope WSI | Excluded from the isolated managed SDR chain. MAKO Decky's explicit compatibility toggle admits one validated 64-bit host payload below the frame-generation role; Scaling additionally requires the spatial-scaling role below WSI. Prepared Heroic and EmuDeck Flatpaks receive the same bounded order without exposing the host layer directory. HDR remains disabled. It is independent from the MangoHud/vkBasalt post-process selector. Real-game Flatpak pacing, 32-bit presentation, HDR, and broader platform support remain separate evidence boundaries. |
| Other frame generation | Never combine with MAKO. Two frame generators cannot own one swapchain. |
| Mesa device selection or anti-lag | Excluded by the guarded External Tools paths and documented manual commands so only the selected integration joins MAKO's layer chain. |
| Other capture, post-processing, vendor, or developer layers | Unsupported until their exact activation, order, and runtime matrix have real evidence. |
| Game-local integrations such as ReShade or OptiScaler | Not admitted through this manifest path. Their files and DLL overrides remain untouched, but any other frame-generation implementation must stay disabled when MAKO is active. |

Do not broaden the path to a new manifest directory or remove a guard globally. Add one named compatibility path at a time, record its intended order, and validate native Vulkan, DXVK, VKD3D-Proton, Gamescope, focus/overlay transitions, swapchain recreation, and every supported architecture or sandbox boundary it claims.

For each candidate, start from the normal MAKO launch and verify the game first. Then enable only that candidate, turn on `VK_LOADER_DEBUG=layer` and MAKO presentation diagnostics for one short run, and check all of the following before considering the combination usable:

- the architecture-correct MAKO and candidate identities both appear in the instance and device call stacks;
- Gamescope WSI appears only when its per-profile compatibility toggle is selected or the native profile started with Scaling enabled; its identity follows `VK_LAYER_MAKO_render`, precedes `VK_LAYER_MAKO_spatial_scaling` when scaling is provisioned, and remains before any selected post-process tool, while Mesa device-selection, Mesa anti-lag, competing frame-generation, and unrelated layer identities stay out of the game chain;
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

Recorded hardware evidence proves the 64-bit Steam/Proton Pressure Vessel order `Application -> MAKO Renderer -> MangoHud -> Vulkan driver`, not universal performance or compatibility. MAKO Gym and retained traces should own dated run details. These boundaries remain separate:

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
