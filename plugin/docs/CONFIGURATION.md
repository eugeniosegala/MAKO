# Configuration guide

MAKO Decky saves settings automatically. Options marked **Restart** apply the next time the game starts; other supported changes apply while the game is running. **Live Status** shows the profile and settings currently applied, including pending restarts and fallbacks.

## Quick start

1. Add `/home/deck/.local/bin/mako-run %command%` to the game's Steam launch options. See [launcher setup](LAUNCHERS.md) for Heroic, Lutris, EmuDeck, and Flatpak applications.
2. Start the game, open MAKO Decky, and select **Save profile for &lt;game&gt;** after gameplay loads.
3. Choose Frame Generation, Scaling, and/or Shaders from the **Image Processing** tabs.
4. Restart the game after changing any option marked **Restart**.
5. Reopen MAKO Decky and check **Live Status** to confirm what is active.

Test one feature at a time at first. If Frame Generation, Scaling, or Shaders do not work, try Fullscreen, Borderless Fullscreen, and Windowed; support varies by game. A display-mode change can also change the resolution MAKO processes. Try the game's V-Sync setting both on and off and keep whichever feels smoother for that game.

## Lossless Scaling availability

**MAKO Scaler and Shaders work without Lossless Scaling; only Frame Generation and LS1 require it.** All profile controls remain editable and save normally when the DLL is missing. For a setup without it, turn Frame Generation off and choose **MAKO Scaler** and/or **Shaders**. The Decky panel shows an availability warning. To use Frame Generation or LS1, install Lossless Scaling or correct its DLL path, then restart the game.

## Profiles

The **Default** profile applies when no saved game or process profile matches. Saving a game profile records its Steam app ID and safe process names so MAKO can select it automatically on later launches.

The profile dropdown chooses which profile you are editing; it does not force that profile onto the running game. Use **Matched Processes** only when a launcher, emulator, or unusual game executable needs an additional match.

### AC and battery settings

Enable **Separate power settings** for the selected profile, then use **Editing settings for** to configure **Handheld (Battery)** and **Docked (AC Power)** independently. Both start as copies of the existing Renderer settings. **Base settings** remain editable and are used when power detection is unavailable at startup; turning separate settings off restores that set.

MAKO Renderer selects the set from Linux system power supplies at launch and checks for changes at most once every two seconds during presentation. AC power selects Docked, including a charger without a physical dock; battery operation selects Handheld. An external display alone does not select Docked. Switching works with the Decky panel and Qt window closed. A transient power-read failure retains the running Renderer's last confirmed source.

Frame Generation, Scaling, GPU selection, and Renderer performance settings can differ. Process matching, DLL path, Allow FP16, Shaders, and launcher compatibility settings remain shared. Existing live, recreation, and restart rules still apply; plugging in cannot enable resources that were not provisioned at game startup. Check **Live Status** for pending changes. The panel follows the active power set when the source changes during a game; outside a game, select either set to prepare it in advance.

Decky and the standalone Qt UI use the same optional tables in `conf.toml`; use current matching MAKO Renderer and MAKO Decky versions. Older editors do not understand the tables and may remove them when saving. A Flatpak sandbox that cannot read system power supplies uses Base settings at startup. Shader configurations and launch options remain per profile.

## Native Steam Remote Play

**Remote Play** is an opt-in integration for Steam's native **Stream** action on the receiving x86_64 Linux device. With MAKO Renderer installed and streams closed, select **Override Remote Play**. This replaces Steam's `ubuntu12_64/streaming_client` entry point with a self-contained Python 3.11+ wrapper and retains the original executable and its SHA-256 beside it. It applies to all native Steam streams, independently of the profile currently being edited. Standalone Steam Link, Flatpak Steam, and browser streaming are outside this integration.

