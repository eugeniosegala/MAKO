## What's new in MAKO Decky v3.4.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/unbroken-tide.png" alt="Unbroken Tide: a Renaissance-style pixel-art mako surging through an unbroken wave from a storm-dark fleet toward a sunlit fortified coast" width="100%">

### Release codename: unbroken-tide

> _“Come, my friends, ’T is not too late to seek a newer world.”_
>
> **Alfred, Lord Tennyson, _Ulysses_**

---

<!-- Unreleased: complete release validation before publication. -->

MAKO Decky 3.4 makes live menu transitions easier to understand and brings a more resilient MAKO Renderer recovery policy to both Fixed and Adaptive Frame Generation.

- **Independent Frame Generation provisioning:** **Frame Generation (Restart)** is now the process-start control, while the factor selector exposes a live `0x` pause alongside 2x–5x in Fixed mode and alongside the active Adaptive mode. Existing Off profiles retain that state as `0x`. Scaling-only profiles keep reconstruction without LSFG interop or backend resources, and Shaders-only profiles omit the MAKO Renderer layer entirely.
- **Clear menu policy:** Live Status always explains that Frame Generation is disabled while a Steam or Decky menu is open, without polling or writing a redundant live suspension flag. Real frames and active scaling continue, pre-menu generated-image recovery is discarded, and generation resumes with fresh history after returning to the game across Fixed and Adaptive modes.
- **One clean notice area:** Restart-pending, scaling, fallback, supersampling, memory-limit, and menu-policy messages now appear together at the bottom of Live Status. Multiple simultaneous messages use a compact bullet list.
- **More dependable recovery:** Fixed and Adaptive accept recreation authority only from confirmed menu or live-policy transitions followed by an actual generated-image acquire failure. Ordinary gameplay FPS, game-scene changes, scaling cost, shader cost, and slow successful presents cannot start transport repair or request a swapchain recreation.
- **More stable generated FPS:** Adaptive 2x–5x promotions use one measured adjacent-workload rule, retain the best proven lower-load baseline after failure, and cannot retry against degraded gameplay. Fixed keeps the selected multiplier without a cadence-collapse probe; both modes retain bounded retry after explicit Vulkan acquire failures.
- **Better resolution-change handling:** When a live resolution change temporarily reports insufficient GPU memory despite fitting a previously proven output, MAKO retries admission once after the old context has settled. A genuine memory limit remains enforced.
- **Clearer scaling guidance:** Scaling controls and Live Status suggest Windowed mode when fullscreen or borderless keeps the game input at the display size, alongside the existing lower-resolution and Quality Supersampling options. The guidance is updated across supported languages and in the desktop GUI.
- **Per-game vkBasalt shaders:** The new experimental **Shaders** section controls the bundled private vkBasalt layer with compact sharpening, denoise, and anti-aliasing options. The Default profile merges MAKO-owned values into the standard global file, while saved profiles receive isolated, compactly identified files whose advanced manual options are preserved and whose exact paths are shown in the UI.
- **Live vkBasalt tuning:** Sharpening type, anti-aliasing, sharpness, and DLS denoise now update in a running game when MAKO's bundled vkBasalt layer is active. Layer activation and manual advanced changes remain restart-bound.
- **Focused image-processing tabs:** Frame-gen, Scaling, and Shaders now share a compact three-tab **Image Processing** strip with hover and controller-focus ribbon labels. Performance and advanced controls remain in their established sections below it.
