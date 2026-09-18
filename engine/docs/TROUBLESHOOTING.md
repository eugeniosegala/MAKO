# Troubleshooting

## MAKO does not load

1. Confirm that the game uses Vulkan, including DXVK or VKD3D-Proton for compatible Windows games. Select the Vulkan backend in emulators. A native OpenGL game needs the optional [Zink launcher setting](CONFIGURATION.md#standalone-launcher).
2. Installing MAKO or opening its configuration window does not activate it. For a native Steam or Proton game, add this under **Steam Properties > General > Launch Options**, then restart the game:

    ```text
    ~/.local/bin/mako-launch %command%
    ```

    For a direct desktop launch, replace `%command%` with the game command. For a Flatpak application, follow the [Flatpak preparation and verification steps](FLATPAK-GUIDE.md) instead; the host launcher cannot prepare its sandbox.

3. For a native installation, check that the Vulkan loader can see the layer:

    ```bash
    ~/.local/bin/mako-launch vulkaninfo | grep -i VK_LAYER_MAKO_render
    ```

    Install your distribution's Vulkan-tools package if `vulkaninfo` is unavailable. No output usually means the layer manifest was not installed in a Vulkan search path, the process is Flatpak-sandboxed, or the layer is disabled by its launch environment.

4. For a 32-bit game, install both `lib32/libmako-render.so` and `VkLayer_MAKO_render.x86.json`. The manifest must report `"library_arch": "32"`.

Use `VK_LOADER_DEBUG=layer` with the normal launch command when you need to see Vulkan-loader decisions. Look for `VK_LAYER_MAKO_render` and the `MAKO Renderer: render layer active` message.

## The layer loads but a feature does not start

- Validate the configuration:

    ```bash
    mako-cli validate --config ~/.config/mako-render/conf.toml
    ```

- Check the active profile. `active_in` must match the actual Linux binary, Windows executable, process name, or path suffix. Set `MAKO_PROFILE` to a known profile name to test profile matching explicitly.
- On multi-GPU systems, the profile's `gpu` must identify the same GPU used by the game.
- For Frame Generation or LS1, confirm that the **default public version** of Lossless Scaling is installed through Steam and that MAKO can find `Lossless.dll`. MAKO can use beta branches, but they are not validated; the default public branch is recommended. Set `dll` in the configuration if the library is in a non-standard Steam location.
- For scaling, enable it before launching the game. Native Resolution and MAKO Scaler need no licensed model; LS1 also needs `Lossless.dll` and an architecture-matched `libvkd3d-shader.so.1`. Check Live Status or `mako-diagnostics scaling` for the effective factor and any inactive reason.
- Test the game's V-Sync both on and off. Also check its own FPS limiter, VRR, and compositor settings before changing MAKO options.

For a Flatpak game or emulator, the host layer is not visible inside the sandbox. Install the matching MAKO Flatpak extension and application overrides as described in the [Flatpak guide](FLATPAK-GUIDE.md). MAKO Decky manages those steps through **Flatpak Setup**.

## Gamescope WSI, overlays, and HDR

The supported launcher gives MAKO a private implicit-layer chain, disables Gamescope WSI inside the game process, and keeps HDR exposure off. Gamescope and Steam/Game Mode remain active, but implicit overlays, capture layers, and post-processing layers are not admitted automatically. See [WSI isolation](WSI-ISOLATION.md) for loader evidence and compatibility tradeoffs.

HDR frame generation is not currently supported. Do not remove the WSI or HDR guards as a general workaround: layer membership is fixed before Vulkan starts, and the experimental HDR lane has a different presentation contract. See [HDR pipeline architecture](HDR-PIPELINE.md) for its implemented fallbacks and the validation required before exposure.

## Collect diagnostics

Presentation diagnostics are off by default. Add the following before the normal launch command to log slow presentation operations:

```bash
MAKO_PRESENT_DIAGNOSTICS=1 \
MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 \
~/.local/bin/mako-launch your-game-command
```

For a Steam game, replace `your-game-command` with `%command%`. Steam captures the renderer's output in `~/.steam/steam/logs/console-linux.txt`; a direct desktop launch writes it to the terminal or launcher log.

After reproducing the issue and fully quitting the game, create the focused report with:

```bash
mako-diagnostics --lines 2000 all
```

For the complete end-to-end workflow, including Steam, direct commands, Heroic or Flatpak setups, creating `MAKO-diagnostics.txt` on the Desktop, restoring normal settings, and using the shared submission form, see [Collect Standalone MAKO Renderer Diagnostics](COLLECT_DIAGNOSTICS.md).

`MAKO_PRESENT_ACQUIRE_TIMEOUT_MS` sets one shared deadline for all ordered generated-image acquisitions in an application present, so higher multipliers cannot multiply the wait. Exhaustion or elapsed-time overrun enters native recovery. MAKO Decky uses 50 ms. A pool that fits the generated batch but has no additional relief image always uses at most 50 ms, including standalone launches; a shorter configured deadline remains authoritative. Other standalone ordered paths retain the unbounded compatibility default when unset. For a focused stall reproduction, try `25` and include the log.

## Report an issue

Follow [Collect Standalone MAKO Renderer Diagnostics](COLLECT_DIAGNOSTICS.md) to reproduce the problem, create the focused Desktop report, restore normal launch settings, and submit the file privately. The shared form asks for the report context, so do not paste the diagnostic text into a public GitHub issue. Maintainers can use the [Adaptive validation guide](ADAPTIVE-VALIDATION.md) when a follow-up requires the full deterministic and runtime test matrix.
