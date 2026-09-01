## What's new in MAKO Decky v3.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyssal-fang.png" alt="Abyssal Fang: a Renaissance vision of a colossal mako rising beside a storm-dark fortified harbour" width="100%">

### Release codename: Abyssal Fang

> _“Keep the beacon lit and the harbour still; what hunts below mistakes panic for invitation.”_
>
> **Marcelline Veyr, _Warden of the Blackwater Gate_**

---

MAKO Decky 3.0 introduces Spatial Scaling as a first-class MAKO feature, joining it with Frame Generation in one per-game workflow. Render at a lower resolution, reconstruct each real frame with MAKO Scaler or LS1, then optionally generate additional frames for higher displayed FPS.

- **Spatial Scaling joins Frame Generation:** Turn on **Enable Scaling** before launching a game and choose a lower in-game resolution. MAKO reconstructs every real frame to the presentation resolution before optionally applying Fixed or Adaptive Frame Generation; Scaling and Frame Generation can also run independently.
- **Choose how each real frame is rebuilt:** **Native Resolution** is the model-free linear baseline, **MAKO Scaler** is MAKO's open, performance-focused single-pass option, **LS1 Quality** prioritizes reconstruction quality, and **LS1 Performance** lowers the LS1 processing cost. The LS1 options use models from your licensed Lossless Scaling installation through your own `Lossless.dll`; if they are unavailable, MAKO safely falls back to MAKO Scaler.
- **Cleaner native install handoff:** MAKO Decky and the standalone archive now agree on the active Renderer version. Installing either one selects its version, and uninstalling MAKO Renderer removes files supplied by both managed native installation paths while preserving MAKO Decky and profiles.
- **Frame Generation is smarter and smoother:** Adaptive now offers a steady base cap for more consistent pacing and Fractional Adaptive for targets such as 60 real FPS to 90 displayed FPS. It keeps more real frames, which can reduce input lag and ghosting. Fixed mode remains available when you prefer a simple 2x to 5x multiplier.
- **Tune more while you play:** Change compatible Frame Generation, scaling-method, sharpness, target, and performance settings during a game. Options that genuinely need a fresh game session are clearly marked **(Restart)**.
- **Per-game setup is more reliable:** Frame Generation, Scaling, and compatibility choices stay isolated to the game profile you configure. MAKO automatically uses the required compatibility path for Scaling and safely selects a fallback when a requested model cannot be used.
- **The new controls are easier to understand:** Clearer guidance shows what Scale Factor does, how 2x changes the output resolution, and the performance versus image-quality trade-off, without filling the screen with transition messages.
