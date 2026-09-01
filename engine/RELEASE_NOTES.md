## What's new in MAKO Renderer v3.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyssal-fang.png" alt="Abyssal Fang: a Renaissance vision of a colossal mako rising beside a storm-dark fortified harbour" width="100%">

### Release codename: Abyssal Fang

> _“The abyss keeps its strength beneath the surface; one fang above the tide is warning enough.”_
>
> **Severin Voss, _Cartographer of the Drowned Meridian_**

---

MAKO Renderer 3.0 adds Spatial Scaling as a first-class feature alongside Frame Generation. Render at a lower resolution, reconstruct each real frame with MAKO Scaler or LS1, then optionally generate additional frames for higher displayed FPS.

- **Spatial Scaling joins Frame Generation:** Enable Scaling before starting a game and choose a lower in-game resolution. MAKO reconstructs every real frame to the presentation resolution before optionally applying Fixed or Adaptive Frame Generation; Scaling and Frame Generation can also run independently.
- **Choose how each real frame is rebuilt:** **Native Resolution** is the model-free linear baseline, **MAKO Scaler** is MAKO's open, performance-focused single-pass option, **LS1 Quality** prioritizes reconstruction quality, and **LS1 Performance** lowers the LS1 processing cost. The LS1 options use models from your licensed Lossless Scaling installation through your own `Lossless.dll`; if they are unavailable, MAKO safely falls back to MAKO Scaler.
- **Deck scaling respects the real output:** Variable Gamescope surfaces now cap reconstruction at the positively identified display dimensions before Frame Generation allocates its context. A source that already fills the output stays native, while smaller sources still aspect-fit upward, avoiding discarded supersampling load without changing direct non-Gamescope scaling.
- **Guided installation, updates, and removal:** The standalone archive now includes branded **Install MAKO Renderer** and **Uninstall MAKO Renderer** wizards, complete offline instructions, and the correct Steam launch option. Installation verifies the package, updates safely, and preserves profiles; removal explains when the native Renderer is shared with MAKO Decky and keeps profiles by default unless you explicitly choose to remove them. Both managed installation paths now agree on the active Renderer version.
- **Adaptive recovers when combined scaling load eases:** If a Deck-class 45-to-90 path falls toward 30 real/60 displayed FPS under heavy scaling load, MAKO can now release its automatic half-target cap after the slowdown is proven, letting native cadence recover instead of remaining stuck. Manual caps and Fixed mode are unchanged.
- **Tune more while you play:** Compatible Frame Generation, scaling-method, sharpness, target, and performance settings can update during a game. Process-start options remain restart-bound, keeping the presentation path stable.
