## What's new in MAKO Decky v4.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/inferno.png" alt="Inferno: a Renaissance-style pixel-art mako surges through a volcanic sea between lava-lit cities and ships beneath a moonlit infernal sky" width="100%">

### Release codename: inferno

> _“Come, my friends, ’T is not too late to seek a newer world.”_
>
> **Alfred, Lord Tennyson, _Ulysses_**

---

<!-- Unreleased: complete release validation before publication. -->

MAKO Decky 4.0 adds live image-processing controls, clearer menu behavior, and a more predictable MAKO Renderer across Fixed and Adaptive Frame Generation.

- **Live Frame Generation control:** **Enable Frame-gen (Restart)** provisions the process, while the factor selector adds a live `0x` pause beside Fixed 2x–5x or the selected Adaptive mode. Scaling-only profiles avoid unused LSFG resources, and Shaders-only profiles omit the MAKO Renderer layer.
- **Predictable menu recovery:** Frame Generation pauses while a Steam or Decky menu owns input, real frames and scaling continue, and fresh history resumes on return. Recovery now follows explicit menu or policy transitions and real Vulkan acquire failures instead of ordinary gameplay slowdowns.
- **More stable output:** Adaptive promotions compare adjacent measured workloads and retain the best proven lower-load result after failure. Fixed keeps its selected multiplier, live resolution changes receive one safe admission retry, and Gamescope's live VRR state selects the appropriate pacing owner.
- **Cleaner image-processing UI:** Frame-gen, Scaling, and Shaders share a compact three-tab **Image Processing** strip. Live Status groups notices in one area, and scaling guidance now calls out Windowed mode when fullscreen or borderless leaves no room to upscale.
- **Bundled per-game shaders:** The **Shaders** section manages MAKO's private 64-bit and 32-bit vkBasalt with sharpening, anti-aliasing, 20% default DLS denoise, and a curated catalog spanning vivid colour, SDR HDR-style contrast, cinematic, monochrome, retro, and finishing effects. Default and saved profiles use the appropriate editable configuration file while preserving advanced manual options.
- **Live vkBasalt tuning:** Supported sharpening, anti-aliasing, shader, sharpness, and denoise controls update in a running game. Layer activation, custom ReShade effects, and other advanced manual changes remain restart-bound.
