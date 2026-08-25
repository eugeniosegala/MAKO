## What's new in MAKO Renderer v2.2.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyss-ascending.png" alt="Abyss Ascending: a Renaissance vision of the dormant MAKO beneath a Tuscan bay" width="100%">

### Release codename: Abyss Ascending

> _“Where the hidden currents meet in light, the great Mako rises without a wake.”_
>
> **Cassian, _Hierophant of the Abyssal Forge_**

---

MAKO Renderer’s Abyss Ascending release is built around a simple player outcome: smoother frame generation that stays responsive when a game, display, or emulator changes pace. Adaptive targets are paced more evenly, Steady 2x is retained only when it is genuinely sustainable, and recovery has a clearer path back from menus, overlays, and rate switches. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive makes uneven games look steadier:** A target-aware placement clock spaces generated frames more evenly when source frame times vary. It keeps real frames mandatory, stays within the selected multiplier ceiling, and gives targets such as 60-to-90 and 80-to-120 cleaner camera movement with the potential for lower input lag and ghosting.
- **Smooth Cadence delivers Steady only when it can prove it:** On Gamescope SDR with refresh matched to the target, MAKO tests a constant 2x cadence instead of assuming it will work. It retains that path only after proving 98–102% target delivery, then returns to the normal cap or Fractional planner immediately if conditions change.
- **Ultra Performance cuts GPU work while retaining safety:** The restart-only profile policy uses 75% Flow Scale, the Lighter FG Model, FP16 permission when supported, and leaner active-policy resources. It can improve frame-generation performance by up to 30% in favourable GPU-limited scenes, while refresh, HDR, presentation safety, and recovery remain active. The trade-off is more visible frame-generation artifacts.
- **Recovery restores the right cadence after interruptions:** Native presentation now drains generated-image pressure, rebuilds temporal history, and resumes through a guarded zero-wait probe. Dynamic Cadence Recovery can distinguish a true fixed rate from a 30 ↔ 60 FPS menu or emulator transition, allowing Adaptive to recalibrate to its target and Fixed to follow confirmed Gamescope refresh. Its 0.1–3 second probe interval is per-game and opt-in; 2 seconds is the default and 0.1 seconds is deliberately aggressive.
- **Display-aware protection and lower overhead complete the path:** The refresh-rate guard can pause generation at or below a chosen Gamescope threshold and resume the selected mode above it, without changing the profile. Internally, explicit frame-plan ownership, allocation-free handoff, and structured diagnostics reduce normal-path work while retaining clear evidence for pacing, load shedding, recovery, and stabilization.
