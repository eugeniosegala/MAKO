## What's new in MAKO Renderer v3.1.0

<picture>
  <source srcset="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/sea-rapture.webp" type="image/webp">
  <img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/sea-rapture.png" alt="Sea Rapture: an epic Renaissance-style pixel-art mako tearing through the ships attacking it as sailors fall into a storm-lit sea before a fortified city" width="100%">
</picture>

### Release codename: Sea Rapture

> _“Light the harbour, not for the hunters, but for those the tide returns.”_
>
> **Cael Orsino, _Keeper of the Rapture Light_**

---

### The big Spatial Scaling follow-up

Sea Rapture is MAKO 3.1: a focused follow-up that makes Spatial Scaling faster, clearer, and more resilient across changing resolutions, demanding workloads, and different game swapchain behaviours.

- **Clear per-game startup compatibility:** For affected titles such as _Detroit: Become Human_, the new restart-only **Game Swapchain Images** profile option preserves the game's requested swapchain minimum, while MAKO keeps faster generated-frame headroom enabled by default for other games.
- **Smoother Frame Generation changes:** Multiplier increases wait for a game-owned swapchain recreation when necessary, avoiding repeated timeout recovery and preserving steadier presentation cadence.
- **Safer scaling transitions:** Resolution changes, supersampling, and GPU-memory constraints now rebuild or fall back more reliably without leaving stale limits behind.
- **More useful Live Status:** See the real **Input**, **Render**, and **Display** resolutions, available memory headroom, and any constrained scale factor.
- **Faster combined performance:** Leaner resource allocation and improved recovery reduce overhead when Spatial Scaling and Frame Generation run together.
