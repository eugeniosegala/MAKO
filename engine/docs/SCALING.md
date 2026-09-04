# Spatial scaling architecture

MAKO Renderer can reconstruct a lower-resolution Vulkan swapchain to the presentation extent, with or without Frame Generation. Scaling is disabled by default and is independent from Fixed or Adaptive scheduling. [Configuration](CONFIGURATION.md) owns user settings; [runtime transitions](RUNTIME-TRANSITIONS.md) owns live, deferred, and restart boundaries.

## Scaling methods

| Method | Implementation | Requirements |
| --- | --- | --- |
| `native` — Native Resolution | Model-free linear reconstruction through Vulkan blits | None beyond the normal Renderer requirements |
| `mako` — MAKO Scaler | Repository-owned single-pass spatial reconstruction with sharpening and anti-ringing | None beyond the normal Renderer requirements |
| `ls1` — LS1 Quality | Complete proprietary LS1 graph | A lawful, user-supplied `Lossless.dll` and an architecture-matched `libvkd3d-shader.so.1` |
| `ls1-performance` — LS1 Performance | Lower-cost proprietary LS1 graph | A lawful, user-supplied `Lossless.dll` and an architecture-matched `libvkd3d-shader.so.1` |

MAKO never packages, uploads, or modifies `Lossless.dll`. It reads the selected resources from the user's file, validates their containers and bindings, translates LS1 bytecode during setup, and keeps the resulting shader modules inside the process. Compatibility is capability-based rather than tied to an allowlist of DLL versions. `mako-cli inspect-dll --dll <path>` performs the GPU-independent checks; Vulkan pipeline construction remains device-specific.

If LS1 discovery, translation, format support, or pipeline creation fails, the swapchain falls back to MAKO Scaler and records the requested method, active method, and reason. Native Resolution and MAKO Scaler do not require the licensed file or translator.

## Activation and ownership

Scaling must be enabled before the process starts because it changes layer membership and swapchain geometry. Once a scaled process is provisioned, method and sharpness changes replace only private scaler resources. A factor or supersampling change may need a game-owned swapchain recreation when it changes the effective source/presentation pair.

Standalone `mako-launch` normally uses one combined Renderer role. MAKO Decky's managed Gamescope path uses three ordered roles:

```text
Application
    -> VK_LAYER_MAKO_render
       reconstruction + Frame Generation
    -> VK_LAYER_FROG_gamescope_wsi_x86_64
    -> VK_LAYER_MAKO_spatial_scaling
       capability virtualization + lower extent
    -> Vulkan driver
    -> Gamescope compositor
```

The lower spatial role owns surface capabilities and physical lower-swapchain extent expansion. It performs no presentation-time GPU work. The upper role owns reconstruction, optional Frame Generation, private resources, and runtime status. The split must be selected explicitly; implicit-manifest directory order is not an ordering contract. [WSI isolation](WSI-ISOLATION.md) owns the launch and proof requirements.

## Pipeline placement

The upper role selects one immutable pipeline order from the presentation extent. Extents at or below 2,304,000 pixels, equivalent to 1920×1200, reconstruct once before Frame Generation:

```text
application source -> reconstruction -> real frame
                                   -> Frame Generation -> generated frame(s)
                                   -> ordered presentation
```

Larger presentation extents run Frame Generation at the source extent and reconstruct each delivered real or generated image before presentation:

```text
application source -> Frame Generation -> source-size real/generated images
                                      -> reconstruction -> presentation
```

The threshold is a pixel budget, not a width/height clamp. It keeps Deck and 1080p-class output to one reconstruction per application frame while avoiding presentation-sized Frame Generation resources at higher resolutions. Multiplier and Fixed/Adaptive changes do not alter the selected placement. With Frame Generation off, each real frame is reconstructed once.

The pre-Frame Generation path can write directly into the exported Frame Generation source when the device proves the required usage and format support. Otherwise it uses the private-output copy path. This is an optimization only; failure does not change the source/presentation contract.

## Extent policy

Each active scaled swapchain has two extents:

- **Source:** the image size presented to the application and sampled by the scaler.
- **Presentation:** the physical lower WSI size and final output resolution.

Dimensions greater than one are rounded down to even values. Scaling remains inactive when the factor is 1.0, the extents cannot differ safely, or a required capability cannot be proven.

### Fixed-extent surfaces

For a surface with a concrete `currentExtent`, MAKO advertises a source extent derived from the presentation extent and configured factor. Swapchain creation activates scaling only when the application requests the exact advertised source under the current surface and policy contract. If the application instead requests the native presentation extent, MAKO preserves that choice and creates a native context.

In the managed split chain, the upper and lower roles exchange capability and create decisions through same-thread, one-shot relays because Gamescope WSI may replace the surface handle between them. A missing, stale, or mismatched lower decision fails closed instead of creating a transient context with the wrong geometry.

The lower split role must observe a Wayland surface created through Gamescope WSI. If it sees the application's XCB or Xlib surface, Gamescope WSI did not establish the required ownership boundary; scaling stays native with `inactive_reason=gamescope-wsi-surface-unproven`. The direct combined Renderer owns its application surface and does not require this split-only proof.

