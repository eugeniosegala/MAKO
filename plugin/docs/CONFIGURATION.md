# Configuration guide

MAKO Decky saves automatically. Options marked **Restart** need a game restart; **Live Status** shows what is active and any pending changes.

Feature settings require an installed MAKO Renderer. An update notice keeps Frame Generation, Scaling, and Shaders editable; install the bundled update before relying on its new features. Checked toggles show saved settings; **Live Status** reports the running Renderer.

Without live metrics, **Live Status** shows **Off** when Frame Generation and Scaling are both disabled in the current profile and power settings. Otherwise, it keeps the missing-metrics message. Reported live activity takes priority over saved settings that may still need a restart.

## Quick start

1. Add `/home/deck/.local/bin/mako-run %command%` to the game's Steam launch options. For other launchers, follow [launcher setup](LAUNCHERS.md).
2. Start the game and select **Save profile for &lt;game&gt;** after gameplay loads.
3. Use the **Frame-gen**, **Scaling**, and/or **Shaders** tabs to choose features.
4. Restart after changing a **Restart** option, then check **Live Status**.

Use **Performance** (shown as **Perf** on focus), **Advanced**, and **Tweaks** for additional controls; Tweaks contains compatibility options. **External Tools** and **Manual Overrides** are below the launch instructions.

Use Left/Right to move across tabs and Up/Down between rows. Press A to open the focused tab. MAKO remembers the last tab you opened.

Try one feature at a time. If it does not work, try Fullscreen, Borderless Fullscreen, and Windowed. Compare V-Sync on and off and keep the smoother option.

## Lossless Scaling availability

**MAKO FG, MAKO Scaler and Shaders work without Lossless Scaling.** Only the Lossless Scaling frame generation model and LS1 require it. Once MAKO Renderer is installed, the editor stays available without Lossless Scaling; unavailable LSFG and LS1 choices are grayed out.

If `Lossless.dll` is missing, MAKO warns and keeps your saved selection. The Renderer falls back to MAKO FG or MAKO Scaler if the selected model cannot initialize. If the DLL is present but fails its check, the editor saves the MAKO alternative for the profile and power mode being edited. Restart a running game to apply an FG model change.

## Profiles

**Default** applies when no saved profile matches. **Save profile for &lt;game&gt;** enables automatic selection on later launches; wait until gameplay loads so MAKO captures the game rather than its launcher.

Saving a new game profile during gameplay keeps shader selections and adjustments live when Shaders was enabled before launch. Options marked **Restart** still require a game restart.

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

The refresh-rate threshold reads display Hz, not the game's real FPS. A 50 Hz threshold allows generation on a 60 Hz display throughout both 30 FPS cutscenes and 60 FPS gameplay. Fractional Adaptive separately prefers native presentation near its output target.

With **Fractional Adaptive** and **Automatic** Real Frame Priority, sustained native FPS can exceed **Target FPS** without generation when **Base FPS Cap** is off or at least the target. For example, a 60 FPS target on a 90 Hz display allows native 75 FPS gameplay with the cap off or set to 90; the manual cap still limits faster sources. Smooth Cadence can be on or off; below-target caps, explicit priorities and Steady keep their selected pacing.

For games switching between 30 FPS cutscenes and 60 FPS gameplay on a 60 Hz display, try **Fractional Adaptive**, a **2x** ceiling, and a **60 FPS** target (or **Match Display Refresh Rate**). Enable **Dynamic Cadence Recovery** under **Tweaks**. Recovery clears base caps and selects **Automatic** Real Frame Priority; keep those settings to let gameplay reach its native rate. Start with the default two-second probe interval; checks can briefly lower output FPS or hitch.

### Proton offscreen windows

Some games on Proton 9, such as Half-Life 2, draw offscreen in a way that hides generated frames. When **Live Status** asks you to restart, leave **Tweaks > Proton Offscreen Window Fix** enabled (the default) and restart the game. MAKO applies the fix to eligible games on their next launch; Frame Generation stays off for the current launch.

