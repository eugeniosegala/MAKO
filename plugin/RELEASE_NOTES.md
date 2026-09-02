## What's new in MAKO Decky v3.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyssal-fang.png" alt="Abyssal Fang: a Renaissance vision of a colossal mako rising beside a storm-dark fortified harbour" width="100%">

### Release codename: Abyssal Fang

> _“Keep the beacon lit and the harbour still; what hunts below mistakes panic for invitation.”_
>
> **Marcelline Veyr, _Warden of the Blackwater Gate_**

---

MAKO Decky 3.0 introduces Spatial Scaling as a first-class MAKO feature, joining it with Frame Generation in one per-game workflow. Render at a lower resolution, reconstruct each real frame with MAKO Scaler or LS1, then optionally generate additional frames for higher displayed FPS. The expanded v3 workflow now explains the actual render and display result, applies live scaling changes reliably, and keeps recovery actionable when a requested resolution cannot be used.

- **Spatial Scaling joins Frame Generation:** Turn on **Enable Scaling** before launching a game and choose a lower in-game resolution. MAKO reconstructs every real frame to the presentation resolution before optionally applying Fixed or Adaptive Frame Generation; Scaling and Frame Generation can also run independently.
- **Choose how each real frame is rebuilt:** **Native Resolution** is the model-free linear baseline, **MAKO Scaler** is MAKO's open, performance-focused single-pass option, **LS1 Quality** prioritizes reconstruction quality, and **LS1 Performance** lowers the LS1 processing cost. The LS1 options use models from your licensed Lossless Scaling installation through your own `Lossless.dll`; if they are unavailable, MAKO safely falls back to MAKO Scaler.
- **Quality Supersampling is explicit and understandable:** When enabled, MAKO can reconstruct beyond the display target while remaining inside Vulkan and GPU-memory limits. Live Status shows **Input**, **Render**, and **Display** resolutions separately and uses a compact status line instead of squeezing an output arrow and explanatory paragraph into the scaling column.
- **Live Status now explains the real limit:** If the input already fills the display, the card tells you to lower the in-game resolution or enable Quality Supersampling. If GPU memory constrains a request, it reports the requested and effective factors; if the request cannot be admitted at all, it asks you to lower the in-game resolution instead of showing a generic inactive state.
- **Scaling state follows live resolution changes:** Returning to 1.0×, disabling Scaling, lowering the source resolution, or reducing the requested factor clears obsolete memory warnings and selects the context that actually owns the requested scaler. Valid downshifts continue from the last proven safe envelope rather than becoming stuck behind delayed driver memory accounting.
- **Game startup and replacement are more compatible:** MAKO no longer over-requests application swapchain images for Frame Generation, and a replaced resolution briefly rebuilds fresh history before generated frames resume. The behavior is generic and contains no title-specific Steam app-ID exception.
- **Five recent game sessions are retained for diagnostics:** The managed wrapper now rotates five opt-in session logs instead of three. The diagnostics helper can select the latest, previous, oldest retained, two previous, or all retained sessions when a problem spans several launches.
- **Cleaner native install handoff:** MAKO Decky and the standalone archive now agree on the active Renderer version. Installing either one selects its version, and uninstalling MAKO Renderer removes files supplied by both managed native installation paths while preserving MAKO Decky and profiles.
- **MAKO-only upgrade baseline:** Direct state upgrades are supported from public MAKO 2.0.0 and newer. The differently named experimental predecessor remains a separate installation, and MAKO no longer imports its settings, runtime files, or pre-public wrapper formats.
- **Adaptive recovers when combined scaling load eases:** If a Deck-class 45-to-90 path falls toward 30 real/60 displayed FPS under heavy scaling load, MAKO can now release its automatic half-target cap after the slowdown is proven, letting native cadence recover instead of remaining stuck. Manual caps and Fixed mode are unchanged.
- **Tune more while you play:** Change compatible Frame Generation, scaling-method, sharpness, target, and performance settings during a game. Options that genuinely need a fresh game session are clearly marked **(Restart)**.
- **The new controls are easier to understand:** Clearer guidance shows what Scale Factor does, how 2x changes the output resolution, and the performance versus image-quality trade-off, without filling the screen with transition messages.
