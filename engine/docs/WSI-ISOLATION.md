# Gamescope WSI isolation

This guide defines MAKO Renderer's Vulkan-layer discovery and presentation ownership. [HDR pipeline architecture](HDR-PIPELINE.md) owns colour policy; [runtime configuration transitions](RUNTIME-TRANSITIONS.md) owns swapchain replacement and retirement.

## Compositor and Vulkan layer

Gamescope has two separate roles:

- The **Gamescope compositor** owns the display session, Game Mode UI, focus, refresh information, composition, and scanout. It remains active.
- The **Gamescope WSI Vulkan layer** joins the application's Vulkan dispatch chain and changes swapchain and presentation behavior. MAKO excludes it from ordinary managed launches.

Disabling Gamescope WSI does not disable Gamescope, Steam, or Game Mode. It changes only the Vulkan layers visible inside the launched process.

## Why presentation needs one owner

MAKO expands one application present into generated present(s) followed by the real frame. If Gamescope WSI sits above a single combined MAKO layer, it observes only the application's original call while MAKO injects additional calls below it. Gamescope WSI therefore cannot pace every delivered image.

The managed scaling path uses this explicit order:

```text
Application / Proton translation
    -> VK_LAYER_MAKO_render
       reconstruction + Frame Generation
    -> VK_LAYER_FROG_gamescope_wsi_x86_64
       sees every generated and real present
    -> VK_LAYER_MAKO_spatial_scaling
       capabilities + physical lower extent
    -> Vulkan driver
    -> Gamescope compositor
```

The lower MAKO role performs no presentation-time GPU work. Reconstruction stays in the upper role so there is one resource and scheduling owner. An FG-only Gamescope WSI compatibility launch uses the upper MAKO role followed by WSI without the lower spatial role.

## Default managed launch

Standalone `mako-launch` and MAKO Decky's generated wrapper establish the normal boundary before `vkCreateInstance`:

```text
VK_IMPLICIT_LAYER_PATH=<private MAKO manifest directory>
VK_ADD_IMPLICIT_LAYER_PATH unset
ENABLE_MAKO=1
DISABLE_LSFG=1
DISABLE_LSFGVK=1
DISABLE_GAMESCOPE_WSI=1
ENABLE_GAMESCOPE_WSI unset
MAKO_DISABLE_HDR_EXPOSURE=1
DXVK_HDR unset
```

The host manifest directory is `<prefix>/share/mako-render/vulkan/implicit_layer.d`; MAKO Decky uses the matching user-local path. Published host packages contain 64-bit and 32-bit MAKO manifests. A prepared Flatpak uses the matching MAKO runtime extension and bounded application overrides rather than the host launcher.

The private path prevents inherited implicit layers from joining accidentally. Known competing frame-generation layers and the Gamescope HDR bridge are also disabled explicitly. Layer membership cannot be repaired after Vulkan starts, so launchers, manifests, packages, wrapper generation, and Flatpak setup all enforce the same contract.

## Guarded Gamescope WSI paths

MAKO Decky admits a staged 64-bit Gamescope WSI payload in two cases:

- Scaling was enabled before launch, which requires the complete three-role split.
- The per-profile **Gamescope WSI (Restart)** compatibility option was enabled for an FG-only game.

The option is independent from the mutually exclusive MangoHud/vkBasalt selection, and both WSI paths remain SDR-only. MAKO Decky validates the host manifest identity, library, architecture, and activation gates, then copies only that payload into its managed compatibility directory. It never exposes the full host implicit-layer directory.

Eligibility follows Gamescope WSI's active-session boundary: `GAMESCOPE_WAYLAND_DISPLAY` must be nonempty, and `WAYLAND_DISPLAY` must be empty or equal to it. Desktop Mode, a mismatched nested Wayland session, invalid staged files, or missing lower-role evidence fails closed to top-only MAKO; scaling stays inactive rather than loading a partial chain.

The managed wrapper sets the semantic prefix through `VK_INSTANCE_LAYERS`; manifest-directory enumeration does not define order. When scaling is active, the order is render role, Gamescope WSI, spatial role, then any selected post-process tool. Caller-requested layers follow that managed prefix. See [Optional graphics integrations](LAYER-CHAINING.md) for supported exceptions.

Prepared 64-bit Heroic and EmuDeck Flatpak launches can receive the same bounded WSI payload and MAKO extension. Unprepared Flatpaks, 32-bit WSI presentation, HDR, broader host layouts, and other sandboxes are separate validation boundaries.

## Scaling ownership proof

Gamescope WSI may replace the surface handle between upper and lower roles. Fixed-surface capabilities and create decisions therefore cross the two MAKO DSOs through same-thread, one-shot relays tied to the physical device and request. The lower decision is authoritative: a valid source/presentation split activates scaling, while an equal-extent decision creates a native context. Missing, stale, or mismatched evidence rolls back the create.

Call order alone is insufficient. The lower spatial role must observe a surface created through `vkCreateWaylandSurfaceKHR`, proving that Gamescope WSI converted the application's X11 window into the compositor-owned surface. An XCB or Xlib surface at that boundary remains native with `inactive_reason=gamescope-wsi-surface-unproven`; the upper role can still retain Frame Generation. Direct combined Renderer operation has no intervening WSI owner and does not require this split-only proof.

