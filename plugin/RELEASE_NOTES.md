## What's new in MAKO Decky v4.1.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/maelstrom.png" alt="Maelstrom release artwork: a colossal mako above a spiraling sea" width="100%">

### Release codename: Maelstrom

> _“We lashed the charts to the mast. The sea had begun rewriting them.”_
>
> **Mira Valen, _Charts of the Last Fleet_**

---

Maelstrom brings steadier Adaptive behavior and better integration with Steam Game Mode through the matching MAKO Renderer update.

- **Non-Steam menu handling:** Steam and Quick Access menus pause generated frames on supported non-Steam launches, including Heroic/UMU shortcuts. Works across Fixed and Adaptive modes, with or without Scaling.
- **Flatpak VRR controls:** Automatic host discovery connects On/Off requests to the game's verified Gamescope session during Frame Generation. Requires a VRR-capable display. Follow Steam keeps Steam in control, and manual Steam changes take priority over overrides.
- **Adaptive stability:** Steady and Fractional Adaptive avoid repeatedly retrying unsuccessful Smooth Cadence multiplier reductions after stalls or menu returns. Sustained delivery at a lower generated load can qualify a fresh comparison when the scene changes.
- **Frame pacing and recovery:** The matching Renderer improves capped Fractional output spacing and completion-pacing compatibility, while swapchain cleanup avoids blocking native fallback.
- **Japanese translations:** Clearer shader controls, installation messages, and explanations. Thanks to [Tak-attack](https://github.com/Tak-attack) for [PR #71](https://github.com/eugeniosegala/MAKO/pull/71).
