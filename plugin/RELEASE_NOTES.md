## What's new in MAKO Decky v2.1.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyss-ascending.png" alt="Abyss Ascending: a Renaissance vision of the dormant MAKO beneath a Tuscan bay" width="100%">

### Release codename: Abyss Ascending

> _“A moment's pause, and then the abyss stretched, beckoned. We all do.”_
>
> **Nylah, _Hour of Eternity_**

---

MAKO Decky’s Abyss Ascending release is a major Adaptive Frame Generation upgrade for Game Mode. It pairs a more disciplined Renderer with clearer per-game control, so high targets, Fractional Adaptive, and demanding scenes have a better path to smooth, responsive motion. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive is steadier where motion matters:** A target-aware placement clock moves already-earned generation away from clearly short source intervals instead of creating uneven bursts. This gives targets such as 60-to-90 and 80-to-120 a more even camera pan while retaining more real frames for potentially lower input lag and ghosting.
- **Smooth Cadence now proves its clean 2x path:** When Target FPS matches Gamescope refresh, MAKO can test a nearby constant cadence and retain it only after delivery settles at the target. It falls back immediately when the evidence changes, so Steady and Fractional modes remain deliberate per-game choices rather than a blind multiplier switch.
- **Ultra Performance gives constrained GPUs a serious option:** This restart-only per-profile preset combines 75% Flow Scale, the Lighter FG Model, FP16 where supported, and a leaner active-policy allocation. In favourable GPU-limited scenes it can improve frame-generation performance by up to 30%, while Decky shows the forced settings and protects the restart boundary.
- **Recovery now covers the interruptions that matter:** Ordered presentation pressure drains through native frames, warms history, and resumes through a guarded probe instead of leaving a game stranded after menus or overlays. Dynamic Cadence Recovery also handles 30 FPS gameplay ↔ 60 FPS menus, with a live 0.25–3 second probe interval for emulators and other rate-switching games.
- **Per-game tuning is now built on a stronger foundation:** Refactored canonical profiles and runtime session updates keep live controls attached to the running game, preserve safe restart-only changes, and expose the refresh guard, matched processes, recovery, and Adaptive trade-offs in one coherent Decky experience.
