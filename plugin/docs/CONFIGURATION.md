# Configuration guide

MAKO Decky saves automatically. Options marked **Restart** need a game restart; **Live Status** shows what is active and any pending changes.

Without live metrics, **Live Status** shows **Off** when Frame Generation and Scaling are both disabled in the current profile and power settings. Otherwise, it keeps the missing-metrics message. Reported live activity takes priority over saved settings that may still need a restart.

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

Choose the editing profile while idle; running games use their matching profile. **Create Profile** copies the selected settings without a running game. **Rename** and **Delete** manage saved profiles while idle; **Default** is protected. **Matched Processes** accepts comma-separated executable matches for unusual games or emulators. Edit shared profiles in one UI at a time.

### AC and battery settings

Enable **Separate power settings**, then use **Editing settings for** to configure **Handheld (Battery)** and **Docked (AC Power)**. Connecting a charger selects AC settings, even without a dock; an external display alone does not.

**Base settings** apply if startup power detection fails or separate settings are off. Shaders, process matching, DLL path, FP16, and launcher settings stay shared. Normal live/restart rules apply; **Live Status** shows pending changes.

## Native Steam Remote Play

On the receiving device, close streams and select **Override Remote Play**. Choose a saved profile before streaming. **Remove Remote Play Override** restores Steam. See the [Remote Play guide](REMOTE-PLAY.md) for setup, profile creation, and limits.

## Frame Generation

Turn on **Enable Frame-gen (Restart)** before launching. **Real FPS** counts game frames; output FPS includes generated frames. In Fixed or Adaptive mode, `0x` pauses generation and its FPS caps live; selecting 2x–5x resumes it.

| Control | What it does |
| --- | --- |
| **Fixed** | Requests 2x–5x output. Start at 2x. |
| **Adaptive** | Varies generation toward **Target FPS** (30–240), up to **Maximum Adaptive Multiplier** (2x–5x). |
| **Match Display Refresh Rate** | Uses Gamescope's refresh as the Adaptive target; **Fallback Target FPS** applies without feedback. Turn matching off to edit the target. |
| **Steady Base Cap** | Favours even cadence by initially capping real FPS at half the target; may reduce responsiveness. |
| **Fractional Adaptive** | Keeps more real frames through a changing mix of real and generated frames; may feel less even. |
| **Real Frame Priority** | Low–Very High select a Fractional real-FPS cap; higher priority allows more real FPS. **Automatic** adds no priority cap. |
| **Base FPS Cap** | Limits real FPS. **Off** adds no manual cap; Steady or explicit Fractional caps take precedence. |
| **Smooth Cadence** | Favours consistent delivery; compare on and off for responsiveness. |
| **Gamescope VRR** | Follows Steam, or temporarily requests VRR on/off for a supported display during play. |
| **Auto-disable by Refresh Rate / Refresh Rate Threshold** | Pauses generation and Adaptive caps at or below the chosen Gamescope refresh; a manual Base FPS Cap remains. |

## Spatial Scaling

Turn on **Enable Scaling (Restart)**. Gamescope/Game Mode is recommended. Set Steam's **Game Resolution** to the display maximum, then lower the in-game resolution until **Live Status** shows **Input** smaller than **Display**.

| Method | Use it for |
| --- | --- |
| **Native Resolution** | Simple model-free reconstruction |
| **MAKO Scaler** | Open scaling with anti-ringing and sharpening |
| **LS1 Quality** | Highest-quality LS1 scaling; requires Lossless Scaling |
| **LS1 Performance** | Lower-cost LS1 scaling; requires Lossless Scaling |

- **Scale Factor:** targets 1.0x–2.0x output dimensions, within display limits.
- **Sharpness:** adjusts sharpening; hidden for Native Resolution.
- **Quality Supersampling:** can improve quality on supported Gamescope surfaces, with higher GPU and memory use.

If LS1 cannot load, MAKO uses MAKO Scaler and shows the fallback in **Live Status**.

## Shaders

Turn on **Enable Shaders (Restart)** to use bundled vkBasalt; no separate installation is needed.

- **Effects:** select effects in their displayed order. Uncheck and recheck to move one last; **Clear all** removes the chain.
- **Sharpening:** Off, CAS, or DLS, with adjustable **Sharpness**. **DLS Denoise** limits sharpening of grain and noise.
- **Anti-aliasing:** Off, FXAA, or SMAA.
- More effects increase GPU cost. **HDR Look (SDR)** changes appearance without enabling HDR.

