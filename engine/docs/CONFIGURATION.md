# Configuration

**MAKO Renderer Configuration** (`mako-ui`) saves automatically. Options marked **Restart** need a game restart. MAKO Decky shares the same profiles; edit them in one UI at a time.

## Quick start

1. Open **MAKO Renderer Configuration** from the application launcher or run `~/.local/bin/mako-ui`.
2. Create a profile and add the game's executable under **Matched Processes**. Or start the game, use **Detect Running Game…**, save the match, and close it.
3. Choose Frame Generation, Scaling, and/or Shaders.
4. Copy the complete launch option shown by the UI into the game's Steam properties, then launch the game again.

Detection finds the executable; MAKO activates on the next launch. The configuration window can be closed during play. Try display modes and V-Sync on/off if a feature does not work smoothly.

## Lossless Scaling availability

**MAKO Scaler and Shaders work without Lossless Scaling.** Frame Generation and LS1 require it. If MAKO reports it missing, install Lossless Scaling or correct its DLL path, then restart the game.

## Profiles

**Create New Profile** copies the selected profile's settings, matches, power modes, and shaders. **Default** cannot be renamed or deleted.

The dropdown selects the profile you edit; local games use their executable matches. If detection fails, use **Show all applications** or add the executable manually. Match the game, not its launcher, Steam title, or ROM filename.

### AC and battery settings

Under **Power Profiles**, enable **Separate power settings** and choose **Editing settings for** to configure Handheld, Docked, or Base.

Battery selects Handheld; connecting a charger selects Docked, even without a dock. An external display alone does not. Base applies when power detection is unavailable at startup or separate settings are disabled. DLL, FP16, Shaders, and launcher settings stay shared; power changes follow normal live/restart rules.

### Launcher exclusions

Known launchers and helpers are excluded so their child games can match normally. If detection finds only a launcher, wait for gameplay and try again.

## Native Steam Remote Play

On the receiving device, close streams and select **Override Remote Play**. Choose a saved profile before streaming. See the [Remote Play guide](../../plugin/docs/REMOTE-PLAY.md) for setup and restoration.

## Profile settings

### Frame Generation

Enable Frame Generation before launching. **Real FPS** counts game frames; output FPS includes generated frames. Targets depend on the game, GPU, and display.

| Control | What it does |
| --- | --- |
| **Fixed Multiplier** | Requests 2x–5x output. Start at 2x; `0x` pauses generation. |
| **Adaptive Frame Generation** | Varies generation toward **Target FPS**, up to **Max Adaptive Multiplier**. |
| **Match Display Refresh Rate** | Uses Gamescope's refresh as the Adaptive target; **Fallback Target FPS** applies when feedback is unavailable. |
| **Steady Base Cap** | Initially caps real FPS at half the target for even cadence; may reduce responsiveness. |
| **Fractional Adaptive / Real Frame Priority** | Keeps more real frames; higher priority allows more real FPS, but delivery may feel less even. |
| **Base FPS Cap** | Limits real FPS. **Off** adds no manual cap; Steady or explicit Fractional caps take precedence. |
| **Smooth Cadence** | Favours consistent delivery; compare on/off for responsiveness. |
| **Gamescope VRR** | Follows Steam, or temporarily requests VRR on/off on a supported display during play. |

### Scaling

Enable Scaling before launching. Set Steam's **Game Resolution** to the display maximum, then choose a lower in-game resolution. Gamescope is recommended; try different display modes if needed.

| Method | Use it for |
| --- | --- |
| **Native Resolution** | Simple model-free reconstruction |
| **MAKO Scaler** | Open scaling with anti-ringing and sharpening |
| **LS1 Quality** | Highest-quality LS1 scaling; requires Lossless Scaling |
| **LS1 Performance** | Lower-cost LS1 scaling; requires Lossless Scaling |

**Scale Factor** targets 1.0x–2.0x output dimensions; **Sharpness** adjusts sharpening. **Quality Supersampling** can improve quality on supported Gamescope surfaces, with higher GPU and memory use. LS1 failures fall back to MAKO Scaler.