For variable surfaces, managed scaling also requires the positively identified Gamescope output target. A missing target or a source with no enlargement headroom remains native. [Spatial scaling architecture](SCALING.md) owns the full fixed/variable extent policy.

## Presentation and swapchain policy

`PresentationEnvironmentPolicy` resolves WSI isolation and HDR exposure once at process start. `selectPresentationTransport()` then selects one immutable swapchain transport:

- `OrderedSdr` for the supported managed SDR path.
- `GamescopeHdr` only for the experimental combination of Gamescope, WSI, allowed HDR exposure, and an HDR-capable swapchain.

Current launchers select `OrderedSdr`. It owns FIFO ordering for generated and real frames and removes Gamescope's incompatible present-mode override from the lower create chain. Unknown create-chain prefixes fail before driver creation rather than mutating caller-owned data.

Frame Generation normally reserves lower WSI images for the largest configured generated batch. FG-only ordered presentation also requests its established relief headroom; combined scaling uses a smaller bounded topology for compatibility. Surface limits remain authoritative, and an initialization or memory failure retries once with the application's original minimum.

When the returned pool has no safe spare image, MAKO admits generated images before backend work without blocking the real frame. Adaptive can retain a smaller proven generated capacity for that swapchain; Fixed keeps its explicit multiplier and may skip unavailable output under pressure.

**Game Swapchain Images (Restart)** is the per-profile escape hatch for games that reject MAKO's normal reservation. It preserves the application's requested minimum from the first managed create and every replacement. It is off by default, process-static, and never enabled by executable or runtime heuristics. With no reserved headroom, synthetic output can be skipped when the compositor has no free image.

Changes that need a new source/presentation pair use the game-owned recreation and safe-retirement rules in [Runtime configuration transitions](RUNTIME-TRANSITIONS.md). MAKO never destroys an active application swapchain merely to apply a profile setting.

## Supported concurrency boundary

Renderer interception state currently supports one active Vulkan instance dispatch domain per process. Applications must externally synchronize each `VkQueue` as Vulkan requires. Multiple swapchains can exist in that instance, but a `VkPresentInfoKHR` batch containing more than one swapchain is rejected when any entry is managed by MAKO; shared binary waits and per-swapchain extension arrays do not yet have a safe fan-out owner.

Do not claim general multi-instance, multi-queue, or multi-swapchain-batch support until the state is instance/queue-owned and real-hardware coverage exists.

## Isolation results

| Component | Managed behavior |
| --- | --- |
| MAKO render role | Admitted and gated by `ENABLE_MAKO=1` |
| Gamescope compositor and Game Mode | Remain active outside the application layer chain |
| Gamescope WSI | Excluded normally; admitted only by a guarded scaling or explicit compatibility profile |
| MAKO spatial role | Admitted only by the complete managed scaling chain |
| Known competing Frame Generation layers | Disabled |
| System implicit overlays, capture, Mesa helpers, and vendor layers | Excluded unless a named guarded exception stages an exact manifest |
| Explicit application layers | Outside this implicit-discovery policy |
| Game-local DLL integrations | Files are untouched, but any other Frame Generation implementation must be disabled |

This boundary intentionally prefers deterministic presentation over arbitrary layer compatibility.

## Diagnostics and validation

For an ordinary managed launch, loader and Renderer evidence must agree that Gamescope WSI is isolated, HDR exposure is disabled, the render role selected a profile and backend, and ordered SDR presentation owns delivery. For an FG-only compatibility launch, `VK_LAYER_MAKO_render` must be above the architecture-correct Gamescope WSI identity. For scaling, the complete three-role order, lower Wayland provenance, authoritative create relay, active source/presentation split, one upper reconstruction owner, and correct generated/real delivery are all required.

Use `VK_LOADER_DEBUG=layer` only for a short reproduction because it is verbose. Presentation diagnostics are also opt-in and can affect timing. Collect the `layers`, `startup`, `scaling`, `performance`, and `recovery` presets described in [Collect diagnostics](COLLECT_DIAGNOSTICS.md).

Any discovery, ordering, present-mode, pNext, image-reservation, or transport change needs its portable launcher/wrapper/package/Flatpak contracts plus applicable MAKO Gym Gamescope, native Vulkan, Proton, synchronization, recovery, and runtime-family evidence. Loader success or `vkcube` does not prove real-game pacing or image correctness. Record untested architectures, sandboxes, GPUs, drivers, and hardware paths explicitly.

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Standalone launch environment | `scripts/mako-launch` |
| Renderer manifests and installation | `mako-render/CMakeLists.txt` |
| Host archive verification | `scripts/package-local.sh` |
| Process-start policy, transport, and admission helpers | `mako-render/src/presentation_policy.hpp` |
| Surface and split-role interception | `mako-render/src/entrypoint.cpp` |
| Swapchain creation and pNext filtering | `mako-render/src/instance.cpp`, `mako-render/src/swapchain.cpp` |
| Generated/real delivery | `mako-render/src/swapchain_present.cpp` |
| Launcher and portable Renderer tests | `scripts/test-mako-launch.sh`, `mako-render/tests/` |
| MAKO Decky wrapper, manifests, and Flatpak contracts | `../plugin/py_modules/mako_plugin/`, `../plugin/tests/` |
