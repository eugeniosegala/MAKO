## What's new in MAKO Decky v2.1.0

![Leviathan Rising — a Renaissance vision of the MAKO Leviathan](https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png)

### Codename: Leviathan Rising

> **Command the Leviathan.**

MAKO Decky 2.1 brings the Renderer improvements into Game Mode with clearer Adaptive controls, steadier SteamOS presentation, safer compatibility handling, and a more polished everyday experience. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive becomes a real choice:** The controls now show that Adaptive can average between integer ratios—80 real FPS targeting 120 displayed FPS is 1.5x—and explain the tradeoff between flexible fractional generation and the newly clarified **Steady 2x FPS Cap**.
- **AMD image-quality work included:** The bundled Renderer carries the upstream robustness improvements aimed at reducing ghosting and edge corruption, with guarded activation on supported GPUs and the established fallback everywhere else.
- **Smoother SteamOS presentation:** MAKO's generated frames no longer compete with Gamescope's Vulkan WSI policy in the managed game chain, protecting base FPS while Gamescope, Game Mode, overlays, and Steam remain active.
- **Games keep presenting when generation cannot:** More initialization and recovery failures fall back to the game's original frames, while hitches, focus changes, overlays, swapchain recreation, and shutdown receive stronger cleanup.
- **Private Vulkan activation stays private:** Native, Proton, and supported Flatpak launches select the intended MAKO manifests without silently inviting competing presentation or frame-generation layers into the same chain.
- **MangoHud can show the final result:** The new guarded setup lets MangoHud sit after MAKO and report generated output FPS on the validated 64-bit SteamOS/RADV path, with full ordering and rollback guidance in the dedicated layer-chaining guide.
- **Controls say what actually happens:** Fractional targets, multiplier ceilings, live changes, and restart-only settings are explained directly. Flow Scale, Performance Mode, and GPU selection now carry clear restart guidance.
- **A cleaner Game Mode interface:** Spacing, section hierarchy, manual overrides, installation messaging, compatibility controls, and the Leviathan Rising release identity make the plugin feel calmer and easier to scan.
- **Full Simplified Chinese support:** Steam language names and locale variants now resolve consistently to the new complete translation.
- **Armada fails safe:** Native AArch64 rendering and incompatible Flatpak overrides remain disabled until a reproducible Renderer passes the hardware matrix, preserving the normal Armada/FEX game path.
- **Upgrades clean up after themselves:** Wrappers are regenerated from canonical profile state, and retired or unknown settings stay inert before disappearing on the next normal save.
- **Useful diagnostics, not log noise:** Focused collection tools expose activation, presentation, recovery, and layer ordering without enabling expensive frame-by-frame tracing during normal play.
- **A release tested like people use it:** Expanded SteamOS, AMD FP32/FP16, packaging, backend, frontend, and Vulkan-loader validation covers real hardware and runtime boundaries—not just portable unit tests.

**The Leviathan is awake. Take the helm.**
