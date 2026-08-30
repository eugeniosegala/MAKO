## What's new in MAKO Decky v3.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyssal-fang.png" alt="Abyssal Fang: a Renaissance vision of a colossal mako rising beside a storm-dark fortified harbour" width="100%">

### Release codename: Abyssal Fang

> _“A clean edge needs no spectacle; it only needs the right moment.”_
>
> **Aurelius, _Cardinal of the Ninth Tide_**

---

MAKO Decky 3.0 brings spatial scaling into the per-game workflow. It makes the new Renderer path practical to configure, explains which changes apply live or need a boundary, and keeps each game's scaling, Frame Generation, and compatibility choices safely isolated.

- **Spatial Scaling is now a first-class per-game control:** Enable Scaling provisions the path at the next game start. Choose Native Resolution, MAKO Scaler, LS1 Quality, or LS1 Performance, then use scaling by itself or alongside Fixed or Adaptive Frame Generation. Native Resolution is the model-free default, and the licensed LS1 options fail safely to MAKO Scaler when their requirements are unavailable.
- **The scaling controls make the source-to-output relationship explicit:** Scale Factor covers 1.0x through 2.0x, while Scaling Sharpness is available for MAKO Scaler and LS1. The new in-panel info tip shows practical 2x comparisons such as 720p to 1440p and 1080p to 2160p, while making clear that near-native scaling can look subtle.
- **Each control states its real lifetime without a status pop-up:** Method and sharpness updates are live inside a provisioned game. After a Scale Factor edit settles, MAKO requests one guarded game-owned recreation when the lower spatial role has maintenance1 retirement proof; simultaneous Fixed or Adaptive policy, multiplier, target, cap, and capacity edits remain live through their own boundary and are retained by the replacement. Unsupported paths defer only the factor until the next natural resolution change or restart. The transition pop-up and its frontend polling are removed. Enable Scaling, the Vulkan layer chain, and launcher compatibility settings remain restart-bound.
- **Profiles and launch preparation keep the path isolated:** Decky persists scaling independently from Frame Generation, stages the required layer and Gamescope WSI roles only for the selected game process, and fails closed when an architecture, runtime, or dependency cannot support the requested path. The neutral **Gamescope WSI (Restart)** control is checked and locked when Scaling owns the validated managed path; independently enabled FG-only compatibility retains clear 64-bit host scope and performance guidance.
- **Guidance is available without getting in the way:** A compact MAKO Team welcome card explains live versus restart-bound changes, recommends a clean game session after repeated edits, and can be collapsed permanently once the player has read it.
