## What's new in MAKO Renderer v2.2.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/abyssal-fang.png" alt="Abyssal Fang: a Renaissance vision of a colossal mako rising beside a storm-dark fortified harbour" width="100%">

### Release codename: Abyssal Fang

> _“The abyss does not roar before it strikes; it shows one fang.”_
>
> **Cassian, _Hierophant of the Abyssal Forge_**

---

MAKO Renderer’s Abyssal Fang release combines spatial scaling and frame generation in one controlled Vulkan pipeline. Native Resolution, MAKO Scaler, LS1 Quality, and LS1 Performance can present independently or reconstruct each real frame before Fixed or Adaptive generation. The result is a broader feature set with explicit resource boundaries, live controls where the running process can support them, and safer recovery when games, displays, or compositors change state.

- **Spatial scaling is available alone or with Frame Generation:** Native Resolution provides a model-free linear baseline, MAKO Scaler supplies repository-owned single-pass reconstruction, and LS1 Quality or Performance uses the model from the user's licensed `Lossless.dll`. LS1 falls back to MAKO Scaler with a diagnostic reason when its licensed resources, translation, format, or pipeline are unavailable.
- **Provisioned controls change without disturbing game-owned presentation:** Frame Generation turns on or off live when startup construction succeeded. Scaling method and sharpness changes rebuild only MAKO's private scaler, leaving the game swapchain and Gamescope WSI object intact. Scale Factor waits for a natural resolution change or restart; Flow Scale, Lighter FG Model, and capacity growth use their safe game-owned recreation boundary.
- **Fixed and Adaptive generation scale from 2x to 5x:** Mode and multiplier changes apply within reserved capacity, while growth beyond it remains explicit and bounded. Live-safe scheduling changes continue applying even when another resource change is waiting.
- **Fractional Adaptive makes uneven games look steadier:** A target-aware placement clock spaces generated frames more evenly when source frame times vary. It keeps real frames mandatory, stays within the selected multiplier ceiling, and gives targets such as 60-to-90 and 80-to-120 cleaner camera movement with the potential for lower input lag and ghosting.
- **Smooth Cadence delivers Steady only when it can prove it:** On Gamescope SDR with refresh matched to the target, MAKO tests a constant 2x cadence instead of assuming it will work. It retains that path only after proving 98–102% target delivery, then returns to the normal cap or Fractional planner immediately if conditions change.
- **Ultra Performance cuts GPU work without freezing unrelated controls:** The process-start profile policy uses 75% Flow Scale, the Lighter FG Model, FP16 permission when supported, and leaner active-policy resources. Turning it on or off requires restart, while compatible scheduler and scaling controls remain live afterward. It can improve frame-generation performance by up to 30% in favourable GPU-limited scenes; the trade-off is more visible frame-generation artifacts.
- **Recovery restores the right cadence after interruptions:** Native presentation now drains generated-image pressure, rebuilds temporal history, and resumes through a guarded zero-wait probe. Dynamic Cadence Recovery can distinguish a true fixed rate from a 30 ↔ 60 FPS menu or emulator transition, allowing Adaptive to recalibrate to its target and Fixed to follow confirmed Gamescope refresh. Its 0.1–3 second probe interval is per-game and opt-in; 2 seconds is the default and 0.1 seconds is deliberately aggressive.
- **Display-aware protection and lower overhead complete the path:** The refresh-rate guard can pause generation at or below a chosen Gamescope threshold and resume the selected mode above it, without changing the profile. Internally, explicit frame-plan ownership, allocation-free handoff, and structured diagnostics reduce normal-path work while retaining clear evidence for pacing, load shedding, recovery, and stabilization.
