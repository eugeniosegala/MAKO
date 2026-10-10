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

**Create New Profile** copies the selected profile's settings, matches, power modes, and shaders. Use **Rename Profile** or **Delete Profile** to manage copies; **Default** is protected.

The dropdown selects the profile you edit; local games use their executable matches. If detection fails, use **Show all applications** or add the executable manually. Match the game, not its launcher, Steam title, or ROM filename.

### AC and battery settings

Under **Power Profiles**, enable **Separate power settings**, then choose Handheld, Docked, or Base under **Editing settings for**.

Battery selects Handheld; charging selects Docked without requiring a dock. An external display alone does not switch sets. Base applies if startup power detection fails or separate settings are off. DLL, FP16, Shaders, and launcher settings stay shared; normal live/restart rules apply.

### Launcher exclusions

Known launchers and helpers are excluded so their child games can match normally. If detection finds only a launcher, wait for gameplay and try again.

## Native Steam Remote Play

On the receiving device, close streams and select **Override Remote Play**. Choose a saved profile before streaming. **Restore Steam Client** removes the override; **Refresh** checks its status. See the [Remote Play guide](../../plugin/docs/REMOTE-PLAY.md) for setup and limits.

## Profile settings

### Frame Generation

Turn on **Enable Frame-gen (Restart)** before launching. **Real FPS** counts game frames; output FPS includes generated frames. In Fixed or Adaptive mode, `0x` pauses generation and its FPS caps live; selecting 2x–5x resumes it.

| Control | What it does |
| --- | --- |
| **Fixed Multiplier** | Requests 2x–5x output. Start at 2x. |
| **Adaptive Frame Generation** | Varies generation toward **Target FPS** (10–1000), up to **Maximum Adaptive Multiplier** (2x–5x). |
| **Match Display Refresh Rate** | Uses Gamescope's refresh as the Adaptive target; **Fallback Target FPS** applies without feedback. Turn matching off to edit the target. |
| **Steady Base Cap** | Initially caps real FPS at half the target for even cadence; may reduce responsiveness. |
| **Fractional Adaptive** | Keeps more real frames, but delivery may feel less even. |
| **Real Frame Priority** | Low–Very High select a Fractional real-FPS cap; higher priority allows more real FPS. **Automatic** adds no priority cap. |
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

**Scale Factor** targets 1.0x–2.0x output dimensions, within display limits; **Sharpness** adjusts sharpening except in Native Resolution. **Quality Supersampling** can improve quality on supported Gamescope surfaces, with higher GPU and memory use. LS1 failures fall back to MAKO Scaler.

### Performance and device selection

| Control | What it does |
| --- | --- |
| **Ultra Performance (Restart)** | Uses 70% Flow Scale, the lighter FG model, and LS1 Performance when scaling is enabled. |
| **Flow Scale** | 25–100% motion-estimation resolution; lower saves GPU work at a quality cost. Ultra Performance fixes it at 70%. |
| **Lighter FG Model** | Reduces GPU work, with potentially more artifacts; forced on by Ultra Performance. |
| **Allow FP16 (Restart)** | Global precision: FP16 for supported Frame Generation models and MAKO Scaler; off uses FP32. LS1 always uses FP32. |
| **Lossless.dll Path (Restart)** | Global override; leave empty for automatic detection. |
| **GPU (Restart)** | Selects a GPU; multi-GPU Frame Generation is unsupported. |

### Qt Shaders and compatibility controls

Enable **Shaders** before launching to use bundled vkBasalt; more effects cost more GPU time.

- **Effects:** select effects in order; uncheck and recheck to move one last. **Off** clears the effect list. **HDR Look (SDR)** changes appearance without enabling HDR.
- **Sharpening:** Off, CAS, or DLS, with adjustable **Sharpness**. **DLS Denoise** limits sharpening of grain and noise.
- **Anti-aliasing:** Off, FXAA, or SMAA.

