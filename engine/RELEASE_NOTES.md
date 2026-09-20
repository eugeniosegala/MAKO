## What's new in MAKO Renderer v3.4.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/unbroken-tide.png" alt="Unbroken Tide: a Renaissance-style pixel-art mako surging through an unbroken wave from a storm-dark fleet toward a sunlit fortified coast" width="100%">

### Release codename: unbroken-tide

> _“To strive, to seek, to find, and not to yield.”_
>
> **Alfred, Lord Tennyson, _Ulysses_**

---

<!-- Unreleased: complete release validation before publication. -->

MAKO Renderer 3.4 makes live Steam-menu transitions and resolution changes recover predictably without turning ordinary gameplay slowdowns into recovery triggers.

- **Menu-aware Frame Generation:** Confirmed Gamescope focus pauses generated frames while a Steam or Decky menu owns input, while real frames and active scaling continue. Closing the menu restarts temporal history before generation resumes.
- **Consistent Fixed and Adaptive recovery:** Both modes compare post-menu delivery with the same lightweight pre-menu performance baseline. Adaptive uses its configured target and Fixed uses confirmed display refresh, while mode changes invalidate old evidence.
- **Stronger repair for stuck presentation:** Repeated generated-image acquisition failures after a confirmed menu return can request one retirement-protected swapchain recreation after bounded in-place recovery. Ordinary low FPS without the menu event or transport-failure evidence cannot activate it.
- **Stable multiplier recovery:** Adaptive preserves its last proven generated-frame level through transport recovery instead of repeatedly jumping between native and high-multiplier delivery. Fixed retains its separate cadence probe while sharing the same final recovery safeguards.
- **Safer Scaling with Frame Generation:** Combined contexts recover generation policy and history in place first. A persistent event-backed deficit may request one guarded recreation only when memory admission allows it, avoiding rebuild loops on an already pressured GPU.
- **More reliable resolution changes:** A replacement rejected only by a transient live-memory estimate can recheck admission once when the requested output remains inside the surface's previously proven envelope. The retry never bypasses the memory limit or grows the output.
- **Clearer scaling setup:** The desktop GUI now explains that some games need Windowed mode because fullscreen or borderless can keep the input at the display size, leaving no room to upscale.
