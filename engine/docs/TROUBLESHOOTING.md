# Troubleshooting

## MAKO does not load

1. Confirm that the game uses Vulkan. MAKO does not attach to an OpenGL-only process.
2. Launch the game with MAKO enabled and competing LSFG-VK layers disabled. For Steam, use:

    ```text
    DISABLE_LSFG=1 DISABLE_LSFGVK=1 ENABLE_MAKO=1 %command%
    ```

3. Check that the Vulkan loader can see the layer:

    ```bash
    DISABLE_LSFG=1 DISABLE_LSFGVK=1 ENABLE_MAKO=1 vulkaninfo | grep -i VK_LAYER_MAKO_render
    ```

    Install your distribution's Vulkan-tools package if `vulkaninfo` is unavailable. No output usually means the layer manifest was not installed in a Vulkan search path, the process is Flatpak-sandboxed, or the layer is disabled by its launch environment.

4. For a 32-bit game, install both `lib32/libmako-render.so` and `VkLayer_MAKO_render.x86.json`. The manifest must report `"library_arch": "32"`.

Use `VK_LOADER_DEBUG=layer` with the normal launch command when you need to see Vulkan-loader decisions. Look for `VK_LAYER_MAKO_render` and the `mako: render layer active` message.

## The layer loads but frame generation does not start

- Confirm that Lossless Scaling is installed through Steam and that MAKO can find `Lossless.dll`. Set `dll` in the configuration if the library is in a non-standard Steam location.
- Validate the configuration:

    ```bash
    mako-cli validate --config ~/.config/mako-render/conf.toml
    ```

- Check the active profile. `active_in` must match the actual Linux binary, Windows executable, process name, or path suffix. Set `MAKO_PROFILE` to a known profile name to test profile matching explicitly.
- On multi-GPU systems, the profile's `gpu` must identify the same GPU used by the game.
- Test the game's V-Sync both on and off. Also check its own FPS limiter, VRR, and compositor settings before changing MAKO options.

For a Flatpak game or emulator, the host layer is not visible inside the sandbox. Install the matching MAKO Flatpak extension and application overrides as described in the [Flatpak guide](FLATPAK-GUIDE.md). MAKO Decky manages those steps through **Flatpak Setup**.

## Collect diagnostics

Presentation diagnostics are off by default. Add the following before the normal launch command to log slow presentation operations:

```bash
MAKO_PRESENT_DIAGNOSTICS=1 \
MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 \
DISABLE_LSFG=1 \
DISABLE_LSFGVK=1 \
ENABLE_MAKO=1 \
your-game-command
```

For a Steam game, replace `your-game-command` with `%command%`. Steam captures the renderer's output in `~/.steam/steam/logs/console-linux.txt`; a direct desktop launch writes it to the terminal or launcher log.

After reproducing the issue and fully quitting the game, create the focused report with:

```bash
mako-diagnostics --lines 2000 all
```

For the complete end-to-end workflow, including Steam, direct commands, Heroic or Flatpak setups, creating `MAKO-diagnostics.txt` on the Desktop, restoring normal settings, and using the shared submission form, see [Collect Standalone MAKO Renderer Diagnostics](COLLECT_DIAGNOSTICS.md).

`MAKO_PRESENT_ACQUIRE_TIMEOUT_MS` is a recovery diagnostic. Leave it unset for normal play. If you are reproducing a presentation stall, set a small value such as `25` and include the resulting log with the report.

## Report an issue

Follow [Collect Standalone MAKO Renderer Diagnostics](COLLECT_DIAGNOSTICS.md) to reproduce the problem, create the focused Desktop report, restore normal launch settings, and submit the file privately. The shared form asks for the report context, so do not paste the diagnostic text into a public GitHub issue. Maintainers can use the [Adaptive validation guide](ADAPTIVE-VALIDATION.md) when a follow-up requires the full deterministic and runtime test matrix.
