## What's new in MAKO Renderer v1.1.0

- **Live configuration updates:** Supported Fixed and Adaptive settings can be reloaded while a game is running, with native presentation retained while the updated state settles.
- **Reliable process-profile fallback:** Games launched through Steam, Heroic, or EmuDeck can use the intended process profile without leaking MAKO activation or configuration into unrelated processes.
- **Host and Flatpak coverage:** The release contains genuine 64-bit and 32-bit host layers plus matching Freedesktop 23.08, 24.08, and 25.08 Flatpak extensions.
- **Safer frame delivery:** Recovery and transition handling keep real frames presenting when generated-frame resources are unavailable or temporarily busy.
- **Portable builds:** SteamOS-first packaging now reuses an isolated Qt toolchain and shared repository-local caches without requiring a container for normal releases.
