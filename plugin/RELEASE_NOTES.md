## What's new in MAKO Decky v3.2.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/the-captain.png" alt="The Captain: a Renaissance-style pixel-art captain and crew plotting an attack over a nautical chart aboard their ship, while a colossal mako looms over the fleet in the stormy sea behind them" width="100%">

### Release codename: The Captain

> _“Mark every ship upon the chart. Leave room for what moves beneath it.”_
>
> **Mira Valen, _At the Captain's Table_**

---

### A steadier course through troubled waters

The Captain is MAKO 3.2: a focused update to Frame Generation recovery, launcher compatibility, and the safeguards that protect your settings and installation.

- **Per-game Lutris setup:** Enable MAKO through each game's **Command prefix**, with dedicated Flatpak preparation and clearer launcher guides. Previously prepared Flatpak Lutris installations can be prepared again to remove old app-wide activation.
- **Cleaner Ubisoft Connect launches:** Launcher and web UI processes are excluded from profile capture and Renderer activation, while the game keeps its normal MAKO profile.
- **Clearer LS1 availability:** Checks the selected LS1 model and explains when MAKO Scaler is being used as a fallback, while preserving your saved LS1 choice.
- **More honest Live Status:** Missing live metrics now explain that MAKO may still be active, instead of telling you the game is not using it.
- **Smoother Base FPS Cap edits:** Slider changes wait for a brief pause before saving, avoiding temporary low caps while you drag.
- **Safer Renderer installation:** Failed native installs restore the previous files, selected version, and configuration. Installation retains valid profiles and recreates defaults when the existing configuration cannot be read or validated; read-only configurations still stop installation.
- **Updated MAKO Renderer:** Includes the recovery and uneven-cadence fixes, newer VKD3D-Proton presentation compatibility, more accurate scaling memory limits, and cleanup after failed graphics-resource setup.
