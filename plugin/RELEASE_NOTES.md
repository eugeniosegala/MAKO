## What's new in MAKO Decky v2.1.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyss-ascending.png" alt="Abyss Ascending: a Renaissance vision of the dormant MAKO beneath a Tuscan bay" width="100%">

### Release codename: Abyss Ascending

> _“A moment's pause, and then the abyss stretched, beckoned. We all do.”_
>
> **Nylah, _Hour of Eternity_**

---

MAKO Decky’s Abyss Ascending release makes Adaptive Frame Generation dramatically more capable in Game Mode. It gives players a better reason to use high targets, Fractional Adaptive, and per-game tuning: the bundled Renderer now pursues smooth delivery more intelligently, while Decky makes those controls safe, understandable, and specific to each game. Frame generation is available now; scaling is coming soon.

- **A real upgrade for high-target Adaptive play:** The bundled Renderer now uses a target-aware placement clock, sustained-load protection, and AMD-informed pacing principles to make 90, 120, and fractional targets more deliberate. Rather than blindly pushing a multiplier, it balances real and generated frames around the selected display rate and sheds unnecessary generation when the game cannot sustain it.
- **Fractional Adaptive is now a performance and pacing tool:** Pick it for targets between integer ratios—such as 60-to-90 or 80-to-120—when you want more real frames than a forced high multiplier can preserve. Decky keeps Steady as the smooth, even-cadence baseline, makes the trade-off clear, and gives each game its own saved choice instead of one global compromise.
- **Smooth Cadence now makes an evidence-based 2x choice:** When Target FPS matches the Gamescope refresh, the bundled Renderer can test whether a nearby constant 2x cadence actually reaches the target, then use the same ordered-FIFO pacing ownership that makes Fixed 2x feel clean. It keeps the smoother path only while delivery remains healthy, restores the normal Steady cap or Fractional planner immediately when it does not, and backs off before trying again.
- **Made for emulators and games that change speed:** Dynamic Cadence Recovery is a per-profile compatibility option for 30 FPS gameplay ↔ 60 FPS menus, including Vulkan emulators. It safely samples native presentation, then keeps Adaptive synced to Target FPS or lets Fixed follow confirmed Gamescope refresh. Live 0.25–3 second probe intervals let you trade faster transition and audio recovery against more frequent checks; the conservative default remains 2 seconds, and the option stays off unless a game needs it.
- **Tune while playing, without state drift:** Target, ceiling, cadence, recovery, and live frame-generation changes flow through the active game session and its canonical profile together. Decky applies what can safely change now and clearly marks settings that need a natural swapchain recreation or game restart.
- **Menus and overlays no longer have to mean a restart:** The bundled Renderer can drain ordered presentation pressure with native frames, re-establish temporal history, and return through a zero-wait one-frame probe before restoring the full plan. Decky’s recovery diagnostics surface each phase, so a difficult game can be tuned with evidence instead of guesswork.
- **Profiles, installation, and support are ready for serious testing:** Canonical per-game profiles, managed host and Flatpak setup, typed runtime contracts, machine-filterable diagnostics, and controlled private trace capture give players and maintainers a safer path from “this feels wrong” to a useful performance report—without publishing personal logs.
