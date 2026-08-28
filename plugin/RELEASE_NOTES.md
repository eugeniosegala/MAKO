## What's new in MAKO Decky v2.2.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyssal-fang.png" alt="Abyssal Fang: a Renaissance vision of a colossal mako rising beside a storm-dark fortified harbour" width="100%">

### Release codename: Abyssal Fang

> _“A clean edge needs no spectacle; it only needs the right moment.”_
>
> **Aurelius, _Cardinal of the Ninth Tide_**

---

MAKO Decky’s Abyssal Fang release brings spatial scaling and expanded frame generation into one per-game experience. Native Resolution, MAKO Scaler, LS1 Quality, and LS1 Performance can run alone or before Fixed or Adaptive generation, while the interface makes each live, recreation-bound, and restart-bound control explicit. The focus remains practical: predictable changes during play, clear fallbacks, and safer behavior across handheld, desktop, docked, and emulator workloads.

- **Scaling is a first-class per-game mode:** **Enable Scaling (Restart)** provisions the complete presentation path, with Native Resolution selected by default. Method and sharpness remain live inside that path, Scale Factor waits for a natural resolution change or restart, and LS1 reports when it falls back to MAKO Scaler. Scaling and Frame Generation retain their saved settings independently.
- **Live controls respect real resource boundaries:** Frame Generation, Fixed or Adaptive selection, target, refresh guard, and multipliers within available capacity normally apply during play. Flow Scale, Lighter FG Model, and capacity growth wait for their safe game-owned recreation boundary; Ultra Performance, GPU, DLL, FP16, and launcher compatibility remain restart-bound.
- **Fixed and Adaptive generation now cover 2x through 5x:** Higher multipliers are available for high-refresh displays with enough GPU and memory headroom, while MAKO stays within the capacity actually reserved by the running process until a safe growth boundary occurs.
- **Fractional Adaptive is steadier:** A new target-aware placement clock avoids uneven bursts when base frame times vary. Targets such as 60-to-90 and 80-to-120 get a more even camera pan while retaining more real frames for potentially lower input lag and ghosting.
- **Smooth Cadence earns a clean Steady 2x path:** When Target FPS matches Gamescope refresh, MAKO tests a constant cadence and keeps it only after the game proves it can sustain the target. If conditions change, it immediately returns to the normal Adaptive planner. Steady and Fractional are now deliberate per-game choices instead of a blind multiplier switch.
- **Ultra Performance can free meaningful GPU headroom without freezing unrelated controls:** This process-start per-profile preset combines 75% Flow Scale, the Lighter FG Model, FP16 where supported, and leaner active-policy resources. Turning it on or off requires restart, but compatible settings remain live afterward. In favourable GPU-limited scenes, it can improve frame-generation performance by up to 30%; it also increases visual artifacts, so it is best suited to constrained devices such as Steam Deck.
- **Dynamic Cadence Recovery helps emulators, menus, and overlays recover cleanly:** It detects native-rate switches such as 30 FPS gameplay to a 60 FPS menu, briefly exposes the real cadence, and returns to the correct Fixed or Adaptive behavior. It is opt-in per game because each check can briefly affect pacing. Choose a live 0.1–3 second interval: 2 seconds is the default, while 0.1 seconds is an aggressive emulator-focused option.
- **Frame generation can now follow your display:** Auto-disable Frame Generation by Refresh Rate pauses generation at or below a chosen Gamescope refresh threshold, then restores the selected mode when a faster display is detected. It is ideal for dock and display changes without overwriting the profile you tuned.
- **Per-game control is clearer and safer:** Runtime session updates keep edits attached to the game that is running, preserve deferred changes, and bring scaling, refresh behavior, matched processes, recovery, and Adaptive trade-offs together in one Decky experience.
