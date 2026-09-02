## What's new in MAKO Renderer v3.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/sea-rapture.png" alt="Sea Rapture: an epic Renaissance-style pixel-art mako battling sailing ships before a storm-lit fortified city" width="100%">

### Release codename: Sea Rapture

> _“The sea is never still, yet every current keeps its measure; learn that rhythm, and the horizon yields.”_
>
> **Ilyra Venn, _Navigator of the Radiant Deep_**

---

### The big Spatial Scaling follow-up

Sea Rapture is a quick follow-up to the powerful foundation of MAKO 3.0, focused on making the new scaling pipeline faster, clearer, and more reliable.

- **Fixed frame pacing and game compatibility:** Frame-pacing and swapchain fixes help games such as _Detroit: Become Human_, along with other titles using similar presentation patterns, run more reliably.
- **Improved scaling, monitoring, and Live Status:** Scaling now handles resolution changes and memory limits more safely, monitors available GPU memory headroom, and reports the real Input, Render, and Display resolutions plus any constrained scale factor.
- **Faster scaling with Frame Generation:** Leaner resource allocation and improved recovery reduce overhead when scaling and Frame Generation run together, increasing combined performance and overall stability.
