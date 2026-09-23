## What's new in MAKO Decky v4.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/inferno.png" alt="Inferno: a Renaissance-style pixel-art mako surges through a volcanic sea between lava-lit cities and ships beneath a moonlit infernal sky" width="100%">

### Release codename: inferno

> _“Come, my friends, ’T is not too late to seek a newer world.”_
>
> **Alfred, Lord Tennyson, _Ulysses_**

---

<!-- Unreleased: complete release validation before publication. -->

MAKO Decky 4.0 introduces Shaders as a first-class MAKO feature, transforming the managed experience from Frame Generation and Spatial Scaling into a complete per-game image-processing suite. Inferno combines that major new creative layer with live controls, display-aware pacing, and recovery designed to protect gameplay instead of reacting to ordinary load.

- **Shaders arrive in MAKO:** Every profile gains a dedicated **Shaders** section backed by MAKO's private 64-bit and 32-bit vkBasalt—no separate system-wide vkBasalt setup required. Sharpening, anti-aliasing, 20% default DLS denoise, and a curated catalog of vivid colour, SDR HDR-style contrast, cinematic, monochrome, retro, and finishing effects can be configured independently for each game.
- **Tune the image while the game runs:** Supported sharpening, anti-aliasing, shader, sharpness, and denoise controls update live. Default and saved profiles use the appropriate editable configuration file, while layer activation, custom ReShade effects, and other advanced manual changes remain safely restart-bound.
- **One image-processing suite:** Frame-gen, Scaling, and Shaders share a compact three-tab **Image Processing** strip with dedicated per-game profiles. Live Status brings active modes, limits, fallbacks, resolutions, and important notices together while scaling guidance identifies when Windowed mode is needed to create real upscaling headroom.
- **Frame Generation you can control live:** **Enable Frame-gen (Restart)** provisions the process once, while the factor selector adds a live `0x` pause beside Fixed 2x–5x or the selected Adaptive mode. Scaling-only profiles avoid unused LSFG resources, and Shaders-only profiles omit the MAKO Renderer layer entirely.
- **Built for VRR and fixed-refresh displays:** Gamescope's live VRR preference, capability, and active state select the correct pacing owner automatically. MAKO can move between its VRR target clock and eligible fixed-refresh FIFO pacing without discarding the validated Adaptive level.
- **Recovery that protects the game:** Frame Generation pauses while a Steam or Decky menu owns input, while real frames and scaling continue. Fresh history resumes on return, and bounded repair is reserved for explicit transitions and real Vulkan acquire failures—not low FPS, a demanding scene, scaling or shader cost, or a merely slow successful present.
- **Adaptive that keeps the best proven workload:** 2x–5x promotions compare adjacent measured workloads and retain the strongest sustainable lower level after a failed step instead of repeatedly probing against degraded gameplay. Fixed keeps its selected multiplier, and eligible live resolution changes receive one safe admission retry.
