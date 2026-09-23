## What's new in MAKO Renderer v4.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/inferno.png" alt="Inferno: a Renaissance-style pixel-art mako surges through a volcanic sea between lava-lit cities and ships beneath a moonlit infernal sky" width="100%">

### Release codename: inferno

> _“To strive, to seek, to find, and not to yield.”_
>
> **Alfred, Lord Tennyson, _Ulysses_**

---

<!-- Unreleased: complete release validation before publication. -->

MAKO Renderer 4.0 adds live Frame Generation execution control, transition-driven recovery, VRR-aware pacing, and private shader integration.

- **Live Frame Generation execution:** Restart-bound provisioning is separate from the live execution state, enabling an explicit `0x` pause in Decky and the desktop GUI without reallocating resources. Scaling-only processes can omit LSFG device interop and backend ownership.
- **Transition-driven recovery:** Steam-menu focus and live policy changes restart temporal history and authorize bounded repair only when a generated-image acquire actually fails. Fixed and Adaptive no longer treat low FPS, scaling or shader cost, game-scene changes, or slow successful presents as recovery signals.
- **Stable Adaptive workloads:** 2x–5x promotions compare adjacent measured workloads, include scaling and post-processing cost naturally, and retain the best proven lower-load result after failure instead of retrying against degraded gameplay.
- **VRR-aware Gamescope pacing:** Explicit Gamescope VRR preference, capability, and active-state feedback now selects between fixed-refresh FIFO pacing and MAKO's target clock without changing multiplier selection or recovery policy.
- **Ordered scaled delivery:** Combined Scaling and Frame Generation retains lower FIFO delivery so generated and real presents cannot be coalesced before Gamescope receives distinct surface commits.
- **Safer live resolution changes:** Outputs inside a previously proven envelope can retry admission once after retired resources settle, without bypassing the memory limit. The desktop GUI also explains when Windowed mode is needed to create scaling headroom.
- **Private vkBasalt integration:** Native and Flatpak packages include pinned 64-bit and 32-bit vkBasalt payloads. `mako-launch` can select an isolated `MAKO Renderer → vkBasalt` chain, supported controls reload live, and activation or custom advanced effects remain restart-bound.
- **Arch Linux packaging:** The reviewed `mako-renderer-bin` recipe repackages the verified release archive for system-wide pacman ownership while preserving payload hashes and leaving user-local MAKO installations untouched.
