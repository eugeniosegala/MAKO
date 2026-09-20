## What's new in MAKO Decky v3.4.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/unbroken-tide.png" alt="Unbroken Tide: a Renaissance-style pixel-art mako surging through an unbroken wave from a storm-dark fleet toward a sunlit fortified coast" width="100%">

### Release codename: unbroken-tide

> _“Come, my friends, ’T is not too late to seek a newer world.”_
>
> **Alfred, Lord Tennyson, _Ulysses_**

---

<!-- Unreleased: complete release validation before publication. -->

MAKO Decky 3.4 makes live menu transitions easier to understand and brings a more resilient MAKO Renderer recovery policy to both Fixed and Adaptive Frame Generation.

- **Clear menu status:** Live Status shows when Frame Generation is temporarily disabled because a Steam or Decky menu is open. Real frames and active scaling continue, and generation resumes with fresh history after returning to the game.
- **One clean notice area:** Restart-pending, scaling, fallback, supersampling, memory-limit, and menu-suspension messages now appear together at the bottom of Live Status. Multiple simultaneous messages use a compact bullet list.
- **More dependable recovery:** Fixed and Adaptive use the same pre-menu performance baseline and guarded post-menu repair where applicable. Recovery remains tied to confirmed menu and presentation evidence rather than ordinary gameplay FPS changes.
- **More stable generated FPS:** Adaptive preserves a proven multiplier through transport recovery and gives Steam-menu returns and Fractional/Steady changes bounded direct-transport repair without repeatedly oscillating between native and multiplied output. Ordinary gameplay retains the 3.3 multiplier and recreation policy.
- **Better resolution-change handling:** When a live resolution change temporarily reports insufficient GPU memory despite fitting a previously proven output, MAKO retries admission once after the old context has settled. A genuine memory limit remains enforced.
- **Clearer scaling guidance:** Scaling controls and Live Status suggest Windowed mode when fullscreen or borderless keeps the game input at the display size, alongside the existing lower-resolution and Quality Supersampling options. The guidance is updated across supported languages and in the desktop GUI.
