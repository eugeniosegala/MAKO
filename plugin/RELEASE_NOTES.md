## What's new in MAKO Decky v4.5.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/maelstrom.png" alt="Maelstrom release artwork: a colossal mako draws a storm-torn sea into the glowing whirlpool inside its jaws" width="100%">

### Release codename: Maelstrom

> _“I drew a course around the storm. By morning, the storm had drawn a course around us.”_
>
> **Mira Valen, _Charts of the Last Fleet_**

---

Maelstrom adds experimental HDR, AC/battery settings, display-aware Adaptive targets, custom shaders and native Steam Remote Play, with new Steam Overlay controls and broader Flatpak support.

- **Experimental HDR (disabled by default):** Turn off **Disable HDR (Restart)** in the game's profile to use HDR10/PQ or linear scRGB with Scaling, Frame Generation and Shaders. Restart the game after changing the toggle, then enable its HDR setting. Requires an HDR-capable display and HDR enabled in Gamescope. **Reduced HDR Precision** now also accelerates HDR10 Frame Generation by skipping linear colour conversions, alongside compact shader/scaling buffers. It applies live, defaults on for profiles without a saved choice, and trades accuracy for speed; bright-edge artifacts or banding are possible. Matching compact HDR10 shader chains also skip a redundant output copy.
- **AC and battery settings:** Enable separate Handheld and Docked settings for each profile, then edit either set or Base settings. The matching Renderer follows battery or AC power automatically while playing, even with the panel closed. Switching sets preserves shared shader edits and keeps native settings separate; normal restart requirements still apply.
- **Match Display Refresh Rate:** Let Adaptive follow Gamescope's current refresh rate instead of a fixed target. Your manual target is kept as the fallback; turn matching off to edit it. Live Status shows the effective target. The option can differ between AC and battery settings.
- **Custom shaders:** Add compatible ReShade `.fx` files, then select and order them alongside bundled effects. Selection, ordering and deletion apply live while Shaders is active; **Refresh** reloads edited files and dependencies. Failed edits keep the previous chain running. The picker has more reliable paging, scrolling and controller focus. Flatpak games need access to the original files and dependencies.
- **Lossless Scaling is optional:** MAKO Scaler and Shaders work through the installed Renderer without it. All profile controls remain editable; only Frame Generation and LS1 require Lossless Scaling.
- **Clearer FP16 controls:** Allow FP16 applies to LSFG and MAKO Scaler across profiles and presets; LS1 always stays FP32, and shader effects keep their own precision behavior. Model warnings explain unavailable native FP16. Restart the game after changing precision.

- **Native Steam Remote Play:** Stream on the receiving native x86_64 Linux Steam client using your selected profile and optional AC/battery settings. Create a named copy during a stream; normal live/restart rules apply. Qt and Decky share client restoration and current shader selections. Requires system Python 3.11 or newer; standalone Steam Link, Flatpak Steam and browser streaming are unsupported. Stream quality and latency still need qualification. Thanks to [Hugo Uchôas Borges](https://github.com/hugouchoasborges) for the contribution.
- **Disable Steam Overlay (Restart):** New per-profile Performance control, enabled by default. May reduce stutter; disables Steam's in-game overlay and FPS counter and may affect Steam Input. Gaming Mode menus and performance overlay remain available.
- **Better profile capture:** Launcher and helper processes cannot stand in for the game or select another game's profile through old aliases. Ubisoft Connect, CD Projekt RED, Rockstar helpers and EA app windows remain excluded while their games stay eligible.
- **Non-Steam menu handling:** Steam and Quick Access menus pause generated frames on supported non-Steam launches, including Heroic/UMU shortcuts. Works across Fixed and Adaptive modes, with or without Scaling.
- **Flatpak support:** Flatpak Setup now includes Freedesktop 26.08 extensions alongside 23.08–25.08. Automatic host discovery applies VRR On/Off requests to the game's verified Gamescope session during Frame Generation. Requires a VRR-capable display; Follow Steam and manual Steam changes retain their existing priority.
- **Adaptive stability:** Steam menu returns now preserve cooldowns for rejected multiplier increases, avoiding repeated costly retries. Failed Smooth Cadence reductions keep their retry delay across stalls and menu returns. Higher-multiplier pacing is rechecked against its real-frame cost. Brief native-only recovery samples restore the automatic base cap and generated workload before checking recovery.
- **Frame pacing and recovery:** The matching Renderer improves capped Fractional spacing, completion pacing and recovery from retired Gamescope scaling swapchains, while keeping native fallback responsive. The Gamescope bridge now accounts for slower GPU-bound source batches and their readiness margin, reducing dropped generated frames even when accepted FPS appears healthy. Extra readiness applies only while Frame Generation is enabled.
- **Shader overhead:** The bundled vkBasalt reduces per-frame CPU work and preserves ReShade effect settings. Effect GPU cost still increases with the Frame Generation multiplier.
- **Clearer guides and reports:** Setup, configuration, Remote Play, Flatpak and troubleshooting guides are shorter. Diagnostic instructions now request 5,000 lines; reports retain startup, state and process/GPU health context.
- **German interface:** MAKO Decky now follows Steam's German interface language with a complete translated catalog. Thanks to [Hu2ki3](https://github.com/Hu2ki3) for contributing German support to the Renderer configuration app in [PR #77](https://github.com/eugeniosegala/MAKO/pull/77).
- **Russian interface:** MAKO Decky follows Steam's Russian language setting with translations for the current HDR, power-profile, custom-shader and Remote Play controls. Thanks to [rosakodu](https://github.com/rosakodu) and the Decky Loader Russia community for [PR #83](https://github.com/eugeniosegala/MAKO/pull/83).
- **Japanese translations:** Clearer shader controls, installation messages, and explanations. Thanks to [Tak-attack](https://github.com/Tak-attack) for [PR #71](https://github.com/eugeniosegala/MAKO/pull/71).
