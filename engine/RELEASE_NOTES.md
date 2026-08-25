## What's new in MAKO Renderer v2.1.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyss-ascending.png" alt="Abyss Ascending: a Renaissance vision of the dormant MAKO beneath a Tuscan bay" width="100%">

### Release codename: Abyss Ascending

> _“A moment's pause, and then the abyss stretched, beckoned. We all do.”_
>
> **Nylah, _Hour of Eternity_**

---

MAKO Renderer’s Abyss Ascending release is a major Adaptive and frame-pacing upgrade. It makes generated output chase the display target more intelligently, reduces needless work in the frame-generation hot path, and recovers from the cadence changes that used to make overlays, menus, emulators, and uneven scenes feel unpredictable. Frame generation is available now; scaling is coming soon.

- **Adaptive is now built around the target, not a fixed multiplier:** Fractional Adaptive places earned generated work against a display-clock phase, so combinations such as 60-to-90, 80-to-120, and 90-to-120 can pursue the requested output without treating every source interval alike. Real frames remain mandatory, generated work never exceeds the selected 2x–4x ceiling, and Steady remains available when a constant even cadence is the better choice.
- **AMD-informed pacing, without inventing frames or latency:** The new placement clock follows the same practical pacing concerns highlighted by AMD’s FidelityFX Frame Interpolation guidance: equal display time, precise presentation, stable base cadence, and VRR are separate problems that must cooperate. MAKO can move one already-earned output away from a short interval, repay it only when later capacity makes the spacing better, and discard impossible saturated work instead of creating a delayed catch-up burst.
- **Adaptive protects smoothness when the scene gets heavy:** Delivery, native cadence, target-clock pressure, and multiplier history now inform load shedding together. When the active plan cannot prove it is sustaining its target, MAKO tests a cheaper multiplier and holds the recovery until it is genuinely stable—rather than letting expensive generation prolong a hitch.
- **Dynamic Cadence Recovery understands 30 ↔ 60 transitions:** Optional per-profile recovery can distinguish a real 30 FPS game from a 60 FPS menu obscured by ordered presentation. It safely re-measures native cadence, keeps Adaptive aligned with its target, and lets Fixed mode follow confirmed Gamescope refresh while retaining the chosen multiplier as a ceiling. Live 0.25–3 second probe intervals let emulator users trade faster transition and audio recovery against more frequent checks without changing the conservative 2 second default.
- **Overlay and menu pressure recover without a stranded game:** Ordered generated-image pressure now quarantines synthetic work, presents native frames while the FIFO drains, warms temporal history, and resumes through a zero-wait, one-frame probe. A two-second healthy window is required before the full plan returns, preventing recovery from recreating the same 50 ms application-thread stall that caused it.
- **A faster, cleaner frame-generation hot path:** Generated-frame plans, Vulkan submission storage, and timestamp uploads avoid normal-path heap allocation, redundant copies, and unchanged uniform writes. Presentation diagnostics aggregate before one opt-in emission instead of repeatedly flushing on the application thread. The result is less CPU overhead around the work that matters most: scheduling, dispatching, and presenting frames.
- **Diagnosis now explains recovery, not just failure:** Structured records distinguish requested, admitted, scheduled, and delivered frames; target-clock state; load shedding; native drains; zero-wait probes; cadence recovery; and stabilization. Comparative trace capture keeps game evidence private and reproducible for later review.
