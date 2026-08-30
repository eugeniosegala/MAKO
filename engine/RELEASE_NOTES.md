## What's new in MAKO Renderer v3.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyssal-fang.png" alt="Abyssal Fang: a Renaissance vision of a colossal mako rising beside a storm-dark fortified harbour" width="100%">

### Release codename: Abyssal Fang

> _“The abyss does not roar before it strikes; it shows one fang.”_
>
> **Cassian, _Hierophant of the Abyssal Forge_**

---

MAKO Renderer 3.0 is a major milestone for MAKO and a clear step forward from earlier versions, with major changes throughout the underlying engine. Render at a lower resolution, reconstruct each real frame with spatial scaling, then optionally combine it with Frame Generation for higher displayed FPS.

- **Spatial Scaling and Lossless Scaling models are here:** Enable Scaling before starting a game, choose a lower in-game resolution, then select Native Resolution, LS1 Quality, LS1 Performance, or **MAKO Scaler** (MAKO's own performance-focused scaler). The LS1 models use your licensed `Lossless.dll`; if they are unavailable, MAKO safely falls back to MAKO Scaler. Spatial Scaling works on its own or alongside Fixed and Adaptive Frame Generation.
- **Frame Generation is smarter and smoother:** Adaptive now offers a steady base cap for more consistent pacing and Fractional Adaptive for targets such as 60 real FPS to 90 displayed FPS. It keeps more real frames, which can reduce input lag and ghosting. Fixed mode remains available when you prefer a simple 2x to 5x multiplier.
- **Tune more while you play:** Compatible Frame Generation, scaling-method, sharpness, target, and performance settings can update during a game. Process-start options remain restart-bound, keeping the presentation path stable.
- **Presentation is more resilient:** MAKO handles Gamescope and desktop surfaces, resolution changes, uneven frame times, and unavailable scaling models more robustly. When a requested path is not safe or supported, MAKO falls back cleanly instead of risking the game session.
- **Built and tested for real play:** The new spatial path is covered by image-quality, performance, synchronization, recovery, and sustained-load validation on supported AMD hardware, so the extra visual quality comes with the reliability MAKO is built around.