Edit advanced shader parameters in the file shown below these controls. MAKO keeps other settings; restart after manual edits.

**Add Custom Shader** registers a vkBasalt-compatible ReShade `.fx` file; enable it in **Effects**. Keep includes and textures accessible to Flatpak games. **Delete selected custom shaders** removes profile registrations, preserving original files. **Refresh** reloads external edits; failed loads keep the previous chain active.

Under **Compatibility**, keep defaults unless a game needs a change:

- **Auto-disable Frame Generation by Refresh Rate / Refresh Rate Threshold:** pauses generation and Adaptive caps at or below the chosen Gamescope refresh; a manual Base FPS Cap remains.
- **Dynamic Cadence Recovery:** rechecks games that change native FPS; clears base caps and resets Real Frame Priority. Enabling a cap or changing priority turns Recovery off.
- **Cadence Probe Interval:** 0.1–3 seconds between recovery checks; default 2 seconds. Shorter intervals react sooner but can hitch more often.
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

**Interface Settings > Language** changes the Qt interface language. The first launch follows your system language, with English as the fallback.

## Launch Settings

For native Steam and Proton games, copy the UI's complete launch option. Without shaders or an explicit profile, the user-local archive uses:

```text
~/.local/bin/mako-launch %command%
```

The Arch package uses `/usr/bin/mako-launch`. For a terminal launch, replace `%command%` with the executable and arguments. Flatpak applications need [Flatpak preparation](FLATPAK-GUIDE.md).

The controls under **Launch Settings** apply globally to `mako-launch` games. **Disable Steam Overlay (Restart)** is on by default; it may reduce stutter but disables Steam's in-game overlay and FPS counter and may affect Steam Input. Gaming Mode menus and performance overlay remain available.

**Zink** runs OpenGL through Vulkan. **Force ALSA** changes the audio path for compatibility problems. Steam Deck mode, Gamescope WSI, and MangoHud controls are available in MAKO Decky.

## Advanced configuration

For manual TOML fields, defaults, and environment variables, see the [advanced reference](CONFIGURATION-REFERENCE.md). For problems, see [troubleshooting](TROUBLESHOOTING.md) or [collect diagnostics](COLLECT_DIAGNOSTICS.md).

## HDR through the isolated Gamescope bridge

Keep **Gamescope WSI** off, turn **Disable HDR (Restart)** off, restart the game, then enable HDR in the game. Use Gaming Mode with the Steam Deck OLED’s built-in screen or an HDR-capable external display, with HDR allowed in Gamescope. Both use the same bridge and toggle; no dock or charger is required. The Steam Deck LCD’s built-in screen remains SDR. Restart the game after switching between HDR and SDR displays so the bridge detects the new output. Decky stores the toggle in the game profile; Qt’s standalone launch control stores it globally in `launcher.conf`. Turning **Disable HDR** on restores the SDR launch on the next restart. Its setting does not depend on the Scaling, Frame Generation, or Shaders switches. The same bridge serves all combinations; HDR disabled means no HDR capability query, format exposure, or metadata forwarding from that bridge. See [HDR pipeline](HDR-PIPELINE.md) for colour handling and the testing boundary.

**Reduced HDR Precision** appears below the HDR switch while HDR is allowed. It is experimental and off by default. With HDR10, it prioritizes speed by interpolating directly in PQ colour values, removing Frame Generation’s linear colour conversions and full-resolution linear working images. Shader intermediates and scaling inputs also use compact 10-bit buffers where supported. HDR output, game resolution, Flow Scale and model choice stay unchanged. The approximation can add banding or artifacts around moving bright edges and change shader effects. Switching applies live with a brief hitch; off restores the full linear path. Linear scRGB Frame Generation and SDR retain their existing processing. CAS alone in HDR10 already works directly between packed images, so shader-only gains may be small. Compare the same scene and settings; a lower backend GPU cost does not guarantee the same percentage increase in game FPS.
