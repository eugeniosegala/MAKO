## What's new in MAKO Decky v3.3.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/neptune-fury.png" alt="Neptune Fury: a Renaissance-style pixel-art sea god commanding a storm beside a colossal mako, with sailing ships and a distant coastal city" width="100%">

### Release codename: neptune-fury

> _“The realms of ocean and the fields of air are mine, not his.”_
>
> **Virgil, _The Aeneid_, Book I, translated by John Dryden**

---

<!-- Unreleased: complete release validation before publication. -->

- **Independent upscaling:** Scaling no longer enables Gamescope WSI automatically. Supported 64-bit and 32-bit Gamescope games can upscale with WSI off.
- **Lower CPU and memory use:** The updated MAKO Renderer reduces CPU overhead and RAM/VRAM use, with improved memory handling during resolution changes.
- **Smoother Frame Generation cadence:** Smooth Cadence is on by default and uses validated ordered Gamescope presentation to stabilize delivery. Fractional Adaptive retains real frames, while Fixed and Steady Base Cap can favor an even cadence at the cost of real-frame rate and responsiveness; turn it off per game if preferred.
- **A cleaner panel:** Press R1 for a compact view that retains Live Status and warnings. Centered headings, solid dark violet dividers and preserved controller focus make navigation clearer.
- **Clearer model warnings:** See LS1 and LSFG compatibility problems together, with troubleshooting guidance and separate instructions when Lossless Scaling is missing.
- **Improved Japanese translations:** Translated scaling and Live Status messages, with consistent control names and updated Adaptive guidance across MAKO Decky and MAKO Renderer. Thanks to [Tak-attack](https://github.com/Tak-attack) for [PR #64](https://github.com/eugeniosegala/MAKO/pull/64).
- **Clearer vkBasalt integration:** Updated controls and documentation cover 64-bit and 32-bit use, with a separate matching installation and links to official effect configuration guidance.
