## What's new in MAKO Decky v3.3.0

### Release codename: neptune-fury

> **Draft — unreleased.** Release validation, final copy, and banner artwork are pending.

<!-- Before publication: add the shared assets/neptune-fury.png banner to both component notes, record its provenance in ASSET_PROVENANCE.md, and remove the draft notice. -->

### A clearer panel

- **Hide info with R1:** Switch to a compact view that keeps settings and actions available while hiding explanations and optional information. MAKO Decky remembers your preference and preserves controller focus and scroll position as the panel changes.
- **Keep essential information visible:** Live Status, Lossless Scaling model warnings, and the release version and codename stay visible in the compact view.
- **Clearer section headings:** Centered titles and blue-to-white gradient underlines make the settings groups easier to distinguish. Advanced Details also gives values and code blocks more room.

### Clearer model warnings

- **LS1 and LSFG checks together:** A single warning near the top of the panel lists confirmed problems with the models needed by your enabled features, including an active LS1 fallback. It includes troubleshooting guidance and a link to check for MAKO Decky updates.
- **Separate installation guidance:** When Lossless Scaling is missing, the installation status explains what is needed without adding a model-failure warning.

### Scaling and external tools

- **Choose Gamescope WSI independently:** Enabling Scaling no longer forces the Gamescope WSI compatibility option on. The bundled Renderer supports scaling with WSI off on supported 64-bit and 32-bit Gamescope launches. The full WSI option remains a separate choice for supported 64-bit launches.
- **Clearer vkBasalt controls:** The toggle describes sharpening, anti-aliasing, and color adjustments without the experimental badge or a 64-bit-only description. vkBasalt requires a separate installation matching the game's process architecture; use WSI off for 32-bit games. MAKO's guide covers the integration requirements and links to vkBasalt's official documentation for effects and configuration.
- **Easier setup guidance:** Updated installation, update, launcher, and panel-display instructions explain how to activate MAKO and find the right settings.
