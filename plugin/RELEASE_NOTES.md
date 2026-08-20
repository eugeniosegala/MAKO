## What's new in MAKO Decky v2.0.0

![Leviathan Rising — a Renaissance vision of the MAKO Leviathan](https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png)

### Codename: Leviathan Rising

> **Command the Leviathan.**

MAKO Decky 2.0 brings the new Renderer directly into Game Mode across Steam Deck, Steam Machine, SteamOS, and Linux more broadly. Profiles remember each game, Adaptive Frame Generation responds while you play, and a reworked interface keeps the important controls close without exposing the machinery beneath the surface. Frame generation is available now; scaling is coming soon and is not part of this release.

### At your fingertips

- **A profile for every game:** Save a detected game or process once and MAKO selects its settings automatically on later launches. Unmatched games return to Default, repeated saves update the existing profile, and every change persists immediately—there is no Update Profile step.
- **Adaptive power, live:** Fixed Frame Generation from 2x to 4x and Adaptive Frame Generation now live together with Target FPS, FPS caps, multiplier ceiling, and Smooth Cadence. When a game becomes GPU- or compositor-bound, Adaptive reduces generated-frame work instead of holding a fixed interpolation load. That can improve performance and frame pacing; test it per game.
- **Clear restart boundaries:** Controls that can safely change during play update live. Settings that require new GPU resources are clearly marked restart-only, so the interface never promises a live transition the Renderer cannot safely make.
- **A premium, release-aware interface:** Profiles lead the workflow, while Frame Generation, Renderer options, installation status, Advanced Details, and Flatpak Setup share the same dark-teal visual system. A subtle `v2.0.0 - leviathan-rising` identity now appears at the top and is generated from the package version and release codename.
- **Simplified Chinese:** MAKO Decky now includes a complete Simplified Chinese translation and generates its language registry as part of the normal frontend build.

### One private path, more places to play

- **Steam, Heroic, EmuDeck, and compatible launchers:** MAKO's private `mako-run` wrapper and selected Flatpak extensions cover native Steam games, Proton, Heroic, supported emulators, Bazzite, and related SteamOS/Linux environments.
- **Flatpak compatibility across three generations:** Freedesktop 23.08, 24.08, and 25.08 receive matching 64-bit and 32-bit extensions and the same deterministic MAKO layer boundary, avoiding runtime-specific loader ordering.
- **Reliable swapchain ownership:** MAKO restores v2's proven private SDR layer directory and suppresses Gamescope WSI only inside MAKO-managed game processes. Steam's Vulkan Fossilize/overlay hooks, system-wide implicit presentation layers, and competing frame-generation layers are excluded so they cannot bypass MAKO's device or swapchain hooks. Gamescope and the Steam/Game Mode interface remain active outside the application layer chain, and game-local integrations such as OptiScaler are not removed. Use only one frame-generation implementation per game. No global Steam or Proton policy is changed.
- **Per-game compatibility controls:** Zink, ALSA, Steam Deck Mode, GPU and DLL overrides, HDR safety, the one-launch bypass, and renderer profile selection stay isolated to the chosen profile.
- **Upgrade-safe identity:** Existing installs keep the stable `Mako` Decky directory and **MAKO - Frame Generation** listing identity. Wrapper migrations refresh compatibility logic in place without renaming user-facing release tags or versioned assets.
- **Safer translated-host installation:** Renderer staging stays inside MAKO's user-owned data directory and uses deterministic file permissions, avoiding Armada/FEX's problematic translated `/tmp` boundary. The published package declares its x86_64 host requirement and refuses to install an incompatible layer on native AArch64 while preserving Armada's required game launcher for future validated native builds.

### Pinned, tested, and ready to steer

- **Exact Renderer pairing:** Every Decky release pins the Renderer archive, Flatpak bundle, source identity, and checksums it was tested with. The release flow publishes the Renderer first, records that immutable pin, and then packages MAKO Decky.
- **Focused regression coverage:** Backend tests exercise installation, configuration, profiles, wrappers, Flatpak preparation, migrations, and diagnostics. Frontend tests protect renderer installation, settings persistence, out-of-order profile loads, profile switching, default-profile safety, and RPC method names.
- **Native SteamOS validation:** The hardware gate builds and verifies both host architectures and all supported Flatpak runtimes, runs the AMD FP32 and FP16 visual regression, proves that the Vulkan loader activates MAKO, and then builds the complete Decky ZIP from the same source tree.
- **Cleaner development and diagnostics:** Local deployment can rebuild host or Flatpak components, refresh the frontend and Python backend, and reload only MAKO Decky. Presentation diagnostics are explicitly opt-in in development builds so synchronous log traffic cannot distort performance tests. Public lifecycle logs consistently use **MAKO Decky**, while the diagnostic helper remains compatible with historical records.

MAKO installs into its own private locations. It does not replace unrelated Vulkan layers, games, or Decky plugins.

**The Leviathan is awake. Take the helm.**
