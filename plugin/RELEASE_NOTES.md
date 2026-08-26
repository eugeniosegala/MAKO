## What's new in MAKO Decky v2.2.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyss-ascending.png" alt="Abyss Ascending: a Renaissance vision of the dormant MAKO beneath a Tuscan bay" width="100%">

### Release codename: Abyss Ascending

> _“Follow the blue roads across the bay; even the abyss knows how to ascend.”_
>
> **Aurelius, _Cardinal of the Ninth Tide_**

---

MAKO Decky’s Abyss Ascending release adds independent LS1 neural scaling and a built-in open MAKO scaler alongside a major Adaptive Frame Generation upgrade for Game Mode. Scaling can reconstruct a lower-resolution game frame for direct presentation or run once before Fixed or Adaptive Frame Generation, while Fractional Adaptive handles uneven frame delivery more gracefully and Smooth Cadence only locks into a steady path when the game can truly sustain it. LS1 and LSFG use the user's licensed `Lossless.dll`; a scaling-only process still omits all LSFG interop.

- **Scaling is now a first-class per-game mode:** The new Spatial Scaling section provides an independent enable switch, a choice of LS1 Quality, LS1 Performance, or the open MAKO scaler, a 1.0x–2.0x factor, and sharpening/model-variant control. LS1 automatically uses MAKO and logs why if its licensed resources or translator are unavailable. Scaling is off by default, can run with Frame Generation on or off, retains its tuning when disabled, and applies its controls live through one debounced game-owned swapchain recreation. Enabling Frame Generation after a scaling-only process start still requires a restart.
- **Flow Scale and Lighter FG Model no longer require a routine restart:** Both controls now share the debounced live swapchain-recreation path, with a brief flicker while the game rebuilds the swapchain. Turning Ultra Performance itself on or off remains explicitly restart-bound.
- **Fixed and Adaptive multiplier growth now applies live:** Raising a multiplier beyond the current generated-frame capacity requests one brief game-owned swapchain recreation instead of leaving the change pending for restart. The locked HDR status row no longer carries a redundant restart suffix.
- **Fractional Adaptive is steadier:** A new target-aware placement clock avoids uneven bursts when base frame times vary. Targets such as 60-to-90 and 80-to-120 get a more even camera pan while retaining more real frames for potentially lower input lag and ghosting.
- **Smooth Cadence earns a clean Steady 2x path:** When Target FPS matches Gamescope refresh, MAKO tests a constant cadence and keeps it only after the game proves it can sustain the target. If conditions change, it immediately returns to the normal Adaptive planner. Steady and Fractional are now deliberate per-game choices instead of a blind multiplier switch.
- **Ultra Performance can free meaningful GPU headroom without freezing unrelated controls:** This process-start per-profile preset combines 75% Flow Scale, the Lighter FG Model, FP16 where supported, and leaner active-policy resources. Turning it on or off requires restart, but compatible settings remain live afterward. In favourable GPU-limited scenes, it can improve frame-generation performance by up to 30%; it also increases visual artifacts, so it is best suited to constrained devices such as Steam Deck.
- **Dynamic Cadence Recovery helps emulators, menus, and overlays recover cleanly:** It detects native-rate switches such as 30 FPS gameplay to a 60 FPS menu, briefly exposes the real cadence, and returns to the correct Fixed or Adaptive behavior. It is opt-in per game because each check can briefly affect pacing. Choose a live 0.1–3 second interval: 2 seconds is the default, while 0.1 seconds is an aggressive emulator-focused option.
- **Frame generation can now follow your display:** Auto-disable Frame Generation by Refresh Rate pauses generation at or below a chosen Gamescope refresh threshold, then restores the selected mode when a faster display is detected. It is ideal for dock and display changes without overwriting the profile you tuned.
- **Per-game control is clearer and safer:** Runtime session updates keep live settings attached to the game that is running, preserve restart-only changes, and bring the refresh guard, matched processes, recovery, and Adaptive trade-offs together in one Decky experience.
