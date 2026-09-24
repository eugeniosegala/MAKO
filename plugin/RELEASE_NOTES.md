## What's new in MAKO Decky v4.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/inferno.png" alt="Inferno release artwork" width="100%">

### Release codename: inferno

<!-- Unreleased: complete release validation before publication. -->

Shaders are the defining feature of MAKO Decky 4.0. MAKO's maintained and modified vkBasalt fork is now installed and managed with MAKO Renderer for 64-bit and 32-bit games, including prepared Flatpak applications. It provides live per-game controls and a curated effect library while preserving standard vkBasalt configuration for custom chains and additional ReShade-compatible effects. This expands MAKO from Frame Generation and Scaling into a managed image-processing platform without requiring a separate vkBasalt installation.

- **Managed shader platform:** Each profile now has a **Shaders** section backed by MAKO's pinned vkBasalt fork. Available controls include sharpening, anti-aliasing, 20% default DLS denoise, and curated colour, HDR-style contrast for SDR, cinematic, monochrome, retro, and finishing effects. MAKO manages the layer, architecture, configuration path, and native or Flatpak package for the selected game.
- **Live and advanced control:** Supported sharpening, anti-aliasing, shader, sharpness, and denoise settings update while the game is running. Default and saved profiles both use editable standard vkBasalt configuration files, so custom chains and manually added ReShade effects can be used alongside MAKO's controls. Layer activation and manual advanced changes apply after restarting the game.
- **Image Processing interface:** Frame Generation, Scaling, and Shaders are grouped in a three-tab **Image Processing** section. Live Status reports active modes, limits, fallbacks, resolutions, and relevant notices. Scaling guidance identifies when Windowed mode is required to provide upscaling headroom.
- **Live Frame Generation pause:** **Enable Frame-gen (Restart)** provisions Frame Generation when the process starts. The factor selector can then pause and resume it with `0x` alongside Fixed 2x–5x and Adaptive modes. Scaling-only profiles do not allocate LSFG resources, and shader-only profiles do not load the MAKO Renderer layer.
- **Pacing and menu behavior:** Gamescope's VRR state selects VRR target-clock pacing or eligible fixed-refresh FIFO pacing without resetting the current Adaptive level. Frame Generation pauses while a Steam or Decky menu has focus; real frames and scaling continue, and temporal history is reset before generation resumes.
- **Recovery and Adaptive scheduling:** Presentation recovery runs only after a Vulkan acquisition failure, not because a game is rendering slowly or using expensive processing. Adaptive promotions compare adjacent measured workloads and keep the lower sustainable level after a failed promotion. Fixed mode retains its selected multiplier, and eligible resolution changes can retry resource allocation once.
