# Configuration guide

MAKO Decky saves automatically. Options marked **Restart** need a game restart; **Live Status** shows what is active and any pending changes.

## Quick start

1. Add `/home/deck/.local/bin/mako-run %command%` to the game's Steam launch options. For other launchers, follow [launcher setup](LAUNCHERS.md).
2. Start the game and select **Save profile for &lt;game&gt;** after gameplay loads.
3. Choose Frame Generation, Scaling, and/or Shaders under **Image Processing**.
4. Restart after changing a **Restart** option, then check **Live Status**.

Try one feature at a time. If it does not work, try Fullscreen, Borderless Fullscreen, and Windowed. Compare V-Sync on and off and keep the smoother option.

## Lossless Scaling availability

**MAKO Scaler and Shaders work without Lossless Scaling.** Frame Generation and LS1 require it. If MAKO reports it missing, install Lossless Scaling or correct its DLL path, then restart the game.

## Profiles

**Default** applies when no saved profile matches. **Save profile for &lt;game&gt;** enables automatic selection on later launches; wait until gameplay loads so MAKO captures the game rather than its launcher.

The dropdown selects the profile you edit; local games use their matching profile. **Matched Processes** adds executable matches for unusual games or emulators. Edit shared profiles in one UI at a time.

### AC and battery settings

Enable **Separate power settings**, then use **Editing settings for** to configure **Handheld (Battery)** and **Docked (AC Power)**. Connecting a charger selects AC settings, even without a dock; an external display alone does not.

**Base settings** apply when power detection is unavailable at startup or separate settings are disabled. Shaders, process matching, DLL path, FP16, and launcher settings stay shared. Power changes follow the usual live/restart rules; check **Live Status** for pending changes.

## Native Steam Remote Play

On the receiving device, close streams and select **Override Remote Play**. Choose a saved profile before streaming. See the [Remote Play guide](REMOTE-PLAY.md) for setup, profile creation, and restoration.

## Frame Generation

Turn on **Enable Frame-gen (Restart)** before launching. **Real FPS** counts game frames; output FPS includes generated frames. Targets depend on the game, GPU, and display.

| Control | What it does |
| --- | --- |
| **Fixed** | Requests 2x–5x output. Start at 2x; `0x` pauses generation. |
| **Adaptive** | Varies generation toward **Target FPS**, up to **Maximum Multiplier**. |
| **Match Display Refresh Rate** | Uses Gamescope's display refresh as the Adaptive target; **Fallback Target FPS** applies when refresh feedback is unavailable. |
| **Steady Base Cap** | Favours even cadence by initially capping real FPS at half the target; may reduce responsiveness. |
| **Fractional Adaptive** | Keeps more real frames through a changing mix of real and generated frames; may feel less even. |
| **Real Frame Priority** | Higher settings allow more real FPS in Fractional mode; **Automatic** uses the normal policy. |
| **Base FPS Cap** | Limits real FPS. **Off** adds no manual cap; Steady or explicit Fractional caps take precedence. |
| **Smooth Cadence** | Favours consistent delivery; compare on and off for responsiveness. |
| **Gamescope VRR** | Follows Steam, or temporarily requests VRR on/off for a supported display during play. |
| **Auto-disable by Refresh Rate** | Pauses generation at or below the selected Gamescope refresh threshold. Adaptive caps pause too; a manual Base FPS Cap remains. |

## Spatial Scaling

Turn on **Enable Scaling (Restart)**. Gamescope/Game Mode is recommended. Set Steam's **Game Resolution** to the display maximum, then lower the in-game resolution until **Live Status** shows **Input** smaller than **Display**.

| Method | Use it for |
| --- | --- |
| **Native Resolution** | Simple model-free reconstruction |
| **MAKO Scaler** | Open scaling with anti-ringing and sharpening |
| **LS1 Quality** | Highest-quality LS1 scaling; requires Lossless Scaling |
| **LS1 Performance** | Lower-cost LS1 scaling; requires Lossless Scaling |

