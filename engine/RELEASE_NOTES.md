## What's new in MAKO Renderer v3.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyssal-fang.png" alt="Abyssal Fang: a Renaissance vision of a colossal mako rising beside a storm-dark fortified harbour" width="100%">

### Release codename: Abyssal Fang

> _“The abyss does not roar before it strikes; it shows one fang.”_
>
> **Cassian, _Hierophant of the Abyssal Forge_**

---

MAKO Renderer 3.0 introduces controlled spatial scaling to the Vulkan presentation path. A game can render at a lower source resolution, then MAKO reconstructs each real frame for direct presentation or before optional Frame Generation. The new path is built around clear runtime boundaries, guarded presentation ownership, and evidence-driven quality and performance validation.

- **Spatial scaling can run on its own or before Frame Generation:** Enable it at game start to provision the required path, then use scaling alone or combine it with Fixed or Adaptive Frame Generation. A process that could not reserve Frame Generation resources at startup still keeps independent scaling available and reports that generation needs a restart.
- **Four reconstruction methods cover the supported use cases:** Native Resolution supplies a model-free linear baseline, MAKO Scaler provides repository-owned single-pass reconstruction with bounded sharpening, and LS1 Quality or LS1 Performance use the spatial model from the user's licensed `Lossless.dll`. LS1 safely falls back to MAKO Scaler with a diagnostic reason when its resources, translator, format, or pipeline are unavailable.
- **Scale Factor can cross its real boundary safely, including mixed profile edits:** Scaling Method and Scaling Sharpness still rebuild only MAKO's private scaler. After a Scale Factor edit settles, an eligible lower spatial role requests exactly one game-owned recreation after a successful maintenance1-fenced present; unsupported paths retain the change until a natural resolution change or restart. If that request reaches a generated-image present, the upper frame-generation role submits the already-acquired real image before propagating out-of-date; if it reaches the original-image present, that image has already traversed the lower path and MAKO propagates the request without submitting it twice. A simultaneous Fixed multiplier/capacity or Fixed-to-Adaptive ceiling/capacity change remains live through its private transition or the replacement context's latest profile. Enabling scaling remains restart-bound because the Vulkan layer chain is fixed at process start.
- **Presentation stays safe across Gamescope and desktop surfaces:** MAKO keeps an explicit source and presentation extent contract, rejects unsupported swapchain shapes, avoids recursive compositor feedback, and stays native when a requested presentation extent would exceed conservative surface or memory limits.
- **The scaling path is qualified for image quality, cost, and recovery:** The Renderer now carries dedicated spatial scaling policy, shader, diagnostics, and validation coverage for source and presentation extents, formats, synchronization, fallback, live transitions, and recovery. MAKO Gym adds real-hardware quality, performance, repeatability, and synchronization evidence for the supported path.
