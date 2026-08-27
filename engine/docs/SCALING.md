# Spatial scaling architecture

MAKO Renderer provides an optional Vulkan Scaling Engine that is independent from frame synthesis. The engine exposes a Native passthrough baseline, MAKO's open single-pass spatial scaler, the proprietary LS1 Quality neural network, and the lower-cost LS1 Performance neural network. With a scaler selected, a game renders a smaller source image, MAKO reconstructs it to the presentation extent on the application's Vulkan device, and the full-resolution real frame is then either presented directly or supplied to Fixed or Adaptive Frame Generation. The engine is disabled by default.

## Choosing and activating a scaler

| Method | Ownership and graph | Runtime requirement | Selection guidance |
| --- | --- | --- | --- |
| `native` — **Native (No MAKO Scaler)** | Passthrough; no spatial reconstruction graph | No DLL or shader translator | Live A/B baseline that keeps the process-start Scaling Engine/WSI lane provisioned without allocating or dispatching a spatial scaler. |
| `mako` — **MAKO (Open)** | Repository-owned one-pass Catmull-Rom reconstruction, contrast-aware anti-ringing, and bounded sharpening | No DLL or shader translator | Smallest resource and pass count; also the automatic per-swapchain fallback when LS1 setup fails. |
| `ls1` — **LS1 Quality** | Lossless Scaling's proprietary three learned feature passes plus its common reconstruction pass | User's licensed `Lossless.dll` and an architecture-matched `libvkd3d-shader.so.1` | Complete LS1 graph; compare image reconstruction and GPU cost against MAKO in the target game. |
| `ls1-performance` — **LS1 Performance** | Lossless Scaling's proprietary lower-cost learned feature pass plus its common reconstruction pass | User's licensed `Lossless.dll` and an architecture-matched `libvkd3d-shader.so.1` | Lower LS1 pass count for constrained GPU budgets; compare it separately rather than assuming the same result as Quality. |

For MAKO Decky, select the intended game profile, open **Spatial Scaling**, enable **Scaling Engine (Restart)**, and launch or restart the game so Decky can stage the verified Gamescope WSI-before-MAKO presentation path. Inside that provisioned process, `native`, `mako`, `ls1`, `ls1-performance`, factor, and sharpness changes use one Vulkan-standard game-owned swapchain recreation after edits settle; a brief flicker is normal. Native retains the WSI lane but performs no spatial work, making it the live comparison and deselection state. Turning Scaling Engine on or off is always saved for the next process and never changes the running process's layer membership. For direct Renderer configuration, set the `scaling_*` fields in the [Configuration](#configuration) example below and restart after changing `scaling_enabled`; direct launch tooling does not automatically stage Decky's managed Gamescope WSI manifest. `frame_generation_enabled = false` activates scaling-only; enabling it provisions LSFG so the same reconstructed real frame can feed Fixed or Adaptive generation. Changing from a scaling-only process to Frame Generation still requires a complete process restart because swapchain recreation cannot add device features.

## Product and licensed-resource boundary

