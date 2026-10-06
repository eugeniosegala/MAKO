## What's new in MAKO Decky v4.1.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/maelstrom.png" alt="Maelstrom release artwork: a colossal mako above a spiraling sea" width="100%">

### Release codename: Maelstrom

> _“We lashed the charts to the mast. The sea had begun rewriting them.”_
>
> **Mira Valen, _Charts of the Last Fleet_**

---

Maelstrom adds AC/battery settings, display-aware Adaptive targets and custom shaders, with clearer setup and steadier frame pacing through the matching MAKO Renderer update.

- **AC and battery settings:** Enable separate Handheld and Docked settings for each profile, then edit either set or Base settings. The matching Renderer follows battery or AC power automatically while playing, even with the panel closed. Switching sets preserves shared shader edits and keeps native settings separate; normal restart requirements still apply.
- **Match Display Refresh Rate:** Let Adaptive follow Gamescope's current refresh rate instead of a fixed target. Your manual target is kept as the fallback; turn matching off to edit it. Live Status shows the effective target. The option can differ between AC and battery settings.
- **Custom shaders:** Use **Add Custom Shader…** to register a compatible ReShade `.fx` file, then select and order it alongside bundled effects. Custom changes require a game restart; Flatpak games need access to the original files and dependencies.
- **Lossless Scaling is optional:** MAKO Scaler and Shaders work through the installed Renderer without it. All profile controls remain editable; only Frame Generation and LS1 require Lossless Scaling.
- **Clearer FP16 controls:** Allow FP16 applies to LSFG and MAKO Scaler across profiles and presets; LS1 always stays FP32, and shader effects keep their own precision behavior. Model warnings explain unavailable native FP16. Restart the game after changing precision.

- **Non-Steam menu handling:** Steam and Quick Access menus pause generated frames on supported non-Steam launches, including Heroic/UMU shortcuts. Works across Fixed and Adaptive modes, with or without Scaling.
- **Flatpak VRR controls:** Automatic host discovery connects On/Off requests to the game's verified Gamescope session during Frame Generation. Requires a VRR-capable display. Follow Steam keeps Steam in control, and manual Steam changes take priority over overrides.
- **Adaptive stability:** Steady and Fractional Adaptive avoid repeatedly retrying unsuccessful Smooth Cadence multiplier reductions after stalls or menu returns. Sustained delivery at a lower generated load can qualify a fresh comparison when the scene changes.
- **Frame pacing and recovery:** The matching Renderer improves capped Fractional output spacing and completion-pacing compatibility, while swapchain cleanup avoids blocking native fallback.
- **Shader overhead:** The bundled vkBasalt reduces per-frame CPU work and preserves ReShade effect settings. Effect GPU cost still increases with the Frame Generation multiplier.
- **German interface:** MAKO Decky now follows Steam's German interface language with a complete translated catalog.
- **Japanese translations:** Clearer shader controls, installation messages, and explanations. Thanks to [Tak-attack](https://github.com/Tak-attack) for [PR #71](https://github.com/eugeniosegala/MAKO/pull/71).
