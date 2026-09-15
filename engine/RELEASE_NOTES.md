## What's new in MAKO Renderer v3.3.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/neptune-fury.png" alt="Neptune Fury: a Renaissance-style pixel-art sea god commanding a storm beside a colossal mako, with sailing ships and a distant coastal city" width="100%">

### Release codename: neptune-fury

> _“The waves unruffle and the sea subsides.”_
>
> **Virgil, _The Aeneid_, Book I, translated by John Dryden**

---

<!-- Unreleased: complete release validation before publication. -->

- **Upscaling without the full Gamescope WSI layer:** Scale independently or alongside Frame Generation on supported 64-bit and 32-bit Gamescope X11 launches. The full WSI layer remains an optional compatibility choice for supported 64-bit launches.
- **Lower CPU and memory use:** Reusing recorded GPU commands and temporary resources reduces CPU overhead, RAM and VRAM use during Frame Generation.
- **More reliable resolution changes:** Reclaim retired rendering resources before checking memory for a replacement, reducing unnecessary scaling rejections when changing resolution.
- **More resilient model loading:** LS1 and LSFG recognize supported model layouts after resource IDs move, with shared checks for inspection and rendering. Restart the game after updating Lossless Scaling.
- **Consistent FP16 defaults:** New configurations, missing precision settings and CLI tools allow FP16 where supported. Explicit choices are preserved, with FP32 fallback on unsupported GPUs.