`Lossless.dll` supplies the proprietary LS1 and LSFG model shaders. MAKO never bundles, uploads, installs, or persists those licensed payloads. When LS1 is selected, the Renderer extracts only the selected model resources from the user's own DLL in memory and uses the open-source `libvkd3d-shader.so.1` translator to convert their Direct3D 11 compute bytecode to Vulkan SPIR-V during swapchain setup. The resulting Vulkan shader modules remain process-local. [Lossless Scaling's developer describes LS1 as a small, fast neural network designed for scaling ratios from 1x to 2x](https://steamcommunity.com/app/993090/discussions/0/3449213285561959332/?ctp=2); MAKO implements the discovered GPU graph around that model rather than calling an unsupported Windows capture/presentation ABI.

MAKO's default method remains fully repository-owned. Its embedded compute shader performs a clamped Catmull-Rom reconstruction, contrast-aware anti-ringing, and bounded local sharpening. It is also the automatic runtime fallback when LS1 cannot find the licensed DLL or translator, the DLL lacks the expected resources, or the driver rejects the LS1 formats or pipelines. The active and requested methods plus any fallback reason are logged explicitly.

When a process starts with Scaling Engine enabled and `frame_generation_enabled = false`, MAKO does not negotiate the external-memory, external-semaphore, or timeline-semaphore features used by LSFG and does not create the private frame-generation backend or generated-output resources. Native creates no spatial pipeline and never locates `Lossless.dll`; MAKO scaling also avoids the DLL; LS1 scaling locates the DLL and translates its selected spatial model but still avoids every LSFG interop feature. This is a process/device-creation decision: enabling Frame Generation afterward requires a complete game/process restart. A natural swapchain recreation cannot add the omitted Vulkan extensions or device features. A process that started with Frame Generation enabled retains its existing resources when generation is turned off and may therefore turn it back on live.

## Pipeline order

The application always produces one low-resolution real frame. MAKO scales that real frame once before any optional frame synthesis:

```text
Application source rectangle
        |
        v
selected spatial reconstruction (Native bypass, MAKO, or LS1)
        |
        +----------------------> full-resolution real frame -> presentation
        |
        v
full-resolution LSFG source -> Fixed or Adaptive scheduling
        |
        v
full-resolution generated frame(s) + full-resolution real frame -> presentation
```

With `frame_generation_enabled = false`, the reconstructed real frame goes directly to the lower presentation path. With Fixed or Adaptive Frame Generation enabled, the same reconstructed result is copied to the existing exported source image and the LSFG backend generates at the presentation resolution. Generated frames are not scaled one by one, so increasing the frame-generation multiplier does not multiply the spatial reconstruction work.

Scaling does not own or modify Fixed and Adaptive policy. The selected multiplier, Adaptive target and ceiling, Smooth Cadence, Dynamic Cadence Recovery, acquire recovery, and native-first fallbacks keep their existing behavior. Any path that chooses a real/native frame still runs the active spatial stage before presenting it.

## Extent ownership

`SpatialScalingExtents` records two different dimensions for each scaled swapchain:

- **Source extent:** the rectangle the application renders and the scaler samples.
- **Presentation extent:** the real lower WSI image size, the spatial output size, and the resolution consumed by frame generation and presentation.

### Fixed-extent surfaces

Gamescope and other fixed-extent surfaces publish a concrete `currentExtent`. After transfer-usage and format-feature preflight, MAKO intercepts both surface-capability entry points and advertises `floor(presentation / scaling_factor)` as the source extent. Dimensions greater than one are rounded down to even values so compute and frame-generation workgroups avoid half-pixel asymmetry. The lower swapchain retains the compositor-owned presentation extent. Each successful virtualized query latches an exact physical-device/surface contract containing the advertised source, real presentation extent, factor, policy revision, and query generation; a non-virtualized query or surface destruction clears that contract.

MAKO scales only when the application's swapchain request matches the source extent in the latest compatible contract, the current policy revision and factor still match, and the real fixed presentation extent has not changed. An application-selected override is not silently replaced with a different source size. If the application requests the real native extent after observing MAKO's smaller advertised extent, there is no source/presentation split for an in-layer scaler: scaling remains hard-inactive with `inactive_reason=application-extent-override-no-source-presentation-split`. A different non-native mismatch is rejected rather than passed to lower WSI. The original lower capabilities are queried again at swapchain creation and remain authoritative for presentation.

The extent-policy revision changes when effective spatial activation, factor, or process support changes. MAKO↔LS1 method and sharpness edits can therefore reuse the same fixed extent contract during a game-owned live recreation. Native↔scaler transitions change effective spatial activation and, like a factor/support change, require a fresh capability query before a fixed source request can activate; stale low-resolution requests fail closed, while a native request remains native.

### Variable-extent surfaces

Desktop window systems normally publish `UINT32_MAX` as `currentExtent`, so there is no compositor-owned native size to advertise during the capability query. MAKO preserves the application's requested extent as the source and requests a lower presentation extent enlarged by `scaling_factor`. It uses one aspect-preserving effective factor for both dimensions, clamps that factor to the surface maximum extent, and rounds enlarged dimensions down to even values. This is the only existing safe layer-side path that can create a split when an application owns its requested extent; MAKO never reinterprets an application-requested native fixed extent as a hidden low-resolution source.

On a variable surface, configure the game or window to request the intended lower rendering resolution; MAKO cannot infer a separate monitor-native target from an unconstrained window. A factor that cannot enlarge both dimensions within the surface limits leaves scaling inactive.

Some Wayland compositors report MAKO's enlarged lower WSI extent back to the application as its next logical window size. MAKO records the last source/presentation pair per surface and treats an exact echoed presentation extent as compositor feedback rather than a new low-resolution source. That recreated swapchain stays native-sized and the record is retained so repeated echoes cannot compound the scaling factor. A genuinely different application-requested extent may activate scaling again. This conservative rule can also suppress an intentional source-size change that exactly equals the previous presentation size for the remaining surface lifetime. The guard does not apply to fixed extents and is cleared when the surface is destroyed. Direct variable-Wayland scaling can therefore become inactive after a compositor-driven recreation. MAKO Decky's ordered Gamescope WSI lane is the validated managed path, but a replacement context must still retain a genuine source/presentation split; a fixed native surface is valid only when the application consumes MAKO's advertised source capability.

## Swapchain, queue, and present-batch contract

The initial scaler supports only a conventional WSI swapchain with `imageArrayLayers = 1`, `VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR`, no `VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR`, and neither shared-demand nor shared-continuous present mode. “Single-layer” here means one swapchain image-array layer, not one Vulkan interception layer. Unsupported variable-extent swapchains stay on the unscaled native path. A fixed surface that was already virtualized cannot safely fall back after the application selected the advertised source extent, so an unsupported create-time shape, queue, or format is rejected instead of exposing an incomplete top-left image.

At device creation, scaling selects an ordinary application-created queue family whose device-queue create flags are zero and whose Vulkan family properties include both graphics and compute. Swapchain creation additionally proves that this family can present to the selected surface. A scaled `vkQueuePresentKHR` must arrive on queue index 0 from that same family; another family, a protected queue, or another queue index fails closed before MAKO submits spatial work.

Application wait semaphores belong to an entire `VkPresentInfoKHR` batch and can be consumed only once. MAKO does not yet implement the fan-out and per-swapchain `pNext` handling required for a batch containing a scaled swapchain. If `swapchainCount > 1` and any member has active spatial scaling, MAKO returns `VK_ERROR_UNKNOWN` and fills `pResults` when supplied before submitting work or consuming any application wait semaphore.

## Spatial reconstruction passes

The application renders into the source-sized top-left rectangle of a real presentation-sized WSI image. At present time, MAKO waits on the application's semaphores and copies that rectangle into the selected method's private source image.

The MAKO method then records this work:

1. Dispatch one 8-by-8-workgroup compute pass over the presentation extent.
2. Sample a clamped 4-by-4 neighbourhood for each output pixel, reconstruct with separable Catmull-Rom weights, and clamp the result to the local colour envelope.
3. Blend a small bilinear anti-ringing correction in high-contrast regions and apply a bounded, edge-aware sharpening term.

LS1 Quality runs three 16-by-16-workgroup neural passes: two source-sized RGBA8 feature stages followed by a learned luma-feature stage that writes a 2x-source `R8_SNORM` image. LS1 Performance replaces those three passes with one lower-cost learned pass that writes the same feature representation. Both modes then run the common output-resolution LS1 reconstruction pass, which combines the learned feature image with the original source through the model's linear sampler and colour reconstruction. The five DLL model variants are selected by rounding `scaling_sharpness * 4`.

After either method, MAKO copies the reconstructed image back across the complete WSI image and, when frame generation is active, into its full-resolution exported source image, then returns the WSI image to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` before the lower present. Explicit compute-to-compute barriers separate LS1's learned stages; all images, pipelines, descriptors, and constants are created once with the swapchain.

Incremental-present regions are removed from the lower `pNext` chain while scaling is active because application damage rectangles use source coordinates and cannot safely describe the reconstructed presentation image.

## Formats and HDR boundary

Spatial scaling currently supports swapchains classified by MAKO's colour pipeline as:

| Encoding | Spatial working format | Intended input |
| --- | --- | --- |
| `Sdr8` | `VK_FORMAT_R8G8B8A8_UNORM` | MAKO directly; LS1 through its native RGBA8 model resources |
| `SdrHighPrecision` | `VK_FORMAT_R16G16B16A16_SFLOAT` | MAKO directly; LS1 with Vulkan blit conversion at the RGBA8 model boundary |

The surface must support transfer source and destination usage. The selected application format must support blit source and destination operations, and the spatial working format must support blit, sampling, and storage-image operations with optimal tiling. LS1 keeps its discovered learned graph in its native `R8G8B8A8_UNORM` and `R8_SNORM` resources even when MAKO's SDR exchange transport is `R16G16B16A16_SFLOAT`; the boundary blits perform the normalized SDR conversion before and after the model. MAKO checks the surface before fixed-extent virtualization and repeats format-specific checks at swapchain creation. Unsupported combinations do not construct a spatial-scaling context.

HDR10/PQ, scRGB HDR, HLG, Dolby Vision, and unsupported colour pairs are outside the current scaling contract. All scaling under Gamescope requires HDR exposure to be disabled at process start, normally through `MAKO_DISABLE_HDR_EXPOSURE=1`; if Gamescope is detected while HDR exposure is allowed, MAKO does not select scaling on either fixed or variable surfaces. A swapchain classified as HDR never activates the spatial scaler on any desktop. Treat this as a fail-closed boundary: HDR scaling is not available, and an HDR test that merely continues presenting real frames is not positive scaling evidence. See [HDR pipeline architecture](HDR-PIPELINE.md) and [Gamescope WSI isolation](WSI-ISOLATION.md) for the independent colour and presentation contracts.

## Resource and performance model

The spatial stage allocates no additional GPU resources when Scaling Engine is disabled, Native is selected, or the selected swapchain is unsupported. A provisioned Native process still pays the compatibility and presentation behavior of the Gamescope WSI layer chain described in [Gamescope WSI isolation](WSI-ISOLATION.md), but MAKO performs no spatial allocation, copy, shader dispatch, or licensed-resource lookup. A process that starts in scaling-only mode also omits all frame-generation interop and private-backend resources. Every active scaler owns one source-sized sampled image, one presentation-sized output image, immutable parameters, pipelines/descriptors, and one reusable command buffer and ready semaphore per WSI image.

MAKO adds one compute pipeline and descriptor set. LS1 Performance adds a 2x-source single-channel signed-normalized feature image and two compute pipelines. LS1 Quality additionally adds two source-sized RGBA8 feature images and two compute pipelines. The LS1 model weights live in the translated shaders rather than separate runtime buffers.

Those objects are created with the swapchain and reused. The present hot path performs no spatial-resource allocation or shader translation. The scaler runs on the application's selected Vulkan device, so the spatial stage itself does not introduce a cross-device image boundary; optional frame-generation resources retain their existing private-device contract. In the combined path, the existing frame-generation source receives the reconstructed full-resolution frame once.

The shader uses nine bilinear texture samples for exact Catmull-Rom folding plus four explicit center-texel loads per output pixel. Sharpening is folded into the reconstruction pass rather than dispatched separately. This keeps command submission, descriptors, image count, and synchronization bounded across scaling factors and frame-generation multipliers, but it does not make scaling free: validate GPU time, memory, base cadence, and presentation behavior on every supported hardware class.

The GLSL source, generated SPIR-V arrays, and hash manifest live under `mako-render/src/shaders/`. `scripts/generate-spatial-scaling-spirv.py` owns regeneration and its read-only `--check` mode is part of CTest. Do not edit the embedded header or hashes independently.

## Measured development evidence

On 26 August 2026, the strengthened 44-case advanced smoke matrix passed one-for-one under headless Gamescope 3.16.23.4 at 120 Hz on the available AMD Radeon Graphics RADV NAVI33 host. Fixed cases reported positively presented generated frames, Adaptive cases reported positive generated-frame plans, every Steady case reached active Smooth Cadence, all ten LS1 method/variant selections remained active, and the only LS1 fallback was the intentionally invalid-DLL case. Aggregate logs contained no device-loss, validation, sanitizer, abort, or crash marker. This record used the installed 64-bit development Renderer and native Vulkan `vkcube`; it is not evidence for spatial image quality, logged LSFG shader precision, a game API translation layer, 32-bit presentation, Steam Deck/RDNA2, Flatpak, HDR, or live configuration mutation.

On the available RADV NAVI33 development GPU, the optimized MAKO shader reported 56→40 VGPR, 18→24 subgroups per SIMD, 365→320 instructions, 264→229 inverse throughput, and zero spills. A live direct-Wayland run reconstructed 500×500 to 750×750; when the compositor echoed 750×750 during recreation, MAKO's feedback guard kept the replacement swapchain native-sized instead of recursively requesting 1124×1124. Bounded fixed-extent Gamescope/X11 runs activated 332×332-to-500×500 MAKO scaling before both Fixed 2x and Adaptive 120 FPS/2x-ceiling frame generation. The installed Lossless Scaling 3.2.2.0 DLL's LS1 Quality and Performance resource sets were extracted and translated with the installed architecture-matched 64-bit Steam Linux Runtime vkd3d-shader; a separate 32-bit executable translated and validated the same model matrix with the runtime's i386 library. Direct-Wayland scaling-only runs activated both LS1 methods on RADV NAVI33 from 640×360 to 960×540 through MAKO's high-precision SDR transport without creating the frame-generation backend or LSFG interop resources. Separate combined runs activated LS1 Quality before Fixed 2x generation and LS1 Performance before Adaptive 120 FPS/2x-ceiling generation, including the variable-surface feedback guard during recreation. This is basic 64-bit GPU activation and 32-bit translation evidence, not spatial image-quality, spatial GPU-performance, 32-bit Vulkan presentation, Steam Deck/RDNA2, fixed-Gamescope LS1, or game-runtime evidence, and it does not replace the hardware and game matrix below.

On 26 August 2026, MAKO-Gym's complete 66-case procedural visual matrix passed on AMD Radeon Graphics RADV NAVI33 with Mesa 26.0.0-devel `git-9cc9241790`: 18/18 LSFG cases, 28/28 production spatial-scaling cases, and 20/20 production scaling-to-LSFG handoffs. The pass covered all five scenes, FP32/FP16, both frame-generation models, Flow Scale 0.25–1.0, interpolation timestamps 0.25–0.75, MAKO/LS1/LS1 Performance, factors 1.25×–2×, MAKO sharpness edges, and every LS1 learned variant. All 66 logs and PPM directories passed Gym's parameter, extent, payload, temporal-motion, active-method, and metric assertions and the evidence directory was sanitized. This is deterministic offscreen pixel and synchronization evidence for that source-built CLI, licensed input, translator, GPU, and driver; it is not WSI/compositor presentation, GPU-time, power, subjective game capture, Steam Deck/RDNA2, HDR, DXVK/VKD3D-Proton, or another-driver evidence.

## Configuration

The three profile settings are independent from Fixed and Adaptive Frame Generation:

```toml
[[profile]]
name = "Scaled game"
active_in = ["Game.exe"]

scaling_enabled = true
scaling_method = "ls1"
scaling_factor = 1.5
scaling_sharpness = 0.5

# Scaling-only:
frame_generation_enabled = false
```

| Setting | Range | Default | Meaning |
| --- | --- | --- | --- |
| `scaling_enabled` | Boolean | `false` | Provisions Scaling Engine for the process. Changing it requires a game restart; MAKO Decky also stages Gamescope WSI when enabled. |
| `scaling_method` | `native`, `mako`, `ls1`, `ls1-performance` | `mako` | Selects passthrough, the open single-pass method, LS1 Quality, or LS1 Performance. Method changes are live through game-owned recreation after the engine is provisioned. LS1 requires the licensed DLL and vkd3d-shader; failure uses MAKO and logs why. |
| `scaling_factor` | 1.0–2.0 | `1.5` | Ratio from each source dimension to each presentation dimension; 1.0 performs no scaling work. |
| `scaling_sharpness` | 0.0–1.0 | `0.5` | MAKO's continuous sharpening strength or LS1's nearest one of five learned model variants. |

To combine scaling with Fixed Frame Generation, set `frame_generation_enabled = true`, leave `adaptive = false`, and select `multiplier`. To combine it with Adaptive Frame Generation, enable both `frame_generation_enabled` and `adaptive`, then configure the normal Adaptive target, ceiling, and cadence options. No scaling-specific frame-generation mode exists.

For an environment-only profile, set `MAKO_ENV=1` and use `MAKO_SCALING_ENABLED`, `MAKO_SCALING_METHOD`, `MAKO_SCALING_FACTOR`, and `MAKO_SCALING_SHARPNESS`. The normal frame-generation variables remain independent.

LS1 translator discovery is architecture-aware. MAKO tries stable platform libraries from Steam Linux Runtime 4, sniper, soldier, and the legacy runtime beside the Lossless Scaling Steam library, then the system `libvkd3d-shader.so.1`. `MAKO_VKD3D_SHADER_PATH=/absolute/path/to/libvkd3d-shader.so.1` can select an explicit architecture-matched translator for troubleshooting or a nonstandard Steam layout. It changes only setup-time translation; no translator call occurs in the frame path.

Scaling Engine enablement cannot mutate process-start Vulkan layer membership and always waits for game restart. With the engine provisioned, changing Native/scaler method, factor, or sharpness, changing Flow Scale, selecting Lighter FG Model, or selecting a Fixed/Adaptive policy that needs more generated-frame capacity cannot mutate the GPU resources owned by an existing game swapchain. MAKO debounces distinct resource edits for 500 ms. After the quiet period, it completes one ordinary lower present so the driver's WSI consumes the application's binary wait semaphores, then converts only that successful or suboptimal result into one `VK_ERROR_OUT_OF_DATE_KHR`. A conforming Vulkan game destroys and recreates its own swapchain, and the normal creation path applies the latest scaling and LSFG context profile. Multiple edits settle into the same request. MAKO never destroys the application's handle, never signals before semaphore consumption, and never repeats the same request; a brief flicker is expected. Method and sharpness changes retain the compatible fixed extent contract. Factor changes require the title to make a fresh surface-capability query; if it recreates directly at its native fixed extent, the scaler stays inactive for that process. A title that ignores the one-shot result retains its current context and needs a process restart as the compatibility fallback for changes that depend on recreation. Flow Scale, Lighter FG Model, and generated-frame capacity use this live path only when the process retained frame-generation resources. For a combined write containing a process-static change such as Scaling Engine, Ultra Performance, or GPU selection, Root retains the running process's static baseline while applying compatible fields and safe recreation requests; diagnostics keep the static portion explicitly pending for restart. Enabling Frame Generation in a process that started scaling-only always requires a full game/process restart because the Vulkan device omitted LSFG interop; swapchain recreation is insufficient. Other live Fixed/Adaptive settings retain the semantics documented in [Configuration](CONFIGURATION.md).

## Validation

Portable tests prove parsing and policy, not Vulkan image quality or presentation. Run the normal Renderer suite and sanitizer path described in [Testing MAKO](../../TESTING.md). The focused deterministic boundaries include:

- configuration defaults, accepted ranges, TOML round trips, and environment overrides;
- fixed-surface capability virtualization, source rounding, exact latched capability/create matching, policy and surface invalidation, application-override no-split diagnostics, variable-surface enlargement, echoed-presentation feedback suppression, maximum-extent clamping, disabled and 1.0-factor behavior;
- opaque single-array-layer, unprotected, non-shared-present swapchain-shape policy;
- process-static Scaling Engine projection plus debounced one-shot live-recreation state for Native/scaler method, factor, sharpness, Flow Scale, and Lighter FG Model changes, including cancellation, edit coalescing, and no repeated signal for the same request; and
- GLSL, SPIR-V payload, and hash freshness for both SDR working formats.

Run `scripts/run-mako-gym.sh` with the private sibling MAKO-Gym checkout for the hardware lanes. Gym's 47-case native-Vulkan inventory owns headless Gamescope orchestration, runtime assertions, logs, and `summary.tsv`; it covers Scaling Engine provisioned with Native passthrough, every scaler and LS1 learned sharpness variant, explicit fallback, scaling-only resource separation, isolated Fixed/Adaptive/Steady/Smooth/recovery/Ultra policies, FP32/FP16 configurations, Flow Scale and lighter-model states, and representative combined presentation paths. Gym's separate 74-case procedural visual inventory runs five scenes through real LSFG, every production spatial method, edge factors/timestamps, and the actual scaling-to-LSFG handoff while retaining validated PPM comparisons. Nine three-run sentinels detect nondeterministic output, 17 repeated LSFG workloads enforce practical throughput and variance budgets from 720p through 5120×2160, 36 every-scaler rows score exact-resolution pixels and timestamp complete scaler graphs through 5120×2160, 12 paired live rows measure present/full-frame p95/p99 plus CPU/RSS overhead, and eight canonical paths run under Khronos synchronization validation. The complete contracts are authoritative in `MAKO-Gym/docs/`; [Testing MAKO](../../TESTING.md#mako-gym-advanced-native-vulkan-smoke-matrix) owns when each private gate is optional or required. No lane replaces API/game compatibility, live-mutation lifecycle, subjective review, another driver, or another hardware class.

Every spatial-scaling change also needs proportionate real-Vulkan evidence. Use this matrix and record unavailable rows as **not tested**:

| Boundary | Required cases |
| --- | --- |
| Surface ownership | Fixed Gamescope surface with HDR exposure disabled and variable desktop surface; source and presentation extents must match the policy logs, and an echoed variable presentation extent must fall back without recursive enlargement. |
| Operation mode | Fresh MAKO scaling-only startup with no DLL/backend/interop resources; LS1 Quality and Performance scaling-only with DLL translation but no LSFG interop; every method plus Fixed 2x and Adaptive; enabling FG after scaling-only must remain unavailable until process restart. |
| Swapchain shape | Opaque single-array-layer ordinary swapchain plus fail-closed non-opaque, array-layer, protected, shared-demand, and shared-continuous cases. |
| Queue ownership | Ordinary queue 0 from the application-created graphics-and-compute family that presents to the surface; wrong family, wrong index, and protected queue must fail closed. |
| Present batching | One scaled swapchain succeeds; a multi-swapchain batch containing scaling returns before any application wait semaphore is consumed. |
| Format | Standard 8-bit and high-precision SDR for every method; prove all sampled/storage/blit format features and LS1's RGBA8 boundary conversion. |
| Tuning | Factor 1.0 inactive behavior plus representative 1.5 and 2.0 runs; MAKO sharpness 0.0, 0.5, and 1.0; all five LS1 model variants. |
| Lifecycle | Initial create, focus and overlay transitions, resolution change, natural swapchain recreation, process-restart Scaling Engine changes, live Native/scaler method/factor/sharpness recreation, ignored one-shot compatibility fallback, profile update, and shutdown. |
| Launch boundary | Native Vulkan, DXVK, and VKD3D-Proton under the supported Gamescope path; direct desktop and each changed Flatpak/runtime path. |
| Frame-generation precision | Combined-path FP32 and FP16 runs; spatial RGBA16F transport is not a substitute for LSFG FP16 evidence. |
| Fail closed | Missing/wrong DLL, missing translator, missing LS1 resource, unsupported LS1 resource format or format feature, Gamescope with HDR exposure allowed, explicit HDR formats, unsupported swapchain shape, incompatible presentation queue, maximum-extent exhaustion, and scaled multi-swapchain batches. LS1-specific failures must log and use MAKO without provisioning LSFG. |
| Quality | Fine text, UI edges, thin geometry, diagonal motion, foliage, particles, gradients, high-contrast edges, and repeated camera motion against native-resolution reference captures. |
| Performance | Scaling GPU time, base real-frame cadence, displayed cadence, device/unified memory, frame pacing, and startup/recreation cost on low-power and desktop hardware. |

Use the Vulkan validation layer when it is installed and preserve `VK_LOADER_DEBUG=layer` evidence that the development build, rather than an older installed layer, was activated. An implicit MAKO layer must be selected through its development manifest path; setting only the explicit-layer path is not proof. Inspect `MAKO Renderer: spatial scaling surface virtualized`, `spatial scaling swapchain policy`, and `spatial scaling active` records for the selected dimensions, factor, format, and active decision. The policy record exposes fixed/variable surface mode, advertised and actual source/presentation extents, current and contract policy revisions, query generation, the stable `inactive_reason`, and `source_presentation_split`. `active=0` or `source_presentation_split=0` proves that no scaler ran even if a model was selected in configuration. Live resource changes emit `runtime-transition-pending` with separate scaling, flow-resolution, and lighter-model fields, followed by `runtime-transition-recreation-requested`; the replacement context's `runtime-state-applied` record exposes `effective_flow_scale` and `lighter_model`.

A skipped GPU test is not evidence of scaling correctness. MAKO-Gym's procedural visual matrix validates deterministic pixels, its timestamp-query suite measures the offscreen production scaler graph, and its synchronization suite detects selected offscreen access hazards; none replaces subjective captures, WSI presentation, power/latency measurement, or the real game/runtime matrix.

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Fixed and variable extent policy | `mako-render/src/spatial_scaling_policy.hpp` |
| Surface-capability interception and preflight | `mako-render/src/entrypoint.cpp` |
| Swapchain extent and format activation | `mako-render/src/instance.cpp` |
| Spatial resources and command recording | `mako-render/src/spatial_scaler.cpp` |
| Scaling-only and combined presentation | `mako-render/src/swapchain_present.cpp` |
| Open compute algorithm | `mako-render/src/shaders/spatial_scaling.comp` |
| LS1 resource extraction and DXBC translation | `mako-backend/src/extraction/ls1_shader_set.cpp` |
| Embedded shader generation and freshness | `scripts/generate-spatial-scaling-spirv.py`, exercised by CTest |
| Configuration schema and validation | `mako-common/include/mako-common/configuration/config.hpp`, `mako-common/src/configuration/config.cpp` |
| Deterministic policy and live-recreation semantics | `mako-render/tests/spatial_scaling_policy_tests.cpp`, `mako-render/tests/profile_update_tests.cpp` |
| Procedural scenes, ideal references, masks, and scoring | `mako-common/src/quality/image_quality.cpp`, `mako-common/tests/image_quality_tests.cpp` |
| Offscreen spatial and combined real-GPU execution | `mako-cli/src/tools/quality.cpp`, private MAKO-Gym quality matrix |
