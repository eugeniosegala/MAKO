## What's new in MAKO Decky v3.4.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/unbroken-tide.png" alt="Unbroken Tide: a Renaissance-style pixel-art mako surging through an unbroken wave from a storm-dark fleet toward a sunlit fortified coast" width="100%">

### Release codename: unbroken-tide

> _“Come, my friends, ’T is not too late to seek a newer world.”_
>
> **Alfred, Lord Tennyson, _Ulysses_**

---

<!-- Unreleased: complete release validation before publication. -->

MAKO Decky 3.4 makes live menu transitions easier to understand and brings a more resilient MAKO Renderer recovery policy to both Fixed and Adaptive Frame Generation.

- **Clear menu policy:** Live Status always explains that Frame Generation is disabled while a Steam or Decky menu is open, without polling or writing a redundant live suspension flag. Real frames and active scaling continue, pre-menu generated-image recovery is discarded, and generation resumes with fresh history after returning to the game across Fixed and Adaptive modes.
- **One clean notice area:** Restart-pending, scaling, fallback, supersampling, memory-limit, and menu-policy messages now appear together at the bottom of Live Status. Multiple simultaneous messages use a compact bullet list.
- **More dependable recovery:** Fixed and Adaptive use the same pre-menu performance baseline and guarded post-menu repair where applicable. Recovery remains tied to confirmed menu and presentation evidence rather than ordinary gameplay FPS changes.
- **More stable generated FPS:** The shared Adaptive and Fixed Dynamic Cadence scheduler preserves a proven multiplier during confirmed Steam-menu and Fractional/Steady recovery, with bounded direct-transport repair that avoids repeated native/multiplied oscillation. Adaptive also compares a post-menu higher-multiplier probe with a target-proven pre-menu level, preventing temporary return slowdown from locking in a more expensive cadence. Ordinary gameplay retains the 3.3 timeout, cadence-history, and recreation policy.
- **Better resolution-change handling:** When a live resolution change temporarily reports insufficient GPU memory despite fitting a previously proven output, MAKO retries admission once after the old context has settled. A genuine memory limit remains enforced.
- **Clearer scaling guidance:** Scaling controls and Live Status suggest Windowed mode when fullscreen or borderless keeps the game input at the display size, alongside the existing lower-resolution and Quality Supersampling options. The guidance is updated across supported languages and in the desktop GUI.
- **Per-game vkBasalt shaders:** The new experimental **Shaders** section controls the bundled private vkBasalt layer with compact sharpening, denoise, and anti-aliasing options. The Default profile merges MAKO-owned values into the standard global file, while saved profiles receive isolated, compactly identified files whose advanced manual options are preserved and whose exact paths are shown in the UI.
- **Focused image-processing tabs:** Frame Generation, Spatial Settings, and Shaders now share a compact three-tab **Image Processing** strip with hover and controller-focus ribbon labels. Performance and advanced controls remain in their established sections below it.
