## What's new in MAKO Renderer v4.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/inferno.png" alt="Inferno: a Renaissance-style pixel-art mako surges through a volcanic sea between lava-lit cities and ships beneath a moonlit infernal sky" width="100%">

### Release codename: inferno

> _“To strive, to seek, to find, and not to yield.”_
>
> **Alfred, Lord Tennyson, _Ulysses_**

---

<!-- Unreleased: complete release validation before publication. -->

MAKO Renderer 4.0 introduces a complete private shader platform, expanding the Renderer beyond Frame Generation and Spatial Scaling into a unified image-processing engine. Inferno also brings live Frame Generation execution, distinct VRR and fixed-refresh pacing ownership, transition-driven recovery, hardened Adaptive scheduling, and ordered Scaling plus Frame Generation.

- **Shaders become a native MAKO capability:** Native and Flatpak packages now include pinned 64-bit and 32-bit vkBasalt payloads, so shader support ships as one verified MAKO stack instead of relying on a system installation. `mako-launch` builds an isolated `MAKO Renderer → vkBasalt` chain with live reload for supported controls and restart boundaries for activation or custom advanced effects.
- **A complete effect range per game:** The private chain supports sharpening, anti-aliasing, DLS denoise, and MAKO's curated colour, SDR HDR-style contrast, cinematic, monochrome, retro, and finishing catalog while preserving editable advanced configuration.
- **Two pacing owners, selected seamlessly:** Explicit Gamescope VRR preference, capability, and active-state feedback selects MAKO's target clock for VRR or eligible FIFO pacing for fixed refresh. Live transitions preserve multiplier selection and Adaptive history instead of cold-starting generation or forcing one display model onto every device.
- **Frame Generation execution is now live:** Restart-bound provisioning is separate from execution, enabling an explicit `0x` pause in Decky and the desktop GUI without reallocating resources. Scaling-only processes can omit LSFG device interop and backend ownership altogether.
- **Recovery is driven by facts, not frame-rate heuristics:** Steam-menu focus and live policy changes restart temporal history, while bounded transport repair begins only when generated-image acquisition actually fails. Fixed and Adaptive no longer mistake low FPS, scaling or shader cost, game-scene changes, or slow successful presents for broken delivery.
- **Adaptive protects proven performance:** 2x–5x promotions compare adjacent measured workloads, naturally include scaling and post-processing cost, and retain the best sustainable lower result after a failed step instead of retrying from degraded gameplay.
- **Scaling plus Frame Generation stays ordered:** Combined processing retains lower FIFO delivery so generated and real presents reach Gamescope as distinct surface commits rather than being coalesced before presentation.
- **Live resolution changes stay inside proven memory limits:** Outputs within an established envelope can retry admission once after retired resources settle, without bypassing memory accounting. The desktop GUI also explains when Windowed mode is needed to create scaling headroom.
- **A first-class path beyond Decky:** The reviewed `mako-renderer-bin` recipe repackages the verified release archive for system-wide Arch Linux ownership while preserving payload hashes and leaving user-local MAKO installations untouched.
