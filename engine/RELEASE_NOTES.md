## What's new in MAKO Renderer v4.1.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/maelstrom.png" alt="Maelstrom release artwork: a colossal mako draws a storm-torn sea into the glowing whirlpool inside its jaws" width="100%">

### Release codename: Maelstrom

> _“The fleet braced for the wave. Then the wave opened its jaws.”_
>
> **Captain Matteo Veyr, _Chronicles of the Last Fleet_**

---

Maelstrom adds AC/battery profiles, display-aware Adaptive targets, custom shaders and native Steam Remote Play, with new Steam Overlay controls and further pacing, recovery and Flatpak improvements.

- **AC and battery profiles:** Save separate Frame Generation, Scaling and performance settings for battery and AC power. The Renderer switches automatically during play, without keeping the configuration app open. Charging selects AC settings; normal live/restart rules apply.
- **Match Display Refresh Rate:** Adaptive can follow Gamescope's refresh rate while preserving a manual fallback target. Targets and caps track refresh and live mode changes. Turn matching off to edit the target; Fixed mode keeps its selected multiplier.
- **Custom shader controls:** Combine compatible ReShade `.fx` files with bundled effects in one ordered list. Selection, ordering and deletion apply live while Shaders is active; **Refresh** reloads edited files and dependencies. Failed edits keep the previous chain running. Flatpak games need access to the files and dependencies.
- **Use MAKO without Lossless Scaling:** MAKO Renderer runs MAKO Scaler and Shaders without it. All profile controls remain editable; only Frame Generation and LS1 require Lossless Scaling.
- **Reusable profiles in both editors:** Create a named profile without a running game in Qt or MAKO Decky. Qt now copies the selected profile, including both power sets and custom shaders, saves the selection, protects Default, and reports invalid names or failed saves.
- **Native Steam Remote Play:** Enable native Steam streaming on the receiving x86_64 Linux device. Streams follow your selected profile, including Shaders, Scaling, Frame Generation and AC/battery settings, under normal live/restart rules. Steam-client restoration is shared with MAKO Decky. Requires system Python 3.11 or newer; stream quality and latency still need qualification.
- **Consistent precision controls:** Allow FP16 controls LSFG and MAKO Scaler colour arithmetic, including Ultra Performance. Turning it off selects FP32; LS1 Quality and Performance always use FP32. Shader effects retain their own precision behavior.
- **Lossless Scaling model compatibility:** Supported DirectX-based beta model layouts can run through the FP32 path. Standard builds report unavailable native FP16 explicitly; separately selected experimental source builds can convert supported FP32 models, with game image quality still unqualified. The default public Lossless Scaling branch remains recommended.

- **Disable Steam Overlay (Restart):** New global launcher control, enabled by default. May reduce stutter; disables Steam's in-game overlay and FPS counter and may affect Steam Input. Gaming Mode menus and performance overlay remain available.
- **Adaptive stability:** Failed Smooth Cadence reductions retain their retry delay through stalls and menu returns. Adaptive rechecks the real-frame cost after higher-multiplier pacing takes effect and steps back when extra output does not justify the loss. Native-only recovery sampling releases the automatic base cap briefly, then restores the cap and generated workload before checking recovery.
- **Frame pacing and recovery:** Capped Fractional output retains its spacing, and ordered output retains GPU readiness time. Gamescope WSI respects application presentation waits, and the private scaling bridge negotiates compatible completion pacing. Retired Gamescope scaling swapchains now request recreation; retirement keeps native fallback responsive, and timing feedback stays bounded.
- **Launcher profile matching:** Ubisoft Connect, CD Projekt RED, Rockstar helpers and EA app processes stay outside game activation and capture. Launcher aliases cannot select another game's profile; child games remain eligible.
- **Steam menus in non-Steam games:** Better shortcut and Heroic/UMU recognition lets Steam and Quick Access menus pause generated frames on supported Game Mode launches, across Fixed and Adaptive modes.
- **Flatpak support:** Runtime extensions now include Freedesktop 26.08 alongside 23.08–25.08. Standalone games can use VRR On/Off overrides through the [host helper](https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/FLATPAK-GUIDE.md#gamescope-vrr-overrides) during Frame Generation, without broader sandbox permissions. Requires Gamescope and a VRR-capable display; MAKO Decky starts discovery automatically.
- **Shaders with Frame Generation:** The bundled vkBasalt reduces per-frame CPU work while preserving ReShade settings. Effect GPU cost still scales with the multiplier. Native and Flatpak packages align Renderer and vkBasalt with the updated Vulkan build baseline.
- **Correct Steam launch options:** The configuration app uses the installed launcher path, including `/usr/bin/mako-launch` for Arch packages.
- **Diagnostics and profiling:** Opt-in reports add process/GPU health, scoped MAKO memory accounting and frame-phase timings, retaining earlier startup and state context beside recent logs. Scaling reports relate requested timing to compositor feedback; `mako-cli benchmark --profile` adds CPU submission/wait and GPU preprocessing/output timings.
- **Shorter guides:** Setup, configuration, Flatpak, Remote Play, troubleshooting and video instructions are more concise. Advanced TOML and environment settings have a separate reference.
- **German configuration app:** A complete German interface with automatic locale detection. Thanks to [Hu2ki3](https://github.com/Hu2ki3) for [PR #77](https://github.com/eugeniosegala/MAKO/pull/77).
