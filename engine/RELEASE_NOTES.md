## What's new in MAKO Renderer v3.0.1

<picture>
  <source srcset="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/sea-rapture.webp" type="image/webp">
  <img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/sea-rapture.png" alt="Sea Rapture: an epic Renaissance-style pixel-art mako tearing through the ships attacking it as sailors fall into a storm-lit sea before a fortified city" width="100%">
</picture>

### Release codename: Sea Rapture

> _“The captains called it a hunt until the sea chose its champion.”_
>
> **Mira Valen, _Last Signal from the Blackwater Gate_**

---

### The big Spatial Scaling follow-up

Sea Rapture is a quick follow-up to the powerful foundation of MAKO 3.0, focused on making the new scaling pipeline faster, clearer, and more reliable.

- **Explicit startup compatibility:** The new restart-only **Game Swapchain Images** profile option preserves an affected game's requested swapchain minimum across replacements, with or without scaling and without lifecycle heuristics. MAKO keeps higher-performance generated-frame headroom by default and defers multiplier increases until the game recreates an undersized swapchain.
- **Improved scaling, monitoring, and Live Status:** Scaling now handles resolution changes and memory limits more safely, monitors available GPU memory headroom, and reports the real Input, Render, and Display resolutions plus any constrained scale factor.
- **Faster scaling with Frame Generation:** Leaner resource allocation and improved recovery reduce overhead when scaling and Frame Generation run together, increasing combined performance and overall stability.