The matching Qt UI can also enable, edit, refresh, or restore this same override through the shared installation owner. Edit the shared default configuration in one UI at a time; an alternate Qt configuration remains managed in Qt and is reported as a conflict in Decky. See [Remote Play](REMOTE-PLAY.md) for the launcher and recovery contract.

The first installation creates a **Remote Play** process profile with Fixed 2x Frame Generation, a 30 FPS base cap, and a 60 FPS target. Existing settings are preserved on reinstall. Edit this profile before streaming, or let Decky select it automatically when the native client starts, including sessions without a Steam AppID. Supported live settings and optional AC/battery tables use the normal Renderer behavior; restart-only settings require closing and restarting the stream. MAKO Scaler and Shaders can also use the normal launch path without Lossless Scaling; Frame Generation and LS1 require it. The active indicator matches an active Frame Generation runtime context to the exact client PID; it does not measure visible interpolation or latency.

Close streams and select **Remove Remote Play Override** to restore the verified original. The profile remains available. If Steam has already updated the native client, removal retires the stale backup while preserving Steam's replacement; enable the override again afterward. Removal refuses altered wrappers or corrupt backups and reports the conflict instead of overwriting them. Existing proof-of-concept wrappers or the experimental Remote Play Vulkan manifest must be removed using their original tools before enabling this integration.

Decky's Renderer and plugin uninstall paths attempt restoration before removing Renderer files. Remove the override through Decky before standalone uninstall or configuration purge. If its configuration, runner, or launch lock disappears, the wrapper launches the checksum-verified original with MAKO disabled; a missing or corrupt original stops the launch. This provides launch-preparation recovery, not recovery after execution has transferred to `mako-run` or the native client. See the [implementation and review notes](REMOTE-PLAY.md) for the boundaries and remaining hardware validation.

## Frame Generation

Turn on **Enable Frame-gen (Restart)** before starting the game.

**Real FPS** counts game-rendered frames; **output FPS** also includes generated frames. Fixed 2x can request 120 output FPS from 60 real FPS if the Renderer and display keep up. **Target FPS** is a desired output rate, not a guarantee of game FPS or physical display scanout.

- **Fixed** requests the selected 2x–5x total output ratio. Start with 2x. Select `0x` to pause generation without losing the saved multiplier.
- **Adaptive** varies generation toward **Target FPS** without slowing a game already above the target. **Maximum Multiplier** is a ceiling, not a fixed ratio; MAKO may use less or miss a target the game and GPU cannot sustain.
- **Match Display Refresh Rate** sits directly below Target FPS and is off by default. When enabled, Adaptive follows Gamescope's current display refresh. The manual slider becomes a disabled **Fallback Target FPS**; turn matching off to edit it. Unavailable refresh feedback uses that saved value. Editor cap estimates use the fallback; **Live Status** shows the applied target. The same behavior applies in Qt.
- **Steady Base Cap** starts by limiting real FPS to half the target, such as 60 real FPS for a 120 FPS target, to favour an even cadence. With Smooth Cadence, it can align a validated higher integer ratio. The cap may reduce responsiveness.
- **Fractional Adaptive** allows a changing mix of real and generated frames. It can retain more real frames and feel more responsive than Steady Base Cap, but may feel less even, especially on some VRR setups.
- **Real Frame Priority** sets a target-relative real-frame ceiling in Fractional mode. At a 120 FPS target, Low, Medium, High, and Very High correspond to 72, 80, 90, and 96 real FPS caps. Higher priority permits more real frames but does not make a game deliver them. Automatic keeps the normal Fractional policy.
- **Base FPS Cap** manually limits real FPS; Off adds no MAKO real-frame cap. Steady Base Cap or an explicit Fractional Real Frame Priority takes precedence while active. Changing this cap turns Dynamic Cadence Recovery off.
- **Smooth Cadence** favours consistent delivery and can let eligible ordered Gamescope presentation pace the game. It may lower real FPS and responsiveness, so compare both settings in the affected game.
- **Gamescope VRR** follows Steam by default. On or Off temporarily requests live VRR for this game's MAKO Frame Generation session only when Gamescope reports support on the active display and `gamescopectl`, `xprop`, and the systemd user service are available. Unsupported or unknown displays leave the option saved but the live setting untouched. MAKO Decky also discovers live Flatpak Renderer sessions from the host; this requires the matching current Renderer helper and does not broaden the sandbox’s permissions. MAKO restores the previous state afterward unless Steam or the user changed it during play. Gamescope's VRR state is session-wide; this option does not set FPS or the display refresh rate.
- **Auto-disable by Refresh Rate** pauses generation at or below the selected Gamescope refresh threshold. It has no effect without confirmed refresh feedback.