Edit advanced shader parameters in the file shown below these controls. MAKO keeps other settings; restart after manual edits.

### Custom shaders

**Add Custom Shader** registers a vkBasalt-compatible ReShade `.fx` file; enable its **Custom:** entry in **Effects**. Keep includes and textures accessible to Flatpak games.

Unchecking retains a shader for reuse. **Delete selected custom shaders** removes profile registrations and selections, preserving original files. **Refresh** reloads external edits; failed loads keep the previous chain active.

## Performance settings

| Control | What it does |
| --- | --- |
| **Disable Steam Overlay (Restart)** | On by default. May reduce stutter; disables Steam's in-game overlay and FPS counter and may affect Steam Input. Gaming Mode menus and performance overlay remain available. |
| **Ultra Performance (Restart)** | Uses 70% Flow Scale, the lighter FG model, and LS1 Performance when scaling is enabled. |
| **Flow Scale** | 25–100% motion-estimation resolution; lower saves GPU work at a quality cost. Ultra Performance fixes it at 70%. |
| **Lighter FG Model** | Reduces GPU work, with potentially more artifacts; forced on by Ultra Performance. |
| **Allow FP16 (Restart)** | Global precision: FP16 for supported Frame Generation models and MAKO Scaler; off uses FP32. LS1 always uses FP32. |
| **Lossless.dll Path (Restart)** | Global override; leave empty for automatic detection. |
| **GPU (Restart)** | Optional GPU name, vendor:device ID, or PCI bus ID; blank selects automatically. Multi-GPU Frame Generation is unsupported. |

## Compatibility and external tools

Keep defaults unless a game needs a change.

| Control | Use it for |
| --- | --- |
| **Dynamic Cadence Recovery** | Rechecks games that change native FPS; clears base caps and resets Real Frame Priority. Changing a cap or priority turns Recovery off. |
| **Cadence Probe Interval** | 0.1–3 seconds between recovery checks; default 2 seconds. Shorter intervals react sooner but can hitch more often. |
| **Gamescope WSI (Restart)** | Coloured or pixelated motion artifacts in supported 64-bit host Gamescope games. |
| **Game Swapchain Images (Restart)** | Games that fail to start with MAKO; may reduce generated-frame availability. |
| **Disable MAKO Renderer on Next Launch** | Temporarily bypassing the Renderer for troubleshooting. |
| **Disable Steam Deck Mode** | Exposing settings hidden by a game's handheld mode. |
| **Zink** | Running OpenGL through Vulkan. |
| **Force ALSA** | Audio or Zink compatibility problems. |
| **MangoHud (Restart)** | Host games only; requires installed MangoHud and cannot run alongside MAKO Shaders for the same profile. |

**Disable HDR (Restart)** is on by default. See [HDR setup](#hdr) to enable experimental HDR.

## When changes apply

| Timing | Common settings |
| --- | --- |
| **Restart** | Enabling Frame Generation, Scaling, or Shaders; Ultra Performance; FP16; DLL path; GPU; launcher compatibility controls |
| **Live** | Most Frame Generation controls; scaling method and sharpness; shader selections and adjustments |
| **May wait for resolution change or restart** | Scale Factor, Quality Supersampling, and changes needing more Frame Generation capacity |

## Panel helpers

MAKO Decky follows Steam's interface language. Press **R1** or **Hide info** to hide explanations; repeat to show them. **Advanced Details** shows installation information with copyable values.

For help, see [troubleshooting](TROUBLESHOOTING.md) or [collect diagnostics](COLLECT_DIAGNOSTICS.md). The [Renderer configuration reference](../../engine/docs/CONFIGURATION-REFERENCE.md) covers advanced settings.

## HDR

**HDR is experimental and disabled by default.** It works with Scaling, Frame Generation and Shaders in Gaming Mode on Steam Deck OLED or an HDR-capable external display, with HDR allowed in Gamescope. The Steam Deck LCD’s built-in display is SDR.

In the game profile, keep **Gamescope WSI** off and turn off **Disable HDR (Restart)**, restart the game, then enable HDR in its settings. Restart after changing MAKO’s HDR toggle or switching between HDR and SDR displays.

**Reduced HDR Precision** is on by default; saved choices are preserved. It can improve HDR10 performance at the cost of banding, bright-edge artifacts or altered shader effects. Turn it off for full precision. Changes apply live and may briefly hitch. See the [HDR pipeline](../../engine/docs/HDR-PIPELINE.md) for technical details.