- **Scale Factor:** targets 1.0x–2.0x output dimensions.
- **Sharpness:** adjusts sharpening; hidden for Native Resolution.
- **Quality Supersampling:** can improve quality on supported Gamescope surfaces, with higher GPU and memory use.

If LS1 cannot load, MAKO uses MAKO Scaler and shows the fallback in **Live Status**.

## Shaders

Turn on **Enable Shaders (Restart)** to use bundled vkBasalt; no separate installation is needed.

- **Effects:** select effects in their displayed order. Uncheck and recheck to move one last; **Clear all** removes the chain.
- **Sharpening:** choose CAS or DLS and adjust **Sharpness**; DLS also offers **Denoise**.
- **Anti-aliasing:** choose FXAA or SMAA.
- More effects increase GPU cost. **HDR Look (SDR)** changes appearance without enabling HDR.

### Custom shaders

Select **Add Custom Shader**, choose a vkBasalt-compatible ReShade `.fx` file, then enable its **Custom:** entry in **Effects**. Keep its includes and textures available; Flatpak games need access to the shader folder.

Unchecking a shader retains it for reuse. **Delete selected custom shaders** removes its registration from this profile and deselects it; the original files stay on disk. Use **Refresh** after editing shader files externally. If a shader fails to load, the previous chain stays active.

## Performance settings

| Control | What it does |
| --- | --- |
| **Disable Steam Overlay (Restart)** | On by default. May reduce stutter, but disables the in-game Steam overlay and FPS counter and may affect Steam Input. Gaming Mode menus and its performance overlay remain available. |
| **Ultra Performance (Restart)** | Uses 70% Flow Scale, the lighter FG model, and LS1 Performance when scaling is enabled. |
| **Flow Scale** | Lower values reduce GPU cost and motion-estimation quality. |
| **Lighter FG Model** | Reduces GPU work, with potentially more artifacts. |
| **Allow FP16 (Restart)** | Uses FP16 for supported Frame Generation models and MAKO Scaler; off uses FP32. LS1 always uses FP32. |
| **Lossless.dll Path (Restart)** | Optional override; leave empty for automatic detection. |
| **GPU (Restart)** | Selects a GPU; multi-GPU Frame Generation is unsupported. |

## Compatibility and external tools

Keep defaults unless a game needs a change.

| Control | Use it for |
| --- | --- |
| **Dynamic Cadence Recovery** | Games that change frame rate between scenes or menus; clears real-frame caps while enabled. |
| **Gamescope WSI (Restart)** | Coloured or pixelated motion artifacts in supported 64-bit Gamescope games. |
| **Game Swapchain Images (Restart)** | Games that fail to start with MAKO; may reduce generated-frame availability. |
| **Disable MAKO Renderer on Next Launch** | Temporarily bypassing the Renderer for troubleshooting. |
| **Disable Steam Deck Mode** | Exposing settings hidden by a game's handheld mode. |
| **Zink** | Running OpenGL through Vulkan. |
| **Force ALSA** | Audio or Zink compatibility problems. |
| **MangoHud (Restart)** | The host-installed overlay; cannot run alongside MAKO Shaders for the same profile. |

Frame Generation and Scaling are SDR-only. **Disable HDR** stays enabled and read-only.

## When changes apply

| Timing | Common settings |
| --- | --- |
| **Restart** | Enabling Frame Generation, Scaling, or Shaders; Ultra Performance; FP16; DLL path; GPU; launcher compatibility controls |
| **Live** | Most Frame Generation controls; scaling method and sharpness; shader selections and adjustments |
| **May wait for resolution change or restart** | Scale Factor, Quality Supersampling, and changes needing more Frame Generation capacity |

## Panel helpers

Press **R1** or **Hide info** to hide explanations; repeat to show them. **Advanced Details** shows installation information with copyable values.

For help, see [troubleshooting](TROUBLESHOOTING.md) or [collect diagnostics](COLLECT_DIAGNOSTICS.md). The [Renderer configuration reference](../../engine/docs/CONFIGURATION-REFERENCE.md) covers advanced settings.
