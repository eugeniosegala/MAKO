## What's new in MAKO Renderer v4.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/inferno.png" alt="Inferno release artwork" width="100%">

### Release codename: inferno

<!-- Unreleased: complete release validation before publication. -->

Shader processing is the defining addition in MAKO Renderer 4.0 and the project's largest expansion beyond Frame Generation and Spatial Scaling. MAKO now ships its own maintained and modified fork of vkBasalt for 64-bit and 32-bit games across native Linux and Flatpak. The fork provides controlled live effect updates and a managed layer chain while retaining standard vkBasalt configuration for advanced chains and additional ReShade-compatible effects. Frame Generation, Scaling, and programmable post-processing are now delivered as one managed graphics stack.

- **MAKO's vkBasalt fork:** Native and Flatpak packages include pinned builds of MAKO's fork and do not depend on a system-wide vkBasalt installation. It monitors managed configuration changes, replaces effect graphs without retaining previous graphs in GPU memory, and supports live updates for MAKO's controls. `mako-launch` creates an isolated layer chain with MAKO Renderer before vkBasalt.
- **Built-in and custom effects:** Profiles include sharpening, anti-aliasing, DLS denoise, and curated colour, HDR-style contrast for SDR, cinematic, monochrome, retro, and finishing effects. Standard vkBasalt configuration remains editable, allowing custom chains and manually added ReShade effects to load on the next game launch.
- **Display-aware pacing:** Gamescope's VRR preference, capability, and active state determine whether MAKO uses target-clock pacing for VRR or FIFO pacing for eligible fixed-refresh configurations. Live changes preserve the selected multiplier and current Adaptive level.
- **Live Frame Generation pause:** Frame Generation resources are provisioned at process start, but execution can be paused and resumed with the `0x` setting in MAKO Decky or the desktop GUI without reallocating those resources. Scaling-only processes no longer initialize LSFG device interop or backend resources.
- **Recovery behavior:** Steam-menu focus changes and live policy changes reset temporal history. MAKO attempts presentation recovery only when Vulkan fails to acquire a generated image. Low frame rates, expensive scaling or shader work, scene changes, and slow successful presents do not trigger recovery.
- **Adaptive scheduling:** Promotions from 2x through 5x compare adjacent measured workloads, including scaling and post-processing cost. If a higher level is not sustainable, MAKO retains the lower measured level instead of repeatedly testing the failed level.
- **Combined Scaling and Frame Generation:** FIFO delivery keeps generated and real frames as separate Gamescope surface commits when both features are enabled.
- **Resolution changes:** When a live resolution change remains within the existing memory limit, MAKO can retry resource allocation once after retired resources have been released. The desktop GUI now explains when Windowed mode is required to provide scaling headroom.
- **Arch Linux package:** The `mako-renderer-bin` recipe installs the verified release archive system-wide, preserves payload hashes, and does not modify user-local MAKO installations.