Most Frame Generation controls apply live after Frame Generation was enabled at startup. Changes that require a larger private resource set may cause a brief hitch or wait for a game recreation.

## Spatial Scaling

Turn on **Enable Scaling (Restart)** before starting the game. Gamescope/Game Mode is recommended because it provides a reliable display target.

| Method | Use it for | Requirement |
| --- | --- | --- |
| **Native Resolution** | Simple model-free reconstruction | None |
| **MAKO Scaler** | Open scaling with anti-ringing and sharpening | None |
| **LS1 Quality** | Highest-quality LS1 reconstruction | Licensed `Lossless.dll` |
| **LS1 Performance** | Lower-cost LS1 reconstruction | Licensed `Lossless.dll` |

Set Steam's **Game Resolution** to the display maximum, then choose a lower resolution inside the game. Test the game's display modes and in-game resolutions until **Live Status** shows **Input** smaller than **Display**. The in-game resolution alone does not prove that MAKO receives a lower-resolution image.

- **Scale Factor** targets output width and height from 1.0x to 2.0x the game render size. On fixed-size surfaces, MAKO requests a smaller render size to keep output fixed; on variable-size surfaces, it enlarges the game's requested image within display and GPU limits.
- **Sharpness** controls reconstruction sharpening and is hidden for Native Resolution.
- **Quality Supersampling** can improve quality on supported Gamescope surfaces but increases GPU and memory use.

Method and sharpness changes apply live. Factor or supersampling changes may wait for a resolution change, swapchain recreation, or restart. If LS1 cannot load, MAKO safely uses MAKO Scaler and reports the fallback without changing the saved method.

## Shaders

Turn on **Enable Shaders (Restart)** to use MAKO's private bundled vkBasalt build. No separate vkBasalt installation is needed.

- **Effects** is a multi-selection list. Effects run in the displayed order; unchecking and rechecking an effect moves it to the end. **Clear all** removes the chain.
- **Sharpening** offers CAS or DLS; **Sharpness** adjusts its strength, and **DLS Denoise** appears only with DLS. **Anti-aliasing** offers lighter, softer FXAA or more selective SMAA.
- Sharpening, sharpness, DLS denoise, anti-aliasing, and bundled or custom effect selections apply live after Shaders was enabled at startup. Adding, removing, or reordering selected effects rebuilds the chain and may cause a brief hitch.
- Combining several effects increases GPU cost.
- **HDR Look (SDR)** adjusts contrast and colour but remains SDR; it does not enable HDR output or increase display luminance.

The note below the controls shows the active profile configuration file. Advanced users can edit that file in Desktop Mode or over SSH. MAKO updates only the settings represented in its UI and preserves other options, comments, and lines. Avoid editing the same profile in MAKO Decky and the Qt configuration window simultaneously.

### Custom shaders

Select **Add Custom Shader** to choose a local vkBasalt-compatible ReShade `.fx` file, then enable its **Custom:** entry in **Effects**. Adding a file only registers it for the selected profile; it does not enable it. Custom and bundled effects share the same ordered list. Unchecking a custom effect or using **Clear all** retains its definition and options so it can be selected again.

