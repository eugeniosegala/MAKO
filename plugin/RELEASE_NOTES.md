## What's new in MAKO Decky v3.2.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/the-captain.png" alt="The Captain: a Renaissance-style pixel-art captain and crew plotting an attack over a nautical chart aboard their ship, while a colossal mako looms over the fleet in the stormy sea behind them" width="100%">

### Release codename: The Captain

> _“Mark every ship upon the chart. Leave room for what moves beneath it.”_
>
> **Mira Valen, _At the Captain's Table_**

---

### Stability, frame pacing, and compatibility

MAKO 3.2 addresses uneven frame delivery and unnecessary recovery resets, updates launcher compatibility, and protects saved settings during failed writes and updates.

- **Stability and frame-pacing fixes:** Includes the Renderer fixes for uneven game cadence and brief presentation timeouts, helping avoid unnecessary recovery resets and keeping generated-frame delivery steadier.
- **Per-game Lutris setup:** Enable MAKO through each game's **Command prefix**, with dedicated Flatpak preparation and launcher-specific setup instructions. Previously prepared Flatpak Lutris installations can be prepared again to remove old app-wide activation.
- **Ubisoft Connect launcher exclusion:** Launcher and web UI processes are excluded from profile capture and Renderer activation, while the game keeps its normal MAKO profile.
- **LS1 availability and fallback status:** Checks the selected LS1 model and explains when MAKO Scaler is being used as a fallback, while preserving your saved LS1 choice.
- **Unavailable Live Status metrics:** The panel now states that MAKO may still be active when a game or emulator does not report live metrics.
- **Base FPS Cap slider saving:** Slider changes wait for a brief pause before saving, avoiding temporary low caps while you drag.
- **Renderer installation recovery:** Failed native installs restore the previous files, selected version, and configuration. Installation retains valid profiles and recreates defaults when the existing configuration cannot be read or validated; read-only configurations still stop installation.
- **Renderer compatibility and resource fixes:** Also includes newer VKD3D-Proton presentation compatibility, corrected scaling memory limits, and cleanup after failed graphics-resource setup.