### Variable-extent surfaces

For a surface whose `currentExtent` is variable, the application request is the source. MAKO enlarges it by one aspect-preserving effective factor, subject to Vulkan surface limits and memory admission.

Managed Gamescope scaling also requires a positively identified output target from the server-zero feedback resolver and treats it as the normal presentation ceiling. If the source already fills that target, scaling stays native with `inactive_reason=gamescope-presentation-target-no-headroom`. Quality Supersampling may exceed the target, but it cannot bypass Vulkan limits, memory admission, or the requirement to prove the Gamescope target. Direct non-Gamescope operation applies the factor without inventing a compositor target.

The memory policy admits a presentation extent from device-local heap size and, when available, the driver's live budget and usage. It preserves already proven envelopes across safe live transitions and fails closed when the enlarged swapchain and private resources do not fit. Runtime status reports the requested and effective factor plus the active constraint or inactive reason; it does not promise an exact free-memory measurement.

## Swapchain and queue requirements

Scaling supports ordinary opaque, unprotected, single-array-layer swapchains. The selected source format must support the sampled, transfer, and storage operations used by the active method; LS1 converts at its validated RGBA8 graph boundary while the Renderer preserves the swapchain's supported SDR format.

The application must create an ordinary graphics-and-compute queue family supported by the surface, and presentation must use a registered queue from that family. Unsupported shapes, formats, queues, protected presentation, shared-present modes, or managed multi-swapchain present batches fail closed before consuming application waits. The original real frame remains the fallback whenever private reconstruction cannot be used safely.

Frame Generation normally reserves WSI images for the largest configured generated batch. The restart-only Game Swapchain Images compatibility option preserves the application's requested minimum instead; generated output can then be skipped under compositor pressure because no reserved image is guaranteed. [WSI isolation](WSI-ISOLATION.md) owns the presentation and image-count policy.

## Configuration

```toml
[[profile]]
name = "Scaled game"
active_in = ["Game.exe"]

scaling_enabled = true
scaling_method = "ls1"
scaling_factor = 1.5
scaling_supersampling = false
scaling_sharpness = 0.8
```

Set `frame_generation_enabled = false` for scaling-only operation. Fixed and Adaptive controls remain unchanged when Frame Generation is enabled. Environment-only profiles use `MAKO_SCALING_ENABLED`, `MAKO_SCALING_METHOD`, `MAKO_SCALING_FACTOR`, `MAKO_SCALING_SUPERSAMPLING`, and `MAKO_SCALING_SHARPNESS` with `MAKO_ENV=1`.

`MAKO_VKD3D_SHADER_PATH=/absolute/path/to/libvkd3d-shader.so.1` overrides LS1 translator discovery for a nonstandard installation. It affects setup only.

See [Configuration](CONFIGURATION.md) for exact defaults and ranges. See [Runtime configuration transitions](RUNTIME-TRANSITIONS.md) before changing factor, supersampling, method, or sharpness while a game is running.

## Validation

Portable CTest covers configuration, fixed and variable extent policy, Gamescope target handling, memory admission, swapchain shape and queue requirements, pipeline placement, private transitions, and embedded shader freshness. These tests do not prove Vulkan image quality, layer order, presentation, or hardware memory behavior.

Real-Vulkan changes need proportionate MAKO Gym evidence for both FP32 and FP16 where Frame Generation is involved. Select the applicable suites from [Testing MAKO](../../TESTING.md), including quality, spatial performance, synchronization, Gamescope, recovery, native Vulkan, DXVK, VKD3D-Proton, direct desktop, and supported Flatpak/runtime paths. Record unavailable hardware and matrix rows as not tested.

Positive managed-scaling evidence requires the ordered three-role loader chain, Wayland provenance at the lower role, a source/presentation split, `inactive_reason=none`, one active upper reconstruction owner, and correct real/generated delivery. A selected method alone does not prove that scaling ran. Use `VK_LOADER_DEBUG=layer` only for focused loader captures and the `scaling`, `layers`, and `recovery` diagnostics presets from [Collect diagnostics](COLLECT_DIAGNOSTICS.md).

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Extent, placement, format, queue, and memory policy | `mako-render/src/spatial_scaling_policy.hpp` |
| Surface interception and split-role relays | `mako-render/src/entrypoint.cpp` |
| Swapchain activation and resources | `mako-render/src/instance.cpp`, `mako-render/src/spatial_scaler.cpp` |
| Scaling-only and combined presentation | `mako-render/src/swapchain_present.cpp` |
| MAKO shader source and generated payload | `mako-render/src/shaders/`, `scripts/generate-spatial-scaling-spirv.py` |
| LS1 extraction, validation, and translation | `mako-backend/src/extraction/`, `mako-cli inspect-dll` |
| Portable policy and transition coverage | `mako-render/tests/spatial_scaling_policy_tests.cpp`, `mako-render/tests/profile_update_tests.cpp` |
| GPU quality and runtime evidence | `mako-cli/src/tools/quality.cpp`, private MAKO Gym suites |