On Proton 7 or 8, MAKO cannot detect this redirection: **Live Status** can report Frame Generation as active while generated frames remain hidden. Try Proton 10 or newer. Proton 8 also supports the [manual launch-option fix](../../engine/docs/CONFIGURATION.md#proton-offscreen-windows); Proton 7 does not.

If **Live Status** reports that the fix is unavailable or ineffective, try another Proton version. Turn the setting off if it causes display problems. See [Wine offscreen windows](../../engine/docs/WSI-ISOLATION.md#wine-offscreen-windows) for detection and fallback details.

## GPU Headroom

Under **Performance**, enable **GPU Headroom** and choose a 5%, 10%, 15% or 20% reserve. It defaults to Off; enabling it selects 10%, targeting roughly 90% utilization of the game's GPU, including the compositor and other applications. The toggle and reserve apply live while Frame Generation is active. Off removes its additional cap and stops its utilization sampling. This is a smoothed target: brief spikes can still reach 100%, and a lower utilization percentage does not guarantee lower power consumption.

MAKO gradually limits real frames while Frame Generation is active. **Adaptive** can replace them within **Maximum Adaptive Multiplier**; **Fixed** keeps its multiplier and can lower output FPS. Existing caps still apply. Fixed keeps its usual generated workload after Headroom releases its cap; Adaptive is preferable when you want to regain a higher real-frame rate as scenes become lighter. Adaptive at 90 FPS with a 2× ceiling cannot reduce real frames below 45 FPS; allowing 3× creates more room but may reduce responsiveness or increase interpolation artifacts. A 20 real-FPS safety floor also applies. Dynamic Cadence Recovery is disabled when enabling Headroom and clears Headroom when enabled itself.

Supported AMD GPU telemetry is required. Gamescope is optional, but the driver and sandbox must expose readings for the game's GPU. Unavailable readings leave normal pacing active; **Live Status** reports unavailable telemetry or a reached floor. The setting is dormant with Frame Generation off and does not independently regulate scaling or shaders. Each power profile can save its own reserve.

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

Turn on **Enable shaders (Restart)** to use bundled vkBasalt; no separate installation is needed.

- **Effects:** select effects in their displayed order. Uncheck and recheck to move one last; **Clear all** removes the chain.
- **Sharpening:** Off, CAS, or DLS, with adjustable **Sharpness**. **DLS denoise** limits sharpening of grain and noise.
- **Anti-aliasing:** Off, FXAA, or SMAA.
- More effects increase GPU cost. **HDR Look (SDR)** changes appearance without enabling HDR.

Select **Customize shaders** to adjust selected bundled and compatible custom effects with sliders, exact-value fields, toggles, choices, and colour controls. **Reset shader to defaults** resets only the selected effect, preserving values its author marked as non-resettable. The popup edits parameters; choose enabled effects in **Effects**.

Type a value beneath a slider for finer adjustments.

Palettes such as Nostalgia's **Custom palette** appear as colour swatches. Select a swatch to edit its RGB channels with sliders or exact values. Only the selected colour's controls are expanded; reset still applies to the whole effect. Colour controls require metadata from the matching Renderer helper; older helpers show numeric fields instead.

Adjustments save with the selected profile and are shared across AC and battery modes. Copies retain them; deleting the profile removes them. Unchecking a custom shader retains its values, while deleting its registration removes them. Changes apply live when Shaders was enabled at game launch. Rebuilding an effect may briefly hitch, and higher quality or iteration settings can increase GPU cost.

The popup sits on the left and closes the Quick Access side menus. Its header shows **Saving...** until pending adjustments are saved. **Hide controls** collapses it for a clearer view of the game while keeping the save status visible. Closing waits for pending saves; a failed save offers retry or discarding only the unsaved edits.

If the editor requests an update, install the matching Renderer. Custom effects must be vkBasalt-compatible; compile-time macros are not editable here. Reopen the editor after changing shader source or includes. See the [shader editor contract](../../engine/docs/LAYER-CHAINING.md#shader-parameter-editor-contract) for supported parameter types and implementation details.

### Crop & Stretch

Select **Crop & Stretch** in **Effects**, then open **Customize shaders**. **Left crop (%)**, **Right crop (%)**, **Top crop (%)**, and **Bottom crop (%)** independently remove part of that edge and stretch the remaining image to fill the output. All four default to zero. For example, Top and Bottom at 5 remove 5% from each edge and stretch the remaining 90% vertically; Left and Right at zero keep the horizontal framing. **Reset shader to defaults** restores the original framing.

The controls apply live through the normal per-profile shader editor. Each edge is limited to 49% so opposing edges cannot cross. Cropping affects the whole game image, including HUD elements and subtitles, and independent horizontal/vertical stretching changes aspect ratio. It can remove bars within the game's image, but cannot remove borders added later by Gamescope or the display. Place it before other effects when those effects should use the stretched image.

### Custom shaders

**Add custom shader** registers a vkBasalt-compatible ReShade `.fx` file; enable its **Custom:** entry in **Effects**. Keep includes and textures accessible to Flatpak games.

Unchecking retains a shader for reuse. **Delete selected custom shaders** removes profile registrations and selections, preserving original files. **Refresh** reloads external edits; failed loads keep the previous chain active.

## Performance settings

| Control | What it does |
| --- | --- |
| **Disable Steam Overlay (Restart)** | On by default. May reduce stutter; disables Steam's in-game overlay and FPS counter and may affect Steam Input. Gaming Mode menus and performance overlay remain available. |
| **Ultra Performance (Restart)** | Uses 60% Flow Scale with MAKO FG and 70% with LSFG, at least the Performance MAKO FG Mode, LSFG Performance for LSFG, and MAKO Scaler when scaling is enabled. |
| **Flow Scale** | 25–100% motion-estimation resolution; lower saves GPU work at a quality cost. Ultra Performance fixes it at 60% with MAKO FG and 70% with LSFG. |
| **GPU Headroom** | Live, Frame Generation-only control that reduces real FPS to target spare GPU capacity; see [GPU Headroom](#gpu-headroom). Off by default. |
| **Allow FP16 (Restart)** | Global precision: FP16 for supported Frame Generation models and MAKO Scaler; off uses FP32. LS1 always uses FP32. |
| **Lossless.dll Path (Restart)** | Optional global override for LSFG and LS1; leave empty for automatic detection. |
| **GPU (Restart)** | Optional GPU name, vendor:device ID, or PCI bus ID; blank selects automatically. Multi-GPU Frame Generation is unsupported. |

## Compatibility and external tools

Keep defaults unless a game needs a change.

| Control | Use it for |
| --- | --- |
| **Dynamic Cadence Recovery** | Off by default. Rechecks games that change native FPS by briefly reducing generated work; these checks can lower output FPS or cause hitches under sustained GPU load. Clears base caps and GPU Headroom, and resets Real Frame Priority. Changing a cap or priority, or enabling GPU Headroom, turns Recovery off. |
| **Cadence Probe Interval** | 0.1–3 seconds between recovery checks; default 2 seconds. Shorter intervals react sooner but can hitch more often. |
| **Gamescope WSI (Restart)** | Coloured or pixelated motion artifacts in supported 64-bit host Gamescope games. |
| **Game Swapchain Images (Restart)** | Games that fail to start with MAKO; may reduce generated-frame availability. |
| **Mailbox Presentation (Restart)** | Games that hang or fail with Frame Generation's FIFO presentation; generated frames can be replaced before they are shown, which may reduce smoothness. |
| **Proton Offscreen Window Fix (Restart)** | On by default. Applies the [offscreen-window fix](#proton-offscreen-windows) to eligible games after detection and one restart, only while **Enable Frame-gen** is on. Turn off to stop MAKO applying it for this profile. |
| **Disable MAKO Renderer on Next Launch** | Temporarily bypassing the Renderer for troubleshooting. |
| **Disable Steam Deck Mode** | Exposing settings hidden by a game's handheld mode. |
| **Zink** | Running OpenGL through Vulkan. |
| **Force ALSA** | Audio or Zink compatibility problems. |
| **MangoHud (Restart)** | Host games only; requires installed MangoHud and cannot run alongside MAKO Shaders for the same profile. |
| **Count Real Frames Only (Restart)** | Shown while MangoHud is enabled. Off counts every output frame, including generated ones; on shows the game's real FPS. See [MangoHud frame counting](../../engine/docs/LAYER-CHAINING.md#mangohud-frame-counting). |

For one MangoHud layout across profiles, create `~/.config/mako-render/mangohud.conf` and keep the normal `mako-run %command%` launch option. Profiles with MangoHud enabled select that file on the next supported host launch. See [shared MangoHud layouts](../../engine/docs/LAYER-CHAINING.md#shared-mangohud-layout) for an example, configuration precedence, and per-game exceptions.

**Enable HDR (Restart)** is off by default because HDR is experimental. See [HDR setup](#hdr) to opt in.

## When changes apply

| Timing | Common settings |
| --- | --- |
| **Restart** | Enabling Frame Generation, Scaling, or Shaders; switching between MAKO FG and LSFG; Ultra Performance; FP16; DLL path; GPU; launcher compatibility controls |
| **Live** | Most Frame Generation controls; switching between LSFG Quality and LSFG Performance; scaling method and sharpness; shader selections and adjustments |
| **May wait for resolution change or restart** | Scale Factor, Quality Supersampling, and changes needing more Frame Generation capacity |

## Panel helpers

MAKO Decky follows Steam's interface language. Press **R1** or **Hide info** to hide explanations and Experimental tags; repeat to show them. **Advanced Details** shows installation information with copyable values.

For help, see [troubleshooting](TROUBLESHOOTING.md) or [collect diagnostics](COLLECT_DIAGNOSTICS.md). The [Renderer configuration reference](../../engine/docs/CONFIGURATION-REFERENCE.md) covers advanced settings.

## HDR

**HDR is experimental and disabled by default.** It works with Scaling, Frame Generation and Shaders in Gaming Mode on Steam Deck OLED or an HDR-capable external display, with HDR allowed in Gamescope. The Steam Deck LCD’s built-in display is SDR.

In the game profile, keep **Gamescope WSI** off and turn on **Enable HDR (Restart)**, restart the game, then enable HDR in its settings. Restart after changing MAKO’s HDR toggle or switching between HDR and SDR displays.

**HDR Quality** defaults to **Performance**. **Quality** retains full-precision shader/scaling buffers; **Balanced** uses compact buffers. With LSFG, both retain linear-light generation and full motion analysis; **Performance** also uses approximate HDR10 generation and coarser motion analysis. MAKO FG uses its own HDR processing and selected Flow Scale. Output resolution stays unchanged. Changes apply live with a brief hitch; lower precision may add banding or motion artifacts. See the [HDR pipeline](../../engine/docs/HDR-PIPELINE.md) for details.

## Frame Generation Model

**MAKO FG Mode** appears under the model picker when MAKO FG is selected and applies live. At 2× and 3×, **Quality** solves motion for every real-frame pair, **Balanced** (default) one in two, **Performance** one in three and **Extra Performance** one in four. Reusing motion frees GPU time and can add artifacts; a game already at its frame-rate cap gains spare GPU time, not more frames. At 4× and above, MAKO FG solves every pair regardless of the selected mode. Ultra Performance offers only Performance and Extra Performance. See [MAKO FG Mode](../../engine/docs/MAKO-FG.md#mako-fg-mode).

Select **MAKO FG**, **LSFG Quality**, or **LSFG Performance** in the Frame Generation group. MAKO FG is the default unless the profile saves an LSFG model, including selections saved by older editors. The choice is saved per game and per power mode. Switching between MAKO FG and LSFG requires a game restart; switching between LSFG Quality and LSFG Performance applies live.

LSFG Performance lowers GPU cost but can increase ghosting. Ultra Performance forces it and hides LSFG Quality. LSFG Quality with FP16 has an extra speed optimization on AMD GPUs using Mesa's RADV driver; other GPUs and settings use the standard path. MAKO FG supports SDR, HDR, and Flow Scale; see [MAKO FG setup](../../engine/docs/MAKO-FG.md) for package requirements and limitations.