Select **Delete selected custom shaders** to remove the selected custom definitions from this profile and deselect them. The list updates immediately; bundled effects and unselected custom definitions remain available. The original `.fx` files, includes, textures, and advanced options stay on disk, and other profiles keep their own registrations. The button is disabled when no custom effect is selected. Deletion removes the selected custom effects from the running chain when Shaders was enabled at startup.

MAKO references the original file instead of copying it. Keep its include files and textures available, and configure `reshadeIncludePath`, `reshadeTexturePath`, and shader-specific options in the displayed profile file when required. A shader designed for another ReShade runtime may use unsupported features. Flatpak games must be able to read the shader folder and its dependencies inside their sandbox.

Existing `.fx` assignments in the profile file appear automatically when the profile loads. Before the first Effects edit, the file’s `effects` chain also supplies their initial selection. After that, choose activation and order in the UI. Use **Refresh** after editing the file externally. For example:

```ini
effects = MyTone:makoVibrance
MyTone = "/home/deck/shaders/MyTone.fx"
```

Use aliases beginning with an ASCII letter and containing letters, digits, or underscores, up to 128 characters; aliases are case-sensitive. Bundled aliases such as `makoVibrance`, `cas`, and `fxaa` remain reserved. MAKO preserves existing advanced custom chains until the first explicit **Effects** edit, then manages the discovered custom selections together with bundled effects. Other advanced settings remain intact. A selected shader whose definition was removed is shown as missing and can be unchecked. Selections, order, deletion, and configuration-file path or option edits apply live while Shaders is active. A missing file or compilation error keeps the previous chain running; correct the problem and save the configuration again to retry. Editing a `.fx` file or its dependencies alone does not trigger reload; uncheck and recheck the effect to rebuild it.

## Performance settings

- **Ultra Performance (Restart)** selects a lighter preset: 70% Flow Scale, the lighter FG model and LS1 Performance when Scaling is enabled. The global precision toggle remains authoritative.
- **Flow Scale** trades motion-estimation quality for GPU cost.
- **Lighter FG Model** reduces GPU work but may show more artifacts.
- **Allow FP16 (Restart)** selects FP16 LSFG model and MAKO Scaler colour arithmetic when on and FP32 when off, across all profiles and presets. Ordinary Renderer builds warn when native FP16 LSFG is unavailable. The separately selected [experimental Renderer build](../../engine/docs/BUILDING-FROM-SOURCE.md#experimental-forced-lsfg-fp16) uses this same toggle to force conversion of a supported FP32-only LSFG model; its image quality is unqualified. Native FP16 takes priority, and turning this toggle off always selects FP32. LS1 Quality and LS1 Performance always use FP32 independently of this toggle. If LS1 fails, its MAKO Scaler fallback follows the toggle. Restart the game after changing it.
- **Lossless.dll Path (Restart)** is an optional override; leave it empty for automatic Steam-library detection.
- **GPU (Restart)** selects a GPU on multi-GPU systems. Multi-GPU Frame Generation is unsupported.

## Compatibility and external tools

Leave compatibility options at their defaults unless a game needs them.

- **Dynamic Cadence Recovery** helps games and emulators that switch between rates, such as 30 FPS gameplay and 60 FPS menus. It periodically checks native cadence and clears real-frame caps while enabled.
- **Gamescope WSI (Restart)** is an optional compatibility path for coloured or pixelated motion artifacts in supported 64-bit Gamescope launches.
- **Game Swapchain Images (Restart)** may help titles that fail to start with MAKO's normal generated-output headroom, but can reduce generated-frame availability.
- **Disable MAKO Renderer on Next Launch** temporarily bypasses the Renderer for troubleshooting.
- **Disable Steam Deck Mode** may expose settings hidden by a game's handheld mode. **Zink** routes OpenGL through Vulkan; **Force ALSA** changes the audio path and may help a game affected by Zink or audio stutter. These per-game compatibility switches require a restart.
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
