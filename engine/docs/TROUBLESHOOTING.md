# Troubleshooting

## MAKO does not load

1. Use Vulkan, including DXVK/VKD3D-Proton for Windows games. Select Vulkan in emulators; OpenGL games may need [Zink](CONFIGURATION.md#launch-settings).
2. Copy the complete launch option from **MAKO Renderer Configuration** into Steam and restart. The basic native/Proton option is:

    ```text
    ~/.local/bin/mako-launch %command%
    ```

3. For terminal launches, replace `%command%` with the executable and arguments. Flatpak apps need [Flatpak preparation](FLATPAK-GUIDE.md).
4. Check **Matched Processes** against the game's executable. A saved profile alone does not activate MAKO.

For native loader problems, run:

```bash
~/.local/bin/mako-launch vulkaninfo | grep -i VK_LAYER_MAKO_render
```

Install Vulkan-tools if `vulkaninfo` is missing. If no layer appears, check the installation and launch environment. For 32-bit games, ensure the matching 32-bit layer and manifest are installed.

## The layer loads but a feature does not start

- Enable the feature before launching and restart for options marked **Restart**.
- For Frame Generation or LS1, install Lossless Scaling's default Steam version and check **Lossless.dll Path**. MAKO Scaler and Shaders work without it.
- Clear an unnecessary **GPU** override; Frame Generation must use the game's GPU.
- For scaling, lower the in-game resolution and check the input/output sizes in diagnostics.
- Compare V-Sync on/off and test features separately.

To check configuration errors:

```bash
mako-cli validate --config ~/.config/mako-render/conf.toml
```

## Gamescope WSI, overlays, and HDR

Use [supported graphics integrations](LAYER-CHAINING.md). HDR frame generation is unsupported; keep the HDR and WSI guards in place.

## Collect diagnostics

If the problem remains, [collect a diagnostics report](COLLECT_DIAGNOSTICS.md) and submit it privately through the linked form. Include the game, runtime, settings, and what went wrong; keep logs out of public issues.
