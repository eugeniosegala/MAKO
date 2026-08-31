## What's new in MAKO Decky v3.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyssal-fang.png" alt="Abyssal Fang: a Renaissance vision of a colossal mako rising beside a storm-dark fortified harbour" width="100%">

### Release codename: Abyssal Fang

> _“A clean edge needs no spectacle; it only needs the right moment.”_
>
> **Aurelius, _Cardinal of the Ninth Tide_**

---

MAKO Decky 3.0 is a major milestone for MAKO and a clear step forward from earlier versions, with major changes to the underlying engine. Render games at a lower resolution, rebuild image detail with spatial scaling, and pair it with Frame Generation when you want even higher displayed FPS.

- **Spatial Scaling and Lossless Scaling models are here:** Turn on **Enable Scaling** before launching a game, choose a lower in-game resolution, then select Native Resolution, LS1 Quality, LS1 Performance, or **MAKO Scaler** (MAKO's own performance-focused scaler). The LS1 models use your licensed `Lossless.dll`; if they are unavailable, MAKO safely falls back to MAKO Scaler. Fixed and Adaptive Frame Generation work on their own or alongside Spatial Scaling.
- **A scaler for every setup:** **MAKO Scaler** is MAKO's built-in, open, performance-focused scaler and works without Lossless Scaling. **LS1 Quality** and **LS1 Performance** use the proprietary models from your licensed Lossless Scaling installation through your own `Lossless.dll`.
- **Cleaner native install handoff:** MAKO Decky and the standalone archive now agree on the active Renderer version. Installing either one selects its version, and uninstalling MAKO Renderer removes files supplied by both managed native installation paths while preserving MAKO Decky and profiles.
- **Frame Generation is smarter and smoother:** Adaptive now offers a steady base cap for more consistent pacing and Fractional Adaptive for targets such as 60 real FPS to 90 displayed FPS. It keeps more real frames, which can reduce input lag and ghosting. Fixed mode remains available when you prefer a simple 2x to 5x multiplier.
- **Tune more while you play:** Change compatible Frame Generation, scaling-method, sharpness, target, and performance settings during a game. Options that genuinely need a fresh game session are clearly marked **(Restart)**.
- **Per-game setup is more reliable:** Frame Generation, Scaling, and compatibility choices stay isolated to the game profile you configure. MAKO automatically uses the required compatibility path for Scaling and safely selects a fallback when a requested model cannot be used.
- **The new controls are easier to understand:** Clearer guidance shows what Scale Factor does, how 2x changes the output resolution, and the performance versus image-quality trade-off, without filling the screen with transition messages.
