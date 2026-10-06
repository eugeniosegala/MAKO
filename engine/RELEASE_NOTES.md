## What's new in MAKO Renderer v4.1.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/maelstrom.png" alt="Maelstrom release artwork: a colossal mako above a spiraling sea" width="100%">

### Release codename: Maelstrom

> _“The storm took every bearing. The mako needed none.”_
>
> **Captain Matteo Veyr, _Chronicles of the Last Fleet_**

---

Maelstrom adds separate AC/battery profiles, display-aware Adaptive targets and custom shader controls, alongside steadier frame pacing and better non-Steam and Flatpak integration.

- **AC and battery profiles:** Save separate Frame Generation, Scaling and performance settings for battery and AC power in the configuration app. The Renderer switches automatically during play without keeping the app open. A charger counts as AC power; existing live-update and restart requirements still apply.
- **Match Display Refresh Rate:** Adaptive can optionally follow the current Gamescope refresh rate while preserving a manual fallback target; turn matching off to edit it. The effective target and caps stay current across refresh and live mode changes; Fixed mode retains its selected multiplier.
- **Custom shader controls:** Add compatible ReShade `.fx` files in the configuration app and combine them with bundled effects in one ordered list. Custom shader changes require a restart, and Flatpak games need access to the shader files and their dependencies.
- **Use MAKO without Lossless Scaling:** MAKO Renderer runs MAKO Scaler and Shaders without it. All profile controls remain editable; only Frame Generation and LS1 require Lossless Scaling.
- **Native Steam Remote Play (experimental):** Enable or restore native Steam streaming from the configuration app on the receiving x86_64 device. It shares the dedicated profile and verified Steam-client recovery with MAKO Decky, including normal live settings and optional AC/battery modes. Requires system Python 3.11 or newer; real stream quality and latency still need qualification.
- **Consistent precision controls:** Allow FP16 controls LSFG and MAKO Scaler colour arithmetic, including Ultra Performance. Turning it off selects FP32; LS1 Quality and Performance always use FP32. Shader effects retain their own precision behavior.
- **Lossless Scaling model compatibility:** Supported DirectX-based beta model layouts can run through the FP32 path. Standard builds report unavailable native FP16 explicitly; separately selected experimental source builds can convert supported FP32 models, with game image quality still unqualified. The default public Lossless Scaling branch remains recommended.

- **Adaptive stability:** Steady and Fractional Adaptive remember unsuccessful Smooth Cadence multiplier reductions through brief stalls and menu returns. Sustained delivery at a lower generated load can qualify a fresh comparison when the scene changes.
- **Frame pacing:** Capped Fractional output keeps its intended spacing, and ordered output retains GPU readiness time. Gamescope WSI timing respects applications that wait for frame presentation, while the private scaling bridge negotiates compatible completion pacing at startup.
- **Presentation recovery:** Swapchain retirement avoids blocking native fallback, and owned WSI timing feedback is drained in bounded batches.
- **Steam menus in non-Steam games:** Better shortcut and Heroic/UMU recognition lets Steam and Quick Access menus pause generated frames on supported Game Mode launches, across Fixed and Adaptive modes.
- **Flatpak VRR controls:** Standalone Flatpak games can use On/Off overrides during Frame Generation through the [host helper](https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/FLATPAK-GUIDE.md#gamescope-vrr-overrides), without broader sandbox permissions. Requires Gamescope and a VRR-capable display; MAKO Decky starts discovery automatically.
- **Shaders with Frame Generation:** The bundled vkBasalt checks its toggle key at most every 50 ms and keeps ReShade effect settings mapped, reducing CPU work on every generated and real frame. Effect GPU cost still scales with the multiplier.
- **Correct Steam launch options:** The configuration app uses the installed launcher path, including `/usr/bin/mako-launch` for Arch packages.
- **Diagnostics and profiling:** Opt-in scaling reports relate requested frame timing to compositor feedback. `mako-cli benchmark --profile` adds CPU submission/wait timings and GPU preprocessing/output spans to help investigate Frame Generation cost.
- **German configuration app:** A complete German interface with automatic locale detection. Thanks to [Hu2ki3](https://github.com/Hu2ki3) for [PR #77](https://github.com/eugeniosegala/MAKO/pull/77).
