## What's new in MAKO Renderer v2.1.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyss-ascending.png" alt="Abyss Ascending: a Renaissance vision of the dormant MAKO beneath a Tuscan bay" width="100%">

### Release codename: Abyss Ascending

> _“A moment's pause, and then the abyss stretched, beckoned. We all do.”_
>
> **Nylah, _Hour of Eternity_**

---

MAKO Renderer’s Abyss Ascending release consolidates Adaptive Frame Generation around stable pacing, honest recovery, and a leaner execution path. It makes fractional targets more deliberate, constant cadences evidence-based, and difficult scene changes less likely to interrupt the game. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive now places frames for smoother motion:** A target-aware placement clock can defer one already-earned output away from a clearly short source interval and repay it only when later capacity improves spacing. Generated work stays within the selected ceiling, real frames remain mandatory, and targets such as 60-to-90 and 80-to-120 receive a more even cadence for cleaner camera movement.
- **Smooth Cadence earns its Fixed-like 2x result:** On ordered Gamescope SDR with target-matched refresh, MAKO tests a constant 2x cadence instead of assuming it will work. It retains that path only after proving 98–102% target delivery, lets FIFO own the settled Steady path, and restores the normal cap or Fractional planner immediately on failure.
- **Ultra Performance cuts GPU work without bypassing safety:** The restart-only profile policy forces 75% Flow Scale, the Lighter FG Model, and FP16 permission when supported, sizes generated-output resources for the active policy, and skips live profile checks. It can improve frame-generation performance by up to 30% in favourable GPU-limited scenarios while refresh, HDR, presentation safety, and recovery remain active.
- **Recovery handles overlays, menus, and rate switches:** Generated-image pressure now drains through native presentation, rebuilds temporal history, and returns through a zero-wait probe before restoring the full plan. Dynamic Cadence Recovery distinguishes a true fixed rate from a 30 ↔ 60 FPS transition, so Adaptive recalibrates to its target and Fixed can follow confirmed Gamescope refresh.
- **The core is lighter, more testable, and easier to diagnose:** The engine refactor keeps requested, admitted, scheduled, and delivered work explicit, while generated-frame plans, submission storage, and timestamp uploads avoid normal-path allocations and redundant copies. Structured diagnostics expose pacing, load shedding, recovery, and stabilization without turning ordinary gameplay logging into a hot-path cost.
