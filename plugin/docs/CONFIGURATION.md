# Configuration guide

MAKO Decky saves settings automatically. Options marked **Restart** apply the next time the game starts; other supported changes apply while the game is running. **Live Status** shows the profile and settings currently applied, including pending restarts and fallbacks.

## Quick start

1. Add `/home/deck/.local/bin/mako-run %command%` to the game's Steam launch options. See [launcher setup](LAUNCHERS.md) for Heroic, Lutris, EmuDeck, and Flatpak applications.
2. Start the game, open MAKO Decky, and select **Save profile for &lt;game&gt;** after gameplay loads.
3. Choose Frame Generation, Scaling, and/or Shaders from the **Image Processing** tabs.
4. Restart the game after changing any option marked **Restart**.
5. Reopen MAKO Decky and check **Live Status** to confirm what is active.

Test one feature at a time at first. Try the game's V-Sync setting both on and off and keep whichever feels smoother for that game.

## Profiles

The **Default** profile applies when no saved game or process profile matches. Saving a game profile records its Steam app ID and safe process names so MAKO can select it automatically on later launches.

The profile dropdown chooses which profile you are editing; it does not force that profile onto the running game. Use **Matched Processes** only when a launcher, emulator, or unusual game executable needs an additional match.

## Frame Generation

Turn on **Enable Frame-gen (Restart)** before starting the game.

- **Fixed** uses the selected 2x–5x multiplier. Start with 2x. Select `0x` to pause generation without losing the saved multiplier.
- **Adaptive** varies generation toward **Target FPS** without slowing a game already above the target. Set **Maximum Multiplier** only as high as needed.
- **Steady Base Cap** favours a stable cadence. **Fractional Adaptive** retains more real frames and may reduce latency, but can feel less even.
- **Real Frame Priority** is available with Fractional Adaptive and controls the preferred balance between real and generated frames.
- **Smooth Cadence** favours consistent delivery. Disable it if the game feels more responsive without it.
- **Auto-disable by Refresh Rate** pauses generation at or below the selected Gamescope refresh threshold.

Most Frame Generation controls apply live after Frame Generation was enabled at startup. Changes that require a larger private resource set may cause a brief hitch or wait for a game recreation.

## Spatial Scaling

Turn on **Enable Scaling (Restart)** before starting the game. Gamescope/Game Mode is recommended because it provides a reliable display target.

| Method | Use it for | Requirement |
| --- | --- | --- |
| **Native Resolution** | Simple model-free reconstruction | None |
| **MAKO Scaler** | Open scaling with anti-ringing and sharpening | None |
| **LS1 Quality** | Highest-quality LS1 reconstruction | Licensed `Lossless.dll` |
| **LS1 Performance** | Lower-cost LS1 reconstruction | Licensed `Lossless.dll` |

Set Steam's **Game Resolution** to the display maximum, then choose a lower resolution inside the game. Scaling is active when **Live Status** shows an input resolution smaller than the display resolution. Some games require Windowed mode because fullscreen or borderless keeps a display-sized input.

- **Scale Factor** controls the output size from 1.0x to 2.0x.
- **Sharpness** controls reconstruction sharpening and is hidden for Native Resolution.
- **Quality Supersampling** can improve quality on supported Gamescope surfaces but increases GPU and memory use.

Method and sharpness changes apply live. Factor or supersampling changes may wait for a resolution change, swapchain recreation, or restart. If LS1 cannot load, MAKO safely uses MAKO Scaler and reports the fallback without changing the saved method.

## Shaders

Turn on **Enable Shaders (Restart)** to use MAKO's private bundled vkBasalt build. No separate vkBasalt installation is needed.

- **Effects** is a multi-selection list. Effects run in the displayed order; unchecking and rechecking an effect moves it to the end. **Clear all** removes the chain.
- Sharpening, sharpness, DLS denoise, anti-aliasing, and the selected effects apply live after Shaders was enabled at startup. Rebuilding an effect chain may cause a brief hitch.
- Combining several effects increases GPU cost.
- **HDR Look (SDR)** adjusts contrast and colour but remains SDR; it does not enable HDR output or increase display luminance.

The note below the controls shows the active profile configuration file. Advanced users can edit that file in Desktop Mode or over SSH. MAKO updates only the settings represented in its UI and preserves other options, comments, and lines. Avoid editing the same profile in MAKO Decky and the Qt configuration window simultaneously.

## Performance settings

- **Ultra Performance (Restart)** selects a lighter preset: 70% Flow Scale, the lighter FG model, FP16 where supported, and LS1 Performance when Scaling is enabled.
- **Flow Scale** trades motion-estimation quality for GPU cost.
- **Lighter FG Model** reduces GPU work but may show more artifacts.
- **Allow FP16 (Restart)** normally improves performance on AMD hardware. Some older NVIDIA GPUs may perform better with it disabled.
- **Lossless.dll Path (Restart)** is an optional override; leave it empty for automatic Steam-library detection.
- **GPU (Restart)** selects a GPU on multi-GPU systems. Multi-GPU Frame Generation is unsupported.

## Compatibility and external tools

Leave compatibility options at their defaults unless a game needs them.

- **Dynamic Cadence Recovery** helps games and emulators that switch between rates, such as 30 FPS gameplay and 60 FPS menus.
- **Gamescope WSI (Restart)** is an optional compatibility path for coloured or pixelated motion artifacts in supported 64-bit Gamescope launches.
- **Game Swapchain Images (Restart)** may help titles that fail to start with MAKO's normal generated-output headroom, but can reduce generated-frame availability.
- **Disable MAKO Renderer on Next Launch** temporarily bypasses the Renderer for troubleshooting.
- **Disable Steam Deck Mode**, **Zink**, and **Force ALSA** are per-game compatibility switches and require a restart.
- **MangoHud (Restart)** uses the host installation and cannot run alongside MAKO Shaders for the same profile.

Frame Generation and Scaling remain SDR-only in this release. **Disable HDR** remains enabled and read-only.

## When changes apply

| Behavior | Common settings |
| --- | --- |
| **Restart the game** | Enabling Frame Generation, Scaling, or Shaders; Ultra Performance; FP16; DLL path; GPU; Gamescope WSI; launcher compatibility controls |
| **Applies live** | Most Fixed and Adaptive controls; Scaling method and sharpness; shader effects, sharpening, anti-aliasing, and denoise |
| **May wait for recreation** | Scale Factor, Quality Supersampling, and Frame Generation changes that need more capacity |

A restart-bound change does not block unrelated live changes. Check **Live Status** when the UI and running game appear to disagree.

## Panel helpers

Press **R1** or select **Hide info** for a controls-only panel; repeat the action to restore explanations. **Advanced Details** shows installed Renderer and Lossless Scaling information, and its values can be selected to copy them.

If MAKO reports an unavailable LSFG or LS1 model, update MAKO Renderer, verify the Lossless Scaling installation, restart the game, and collect diagnostics if the warning remains.

For implementation details and advanced configuration, see [Renderer configuration](../../engine/docs/CONFIGURATION.md), [runtime transitions](../../engine/docs/RUNTIME-TRANSITIONS.md), [spatial scaling](../../engine/docs/SCALING.md), [optional graphics integrations](../../engine/docs/LAYER-CHAINING.md), and [troubleshooting](TROUBLESHOOTING.md).
