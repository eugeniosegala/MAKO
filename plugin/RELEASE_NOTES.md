## What's new in MAKO Decky v3.0.1

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/sea-rapture.png" alt="Sea Rapture: an epic Renaissance-style pixel-art mako battling sailing ships before a storm-lit fortified city" width="100%">

### Release codename: Sea Rapture

> _“When the tide changes, raise the lantern higher; a clear signal turns rough water into a passage.”_
>
> **Cael Orsino, _Keeper of the Rapture Light_**

---

### The big Spatial Scaling follow-up

Sea Rapture is a quick follow-up to the powerful foundation of MAKO 3.0, focused on making the new scaling pipeline faster, clearer, and more reliable.

- **Fixed frame pacing and game compatibility:** Frame-pacing and swapchain fixes help games such as _Detroit: Become Human_, along with other titles using similar presentation patterns, run more reliably. Increasing the Frame Generation multiplier now also waits for a game-owned swapchain recreation when the existing WSI image count lacks room for the larger generated batch, avoiding repeated generated-image timeout recovery during fast motion.
- **Improved scaling, monitoring, and Live Status:** Scaling now handles resolution changes and memory limits more safely, monitors available GPU memory headroom, and reports the real Input, Render, and Display resolutions plus any constrained scale factor.
- **Faster scaling with Frame Generation:** Leaner resource allocation and improved recovery reduce overhead when scaling and Frame Generation run together, increasing combined performance and overall stability.
