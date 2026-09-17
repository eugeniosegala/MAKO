## What's new in MAKO Renderer v3.3.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/neptune-fury.png" alt="Neptune Fury: a Renaissance-style pixel-art sea god commanding a storm beside a colossal mako, with sailing ships and a distant coastal city" width="100%">

### Release codename: neptune-fury

> _“The waves unruffle and the sea subsides.”_
>
> **Virgil, _The Aeneid_, Book I, translated by John Dryden**

---

<!-- Unreleased: complete release validation before publication. -->

- **Automatic game recognition in the desktop GUI:** Detect a running native Linux or Wine/Proton game, including non-Steam games, and capture its executable into a new or existing profile. The UI also explains the launch setup needed to activate MAKO.
- **Clearer Japanese controls:** The desktop GUI uses consistent Japanese terminology with MAKO Decky and updated Adaptive guidance, extending [Tak-attack’s contribution](https://github.com/eugeniosegala/MAKO/pull/64).
- **Upscaling without the full Gamescope WSI layer:** Scale independently or alongside Frame Generation on supported 64-bit and 32-bit Gamescope X11 launches. The full WSI layer remains an optional compatibility choice for supported 64-bit launches.
- **Lower CPU and memory use:** Reusing recorded GPU commands and temporary resources reduces CPU overhead, RAM and VRAM use during Frame Generation.
- **Smoother Frame Generation cadence:** Smooth Cadence is on by default and uses validated ordered Gamescope presentation to stabilize delivery. Fractional Adaptive retains real frames, while Fixed and Steady Base Cap can favor an even cadence at the cost of real-frame rate and responsiveness; turn it off per game if preferred.
- **More reliable resolution changes:** Reclaim retired rendering resources before checking memory for a replacement, reducing unnecessary scaling rejections when changing resolution.
- **More resilient model loading:** LS1 and LSFG recognize supported model layouts after resource IDs move, with shared checks for inspection and rendering. Restart the game after updating Lossless Scaling.
- **Consistent FP16 defaults:** New configurations, missing precision settings and CLI tools allow FP16 where supported. Explicit choices are preserved, with FP32 fallback on unsupported GPUs.
