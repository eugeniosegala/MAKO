## What's new in MAKO Renderer v3.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyssal-fang.png" alt="Abyssal Fang: a Renaissance vision of a colossal mako rising beside a storm-dark fortified harbour" width="100%">

### Release codename: Abyssal Fang

> _“The abyss keeps its strength beneath the surface; one fang above the tide is warning enough.”_
>
> **Severin Voss, _Cartographer of the Drowned Meridian_**

---

MAKO Renderer 3.0 is a major milestone for MAKO and a clear step forward from earlier versions, with major changes throughout the underlying engine. Render at a lower resolution, reconstruct each real frame with spatial scaling, then optionally combine it with Frame Generation for higher displayed FPS.

- **Spatial Scaling joins Frame Generation:** Enable Scaling before starting a game and choose a lower in-game resolution. MAKO reconstructs every real frame to the presentation resolution before optionally applying Fixed or Adaptive Frame Generation; Scaling and Frame Generation can also run independently.
- **Choose how each real frame is rebuilt:** **Native Resolution** is the model-free linear baseline, **MAKO Scaler** is MAKO's open, performance-focused single-pass option, **LS1 Quality** prioritizes reconstruction quality, and **LS1 Performance** lowers the LS1 processing cost. The LS1 options use models from your licensed Lossless Scaling installation through your own `Lossless.dll`; if they are unavailable, MAKO safely falls back to MAKO Scaler.
- **Deck scaling respects the real output:** Variable Gamescope surfaces now cap reconstruction at the positively identified display dimensions before Frame Generation allocates its context. A source that already fills the output stays native, while smaller sources still aspect-fit upward, avoiding discarded supersampling load without changing direct non-Gamescope scaling.
- **Guided installation, updates, and removal:** The standalone archive now includes branded **Install MAKO Renderer** and **Uninstall MAKO Renderer** wizards, complete offline instructions, and the correct Steam launch option. Installation verifies the package, updates safely, and preserves profiles; removal explains when the native Renderer is shared with MAKO Decky and keeps profiles by default unless you explicitly choose to remove them. Both managed installation paths now agree on the active Renderer version.
- **Frame Generation is smarter and smoother:** Adaptive now offers a steady base cap for more consistent pacing and Fractional Adaptive for targets such as 60 real FPS to 90 displayed FPS. On ordered Gamescope SDR, a proven combined scaling-load collapse releases the automatic half-target cap instead of leaving Deck-class hardware trapped at 30 real/60 displayed FPS after the load recovers. Manual and Fixed caps remain unchanged. Fixed mode remains available when you prefer a simple 2x to 5x multiplier.
- **Tune more while you play:** Compatible Frame Generation, scaling-method, sharpness, target, and performance settings can update during a game. Process-start options remain restart-bound, keeping the presentation path stable.
- **Presentation is more resilient:** MAKO handles Gamescope and desktop surfaces, resolution changes, uneven frame times, and unavailable scaling models more robustly. When a requested path is not safe or supported, MAKO falls back cleanly instead of risking the game session.
- **Built and tested for real play:** The new spatial path is covered by image-quality, performance, synchronization, recovery, and sustained-load validation on supported AMD hardware, so the extra visual quality comes with the reliability MAKO is built around.