### Performance and device selection

| Control | What it does |
| --- | --- |
| **Ultra Performance (Restart)** | Uses 70% Flow Scale, the lighter FG model, and LS1 Performance when scaling is enabled. |
| **Flow Scale** | Lower values reduce GPU cost and motion-estimation quality. |
| **Lighter FG Model** | Reduces GPU work, with potentially more artifacts. |
| **Allow FP16 (Restart)** | Uses FP16 for supported Frame Generation models and MAKO Scaler; off uses FP32. LS1 always uses FP32. |
| **Lossless.dll Path (Restart)** | Optional override; leave empty for automatic detection. |
| **GPU (Restart)** | Selects a GPU; multi-GPU Frame Generation is unsupported. |

### Qt Shaders and compatibility controls

Enable **Shaders** before launching to use bundled vkBasalt. Select **Effects** in the desired order; more effects cost more GPU time. **Sharpening** offers CAS or DLS; **Anti-aliasing** offers FXAA or SMAA. **HDR Look (SDR)** changes appearance without enabling HDR.

**Add Custom Shader** registers a vkBasalt-compatible ReShade `.fx` file; select its entry in **Effects** to enable it. Keep includes and textures accessible, including inside Flatpak sandboxes. **Delete selected custom shaders** removes registrations from this profile; original files remain. Use **Refresh** after external edits. Failed shader loads keep the previous chain active.

Under **Compatibility**, keep defaults unless a game needs a change:

- **Auto-disable Frame Generation by Refresh Rate:** pauses generation and Adaptive caps at or below a confirmed Gamescope threshold; a manual Base FPS Cap remains.
- **Dynamic Cadence Recovery:** checks games that change frame rate between scenes or menus; clears real-frame caps while enabled.
- **Game Swapchain Images (Restart):** may help games that fail to start, with less generated-frame headroom.

### Desktop scaling and resolution

For 1920×1080 output at 1.5x, aim for a 1280×720 input. Raising the factor without lowering resolution can increase GPU and memory cost. Check source and presentation sizes in [diagnostics](COLLECT_DIAGNOSTICS.md); see [Scaling](SCALING.md) for supported display setups.

## Applying changes

| Timing | Common settings |
| --- | --- |
| **Restart** | Enabling Frame Generation, Scaling, or Shaders; DLL; FP16; GPU; Ultra Performance; launcher compatibility controls |
| **Live** | Most Frame Generation controls; scaling method and sharpness; Flow Scale; Lighter FG Model; shader adjustments |
| **May wait for resolution change or restart** | Scale Factor, Quality Supersampling, and changes needing more Frame Generation capacity |

Some live changes rebuild resources and may briefly hitch. See [Runtime transitions](RUNTIME-TRANSITIONS.md) for details.

## Standalone launcher

For native Steam and Proton games, copy the UI's complete launch option. Without shaders or an explicit profile, the user-local archive uses:

```text
~/.local/bin/mako-launch %command%
```

The Arch package uses `/usr/bin/mako-launch`. For a terminal launch, replace `%command%` with the executable and arguments. Flatpak applications need [Flatpak preparation](FLATPAK-GUIDE.md).

**Disable Steam Overlay (Restart)** is on by default and may reduce stutter. It disables the in-game Steam overlay and FPS counter and may affect Steam Input; Gaming Mode menus and its performance overlay remain available. Turn it off and restart to restore normal Steam integration.

**Zink** runs OpenGL through Vulkan. **Force ALSA** changes the audio path for compatibility problems. Both require a restart. Steam Deck mode, Gamescope WSI, and MangoHud controls are available in MAKO Decky.

## Advanced configuration

For manual TOML fields, defaults, and environment variables, see the [advanced reference](CONFIGURATION-REFERENCE.md). For problems, see [troubleshooting](TROUBLESHOOTING.md) or [collect diagnostics](COLLECT_DIAGNOSTICS.md).
