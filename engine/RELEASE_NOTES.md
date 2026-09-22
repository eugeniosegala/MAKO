## What's new in MAKO Renderer v3.4.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/unbroken-tide.png" alt="Unbroken Tide: a Renaissance-style pixel-art mako surging through an unbroken wave from a storm-dark fleet toward a sunlit fortified coast" width="100%">

### Release codename: unbroken-tide

> _“To strive, to seek, to find, and not to yield.”_
>
> **Alfred, Lord Tennyson, _Ulysses_**

---

<!-- Unreleased: complete release validation before publication. -->

MAKO Renderer 3.4 makes live Steam-menu transitions and resolution changes recover predictably without turning ordinary gameplay slowdowns into recovery triggers.

- **Restart-bound Frame Generation provisioning:** Profiles can omit LSFG device interop and backend ownership while retaining Scaling in the combined Renderer. The separate execution state remains live and powers the explicit `0x` factor in both Decky and the desktop GUI without reallocating process-start resources, including while Adaptive remains selected.
- **Menu-aware Frame Generation:** Confirmed Gamescope focus pauses generated frames while a Steam or Decky menu owns input, while real frames and active scaling continue. Entering the menu discards incomplete generated-image transport recovery, and closing it restarts temporal history before generation resumes across Fixed and Adaptive modes.
- **Consistent Fixed and Adaptive recovery:** Both modes now accept recovery authority only from explicit Steam-menu or live-policy transitions and direct transport failures. Ordinary gameplay FPS, game-scene changes, scaling cost, and post-processing cost cannot start transport recovery or request recreation.
- **Deterministic repair for stuck presentation:** A confirmed Steam-menu return or live generation-policy transition arms one repair permission. A successful generated-image acquire consumes it harmlessly; only an actual acquire timeout can instead request one retirement-protected swapchain recreation. Slow successful presents, low FPS, failure counts, and timed recovery windows have no authority.
- **Stable multiplier selection:** Adaptive 2x–5x promotions use one adjacent-workload rule: target-capped displayed gain must pay for the real-FPS cost measured in the same probe. Failed levels retain the best proven lower-load baseline and cannot be retried against a degraded replacement. The old 2x-to-3x bridge, 2x-only hitch exception, percentage-based promotion boundaries, continuous gameplay load shedding, and Fixed cadence-collapse probe have been removed.
- **Safer Scaling with Frame Generation:** Scaling and post-processing are naturally included in the measured cost of an Adaptive workload promotion, without GPU-specific utilization or cadence thresholds. Combined contexts rely on explicit transitions, actual Vulkan acquire outcomes, or natural application recreation rather than an FPS- or present-duration-triggered rebuild.
- **More reliable resolution changes:** A replacement rejected only by a transient live-memory estimate can recheck admission once when the requested output remains inside the surface's previously proven envelope. The retry never bypasses the memory limit or grows the output.
- **Clearer scaling setup:** The desktop GUI now explains that some games need Windowed mode because fullscreen or borderless can keep the input at the display size, leaving no room to upscale.
- **Private vkBasalt integration:** Native and Flatpak Renderer packages now include pinned 64-bit and 32-bit vkBasalt payloads. The standalone launcher can opt into the isolated `MAKO Renderer → vkBasalt` chain without using a system installation, while retaining an optional standard vkBasalt configuration file for advanced effects. FXAA, SMAA, CAS, DLS, sharpening strength, and DLS denoise changes reload live; layer activation and custom/advanced effects remain restart-bound.
