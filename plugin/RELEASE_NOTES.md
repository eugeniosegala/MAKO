## What's new in MAKO Decky v2.1.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyss-ascending.png" alt="Abyss Ascending: a Renaissance vision of the dormant MAKO beneath a Tuscan bay" width="100%">

### Release codename: Abyss Ascending

> _“Measure the tide before you command it; then even the abyss will carry you.”_
>
> **Aurelius, _Cardinal of the Ninth Tide_**

---

MAKO Decky’s Abyss Ascending release is a major Adaptive Frame Generation upgrade for Game Mode, focused on a more fluid and consistent experience. Fractional Adaptive handles uneven frame delivery more gracefully, Smooth Cadence only locks into a steady path when the game can truly sustain it, and the new controls make it easier to tune every game, display, and emulator safely. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive is steadier:** A new target-aware placement clock avoids uneven bursts when base frame times vary. Targets such as 60-to-90 and 80-to-120 get a more even camera pan while retaining more real frames for potentially lower input lag and ghosting.
- **Smooth Cadence earns a clean Steady 2x path:** When Target FPS matches Gamescope refresh, MAKO tests a constant cadence and keeps it only after the game proves it can sustain the target. If conditions change, it immediately returns to the normal Adaptive planner. Steady and Fractional are now deliberate per-game choices instead of a blind multiplier switch.
- **Ultra Performance can free meaningful GPU headroom:** This restart-only per-profile preset combines 75% Flow Scale, the Lighter FG Model, FP16 where supported, and leaner active-policy resources. In favourable GPU-limited scenes, it can improve frame-generation performance by up to 30%; it also increases visual artifacts, so it is best suited to constrained devices such as Steam Deck.
- **Dynamic Cadence Recovery helps emulators, menus, and overlays recover cleanly:** It detects native-rate switches such as 30 FPS gameplay to a 60 FPS menu, briefly exposes the real cadence, and returns to the correct Fixed or Adaptive behavior. It is opt-in per game because each check can briefly affect pacing. Choose a live 0.1–3 second interval: 2 seconds is the default, while 0.1 seconds is an aggressive emulator-focused option.
- **Frame generation can now follow your display:** Auto-disable Frame Generation by Refresh Rate pauses generation at or below a chosen Gamescope refresh threshold, then restores the selected mode when a faster display is detected. It is ideal for dock and display changes without overwriting the profile you tuned.
- **Per-game control is clearer and safer:** Runtime session updates keep live settings attached to the game that is running, preserve restart-only changes, and bring the refresh guard, matched processes, recovery, and Adaptive trade-offs together in one Decky experience.
