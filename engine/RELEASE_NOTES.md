## What's new in MAKO Renderer v3.0.1

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/sea-rapture.png" alt="Sea Rapture: an epic Renaissance-style pixel-art mako battling sailing ships before a storm-lit fortified city" width="100%">

### Release codename: Sea Rapture

> _“The sea is never still, yet every current keeps its measure; learn that rhythm, and the horizon yields.”_
>
> **Ilyra Venn, _Navigator of the Radiant Deep_**

---

### The big Spatial Scaling follow-up

Sea Rapture is a quick follow-up to the powerful foundation of MAKO 3.0, focused on making the new scaling pipeline faster, clearer, and more reliable.

- **Explicit startup compatibility without heuristics:** The new restart-only **Game Swapchain Images** profile option preserves an affected game's requested swapchain minimum from its first creation through every replacement, with Frame Generation alone or combined with scaling. The choice no longer depends on executable, Proton-version, or swapchain-lifecycle detection, while the default path retains its higher-performance generated-frame headroom. Increasing the Frame Generation multiplier also waits for a game-owned swapchain recreation when the existing image pool cannot hold the larger generated batch, avoiding repeated timeout recovery during fast motion.
- **Improved scaling, monitoring, and Live Status:** Scaling now handles resolution changes and memory limits more safely, monitors available GPU memory headroom, and reports the real Input, Render, and Display resolutions plus any constrained scale factor.
- **Faster scaling with Frame Generation:** Leaner resource allocation and improved recovery reduce overhead when scaling and Frame Generation run together, increasing combined performance and overall stability.
