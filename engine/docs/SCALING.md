# Spatial scaling architecture

MAKO Renderer provides optional Vulkan scaling that is independent from frame synthesis. It exposes a model-free Native Resolution linear baseline, MAKO's open single-pass spatial scaler, the proprietary LS1 Quality neural network, and the lower-cost LS1 Performance neural network. The game renders a smaller source image, the selected method reconstructs it to the presentation extent on the application's Vulkan device, and the full-resolution real frame is then either presented directly or supplied to Fixed or Adaptive Frame Generation. Scaling is disabled by default.

## Choosing and activating a scaler

| Method | Ownership and graph | Runtime requirement | Selection guidance |
| --- | --- | --- | --- |
| `native` — **Native Resolution** | Model-free linear reconstruction through Vulkan transfer blits | No spatial-model DLL or shader translator | Default low-cost A/B baseline that retains the same source/presentation split as every model. |
| `mako` — **MAKO Scaler** | Repository-owned one-pass sharpened-cubic reconstruction, contrast-aware anti-ringing, and bounded sharpening | No spatial-model DLL or shader translator | Smallest resource and pass count; also the automatic per-swapchain fallback when LS1 setup fails. |
| `ls1` — **LS1 Quality** | Lossless Scaling's proprietary three learned feature passes plus its common reconstruction pass | User's licensed `Lossless.dll` and an architecture-matched `libvkd3d-shader.so.1` | Complete LS1 graph; compare image reconstruction and GPU cost against MAKO in the target game. |
| `ls1-performance` — **LS1 Performance** | Lossless Scaling's proprietary lower-cost learned feature pass plus its common reconstruction pass | User's licensed `Lossless.dll` and an architecture-matched `libvkd3d-shader.so.1` | Lower LS1 pass count for constrained GPU budgets; compare it separately rather than assuming the same result as Quality. |

In MAKO Decky, select **Enable Scaling (Restart)** before launch so it can stage the frame-generation role, Gamescope WSI, and spatial role. Inside that process, all four methods share one immutable source/presentation contract. Method changes rebuild only the lower role's private scaler at the next present; sharpness does the same after a 500 ms quiet period. After a Scale Factor edit settles, the managed Gamescope lower spatial role requests one game-owned recreation only after a successful present carries maintenance1 retirement proof; a context without that proof waits for a natural resolution change or restart. Scaling enablement always waits for restart because layer membership is process-static. A compatible non-Gamescope direct context may use the same guarded request. Direct Renderer configuration uses the `scaling_*` fields below but does not stage Decky's split path. `frame_generation_enabled = false` runs scaling alone while retaining successfully provisioned resources for live Frame Generation enablement.

## Product and licensed-resource boundary

`Lossless.dll` supplies the proprietary LS1 and LSFG model shaders. MAKO never bundles, uploads, installs, or persists those licensed payloads. When LS1 is selected, the Renderer extracts only the selected model resources from the user's own DLL in memory and uses the open-source `libvkd3d-shader.so.1` translator to convert their Direct3D 11 compute bytecode to Vulkan SPIR-V during swapchain setup. The resulting Vulkan shader modules remain process-local. [Lossless Scaling's developer describes LS1 as a small, fast neural network designed for scaling ratios from 1x to 2x](https://steamcommunity.com/app/993090/discussions/0/3449213285561959332/?ctp=2); MAKO implements the discovered GPU graph around that model rather than calling an unsupported Windows capture/presentation ABI.

DLL consumption is capability-based rather than version-allowlisted. MAKO bounds-checks PE32 and PE32+ resource trees, fingerprints the complete file and numeric-resource layout for process-local cache identity and diagnostics, validates the required DXBC or SPIR-V container structure, and proves only the descriptor bindings each selected graph actually consumes. Additional resources, unknown DXBC chunks, trailing vendor metadata, and unrelated SPIR-V declarations remain accepted. LSFG precision/model families and LS1 Quality/Performance are inspected independently, so one incompatible path does not suppress another compatible path. `mako-cli inspect-dll --dll <path>` performs the complete GPU-independent resource inspection and translates every LS1 variant; runtime pipeline construction remains the final device-specific check.

MAKO's default method remains fully repository-owned. Its embedded compute shader uses a sharpened cubic reconstruction with a 0.8 tension, so the image starts sharper than Catmull-Rom before `scaling_sharpness` adds its separate bounded local-sharpening pass. Contrast-aware anti-ringing, a local clamp, and a contrast-relative envelope prevent uncontrolled halos. It is also the automatic runtime fallback when LS1 cannot find the licensed DLL or translator, the DLL lacks the expected resources, or the driver rejects the LS1 formats or pipelines. The active and requested methods plus any fallback reason are logged explicitly.

Every matched process negotiates the external-memory, external-semaphore, and timeline-semaphore features used by LSFG and constructs its private frame-generation backend and generated-output resources when startup succeeds, even with `frame_generation_enabled = false`. Native Resolution creates two transfer-only spatial images and uses no compute model, MAKO Scaler uses only repository-owned spatial shaders, and LS1 locates the user's DLL and translates the selected spatial graph. While Frame Generation is Off, the per-frame path bypasses the retained LSFG resources completely. This deliberate startup-memory tradeoff lets the switch enable live without attempting to mutate device features or rebuild the game swapchain. If startup interop or backend construction failed, the Renderer keeps independent scaling available and reports that generation requires restart.

## Pipeline order

The application always produces one low-resolution real frame. MAKO scales that real frame once before any optional frame synthesis:

```text
Application source rectangle
        |
        v
selected spatial reconstruction (Native linear, MAKO, or LS1)
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

Gamescope and other fixed-extent surfaces publish a concrete `currentExtent`. After transfer-usage preflight proves a presentation-capable graphics/compute queue and at least one compatible advertised SDR format, MAKO intercepts both surface-capability entry points and advertises `floor(presentation / scaling_factor)` as the source extent. MAKO Decky's native managed launcher uses `VK_IMPLICIT_LAYER_PATH` only to discover the guarded manifests and explicitly pins `VK_INSTANCE_LAYERS` as Frame Generation → Gamescope WSI → Spatial Scaling; relying on manifest-directory enumeration can reverse those roles on some loader versions and lose the virtual extent. In that split chain, the upper Frame Generation role brackets the downstream capability query and consumes the lower role's immutable result through a same-thread, one-shot relay before forwarding that virtual capability to the application. This association deliberately survives Gamescope WSI replacing `VkSurfaceKHR` between the lower and upper roles; it cannot be consumed by another thread or physical device and is cleared before every query and after every consume attempt. The upper role then forwards the resulting source-sized create request unchanged, while the lower spatial role alone converts it back to the presentation extent and reconstructs it. This prevents an intervening WSI layer from restoring the native fixed extent before Proton or the game consumes it. Gamescope may advertise unrelated HDR formats alongside the managed SDR path; those formats do not disable compatible SDR scaling. Dimensions greater than one are rounded down to even values so compute and frame-generation workgroups avoid half-pixel asymmetry. The lower swapchain retains the compositor-owned presentation extent. Each role still latches an exact physical-device/application-visible-surface contract containing the advertised source, real presentation extent, factor, policy revision, and query generation for its own create-time validation; a non-virtualized query or surface destruction clears that contract.

MAKO scales only when the application's swapchain request matches the source extent in the latest compatible contract, the current policy revision and factor still match, and the real fixed presentation extent has not changed. An application-selected override is not silently replaced with a different source size. If the application requests the real native extent after observing MAKO's smaller advertised extent, there is no source/presentation split for an in-layer scaler: scaling remains hard-inactive with `inactive_reason=application-extent-override-no-source-presentation-split`. A different non-native mismatch is rejected rather than passed to lower WSI. The original lower capabilities are queried again at swapchain creation and remain authoritative for presentation.

The extent-policy revision changes when scaling activation, factor, or process support changes. Native, MAKO, and LS1 method or sharpness edits reuse the same fixed extent contract and rebuild only private scaler resources; they do not require a fresh capability query or game-owned swapchain recreation. Factor and process-support changes still require a new capability contract.

### Variable-extent surfaces

Desktop window systems normally publish `UINT32_MAX` as `currentExtent`, so there is no compositor-owned native size to advertise during the capability query. MAKO preserves the application's requested extent as the source and requests a lower presentation extent enlarged by `scaling_factor`. It uses one aspect-preserving effective factor for both dimensions, clamps that factor to the Vulkan surface maximum extent, then rounds enlarged dimensions down to even values. Before allocating a variable lower swapchain, MAKO checks the exact candidate against a device-memory presentation envelope derived from the largest device-local heap. The envelope retains a 3840×2160 floor for unified-memory devices and otherwise budgets one presentation pixel per 768 heap bytes: one third of the heap against a conservative 256-byte-per-pixel combined active/retired spatial-plus-frame-generation estimate. If a resolution increase pushes the configured factor beyond that envelope, MAKO first aspect-fits the new source into the previous proven presentation extent. For example, a running 1920×1080→3840×2160 context changing to a 2560×1440 source retains 3840×2160 at an effective 1.5× instead of attempting 5120×2880 or dropping to a differently paced unscaled 1440p lower swapchain. If no prior proven extent is safe, the candidate stays native with `inactive_reason=variable-surface-memory-budget`. This blocks a cold 4K-to-8K expansion on an 8 GiB device while allowing 1080p-to-4K there, preserves non-widescreen ratios, and admits 4K-to-8K on a 24 GiB device. Fixed surfaces already own their actual presentation extent and do not use this variable-surface guard.

On a variable surface, configure the game or window to request the intended lower rendering resolution; MAKO cannot infer a separate monitor-native target from an unconstrained window or from Gamescope's Xwayland root geometry, which describes the game's logical size rather than the physical output. A factor that cannot enlarge both dimensions within the surface limits, or whose presentation exceeds the memory envelope without a safe previous presentation envelope, leaves scaling inactive.

Some Wayland compositors report MAKO's enlarged lower WSI extent back to the application as its next logical window size. MAKO records the last source/presentation pair per surface and treats an exact echoed presentation extent as compositor feedback rather than a new low-resolution source. That recreated swapchain stays native-sized and the record is retained so repeated echoes cannot compound the scaling factor. A genuinely different application-requested extent may activate scaling again. This conservative rule can also suppress an intentional source-size change that exactly equals the previous presentation size for the remaining surface lifetime. The guard does not apply to fixed extents and is cleared when the surface is destroyed. Direct variable-Wayland scaling can therefore become inactive after a compositor-driven recreation. MAKO Decky's ordered Gamescope WSI lane is the validated managed path, but a replacement context must still retain a genuine source/presentation split; a fixed native surface is valid only when the application consumes MAKO's advertised source capability.

## Swapchain, queue, and present-batch contract

The initial scaler supports only a conventional WSI swapchain with `imageArrayLayers = 1`, `VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR`, no `VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR`, and neither shared-demand nor shared-continuous present mode. “Single-layer” here means one swapchain image-array layer, not one Vulkan interception layer. Unsupported variable-extent swapchains stay on the unscaled native path. A fixed surface that was already virtualized cannot safely fall back after the application selected the advertised source extent, so an unsupported create-time shape, queue, or format is rejected instead of exposing an incomplete top-left image.

At device creation, scaling selects an ordinary application-created queue family whose device-queue create flags are zero and whose Vulkan family properties include both graphics and compute. Swapchain creation additionally proves that this family can present to the selected surface. A scaled `vkQueuePresentKHR` must arrive on queue index 0 from that same family; another family, a protected queue, or another queue index fails closed before MAKO submits spatial work.

Application wait semaphores belong to an entire `VkPresentInfoKHR` batch and can be consumed only once. MAKO does not yet implement the fan-out and per-swapchain `pNext` handling required for a batch containing a scaled swapchain. If `swapchainCount > 1` and any member has active spatial scaling, MAKO returns `VK_ERROR_UNKNOWN` and fills `pResults` when supplied before submitting work or consuming any application wait semaphore.

## Spatial reconstruction passes

The application renders into the source-sized top-left rectangle of a real presentation-sized WSI image. At present time, MAKO waits on the application's semaphores and copies that rectangle into the selected method's private source image.

MAKO Scaler then records this work:

1. Dispatch one 8-by-8-workgroup compute pass over the presentation extent.
2. Sample a clamped 4-by-4 neighbourhood for each output pixel, reconstruct with separable Catmull-Rom weights, and clamp the result to the local colour envelope.
3. Blend a small bilinear anti-ringing correction in high-contrast regions and apply a bounded, edge-aware sharpening term with a static 2x baseline multiplied by `scaling_sharpness`.

LS1 Quality runs three 16-by-16-workgroup neural passes: two source-sized RGBA8 feature stages followed by a learned luma-feature stage that writes a 2x-source `R8_SNORM` image. LS1 Performance replaces those three passes with one lower-cost learned pass that writes the same feature representation. Both modes then run the common output-resolution LS1 reconstruction pass, which combines the learned feature image with the original source through the model's linear sampler and colour reconstruction. The five DLL model variants are selected by rounding `scaling_sharpness * 4`.

After either method, MAKO copies the reconstructed image back across the complete WSI image and, when frame generation is active, into its full-resolution exported source image, then returns the WSI image to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` before the lower present. Explicit compute-to-compute barriers separate LS1's learned stages; all images, pipelines, descriptors, and constants are created once with the swapchain.

Incremental-present regions are removed from the lower `pNext` chain while scaling is active because application damage rectangles use source coordinates and cannot safely describe the reconstructed presentation image.

## Formats and HDR boundary

Spatial scaling currently supports swapchains classified by MAKO's colour pipeline as:

| Encoding | Spatial working format | Intended input |
| --- | --- | --- |
| `Sdr8` | `VK_FORMAT_R8G8B8A8_UNORM` | MAKO directly; LS1 through its native RGBA8 model resources |
| `SdrHighPrecision` | `VK_FORMAT_R16G16B16A16_SFLOAT` | MAKO directly; LS1 with Vulkan blit conversion at the RGBA8 model boundary |

The surface must support transfer source and destination usage. At least one advertised SDR format must be compatible before MAKO virtualizes a fixed extent. The selected application format must support blit source and destination operations, and the spatial working format must support blit, sampling, and storage-image operations with optimal tiling. LS1 keeps its discovered learned graph in its native `R8G8B8A8_UNORM` and `R8_SNORM` resources even when MAKO's SDR exchange transport is `R16G16B16A16_SFLOAT`; the boundary blits perform the normalized SDR conversion before and after the model. MAKO repeats the selected-format checks at swapchain creation and rejects an unsupported fixed-surface selection rather than presenting an incomplete image. Unrelated advertised HDR or unsupported formats do not block a compatible selected SDR format.

HDR10/PQ, scRGB HDR, HLG, Dolby Vision, and unsupported colour pairs are outside the current scaling contract. All scaling under Gamescope requires HDR exposure to be disabled at process start, normally through `MAKO_DISABLE_HDR_EXPOSURE=1`; if Gamescope is detected while HDR exposure is allowed, MAKO does not select scaling on either fixed or variable surfaces. A swapchain classified as HDR never activates the spatial scaler on any desktop. Treat this as a fail-closed boundary: HDR scaling is not available, and an HDR test that merely continues presenting real frames is not positive scaling evidence. See [HDR pipeline architecture](HDR-PIPELINE.md) and [Gamescope WSI isolation](WSI-ISOLATION.md) for the independent colour and presentation contracts.

## Resource and performance model

The spatial stage allocates nothing when scaling is disabled or unsupported. Native Resolution owns one source-sized and one presentation-sized transfer image and records linear blits, but uses no shader, descriptor, parameter buffer, licensed resource, or compute dispatch. Frame Generation resources are independently retained for live switching and perform no per-frame work while Off. MAKO and LS1 add method-specific sampled/storage images, immutable parameters, pipelines/descriptors, and one reusable command buffer and ready semaphore per WSI image.

MAKO adds one compute pipeline and descriptor set. LS1 Performance adds a 2x-source single-channel signed-normalized feature image and two compute pipelines. LS1 Quality additionally adds two source-sized RGBA8 feature images and two compute pipelines. The LS1 model weights live in the translated shaders rather than separate runtime buffers.

Those objects are created with the swapchain and reused. The present hot path performs no spatial-resource allocation or shader translation. The scaler runs on the application's selected Vulkan device, so the spatial stage itself does not introduce a cross-device image boundary; optional frame-generation resources retain their existing private-device contract. In the combined path, the existing frame-generation source receives the reconstructed full-resolution frame once.

The frame-generation backend reuses Gamma scratch images in the immediately following same-extent Delta stages. The two stages execute sequentially in one command buffer, so this is reuse of the same Vulkan images rather than overlapping-memory aliasing. Remaining internal model images use a context-local non-aliasing linear device-memory pool with unique aligned ranges and bounded block growth. Imported source mappings, exported transport images, host-visible buffers, and spatial-role resources remain outside that pool and preserve their dedicated ownership. Pool construction occurs only while the backend context is built; no allocation, lookup, or locking is added to frame dispatch. Stable `backend-memory` and `renderer-memory` records report internal, imported-mapped, exported, live, and peak byte/allocation totals so MAKO Gym can reject footprint drift independently of process RSS.

The shader uses nine bilinear texture samples for exact Catmull-Rom folding plus four explicit center-texel loads per output pixel. Sharpening is folded into the reconstruction pass rather than dispatched separately. This keeps command submission, descriptors, image count, and synchronization bounded across scaling factors and frame-generation multipliers, but it does not make scaling free: validate GPU time, memory, base cadence, and presentation behavior on every supported hardware class.

The GLSL source, generated SPIR-V arrays, and hash manifest live under `mako-render/src/shaders/`. `scripts/generate-spatial-scaling-spirv.py` owns regeneration and its read-only `--check` mode is part of CTest. Do not edit the embedded header or hashes independently.

## Evidence ownership

Do not preserve dated development results or changing case counts in this architecture guide. MAKO Gym owns real-hardware visual, performance, repeatability, synchronization, Gamescope, and Proton evidence; MAKO Traces owns comparative game captures. [Testing MAKO](../../TESTING.md) defines the release gates. Record the exact commit, build, GPU, driver, runtime, settings, and unsupported boundaries with each retained result.

## Configuration

The four scaling settings are independent from Fixed and Adaptive Frame Generation:

```toml
[[profile]]
name = "Scaled game"
active_in = ["Game.exe"]

scaling_enabled = true
scaling_method = "ls1"
scaling_factor = 2.0
scaling_sharpness = 0.9

# Scaling-only:
frame_generation_enabled = false
```

| Setting | Range | Default | Meaning |
| --- | --- | --- | --- |
| `scaling_enabled` | Boolean | `false` | Enables scaling for the process. Enable it before starting the game; when it is off, scaling is fully disabled. Changing it requires a game restart; MAKO Decky also stages Gamescope WSI when enabled. |
| `scaling_method` | `native`, `mako`, `ls1`, `ls1-performance` | `native` | Selects the Native Resolution model-free linear baseline, MAKO Scaler, LS1 Quality, or LS1 Performance. Method changes rebuild MAKO's private scaler without game-owned WSI recreation after the engine is provisioned. LS1 requires the licensed DLL and vkd3d-shader; failure uses MAKO Scaler and logs why. |
| `scaling_factor` | 1.0–2.0 | `2.0` | Ratio from each source dimension to each presentation dimension; 1.0 performs no scaling work. |
| `scaling_sharpness` | 0.0–1.0 | `0.9` | Multiplier for MAKO's continuous sharpening at its static 2x baseline, or LS1's nearest one of five learned model variants. |

To combine scaling with Fixed Frame Generation, set `frame_generation_enabled = true`, leave `adaptive = false`, and select `multiplier`. To combine it with Adaptive Frame Generation, enable both `frame_generation_enabled` and `adaptive`, then configure the normal Adaptive target, ceiling, and cadence options. No scaling-specific frame-generation mode exists.

For an environment-only profile, set `MAKO_ENV=1` and use `MAKO_SCALING_ENABLED`, `MAKO_SCALING_METHOD`, `MAKO_SCALING_FACTOR`, and `MAKO_SCALING_SHARPNESS`. The normal frame-generation variables remain independent.

LS1 translator discovery is architecture-aware. MAKO tries stable platform libraries from Steam Linux Runtime 4, sniper, soldier, and the legacy runtime beside the Lossless Scaling Steam library, then the system `libvkd3d-shader.so.1`. `MAKO_VKD3D_SHADER_PATH=/absolute/path/to/libvkd3d-shader.so.1` can select an explicit architecture-matched translator for troubleshooting or a nonstandard Steam layout. It changes only setup-time translation; no translator call occurs in the frame path.

Scaling enablement is process-static. With scaling provisioned, all methods share immutable extents and the context retains DLL discovery information so a later LS1 selection needs no application-visible WSI rebuild. Method and debounced sharpness transitions construct the replacement first, wait only on MAKO-owned per-pass completion fences, retain the old scaler on failure, warm frame-generation history, and leave the game swapchain and Gamescope WSI object untouched. Flow Scale, Lighter FG Model, and generated capacity independently use the same centralized private-resource lifecycle for the upper FG context. Scale Factor retains its game-owned recreation boundary but can request that boundary once from the lower spatial role after retirement proof; pacing remains natural-only. Mixed writes apply compatible fields while retaining process-static baselines. [Runtime configuration transitions](RUNTIME-TRANSITIONS.md) owns these lifetimes, synchronization, rollback, retry, status, and diagnostics contracts.

## Validation

Portable tests prove parsing and policy, not Vulkan image quality or presentation. Run the normal Renderer suite and sanitizer path described in [Testing MAKO](../../TESTING.md). The focused deterministic boundaries include:

- configuration defaults, accepted ranges, TOML round trips, and environment overrides;
- fixed-surface capability virtualization, source rounding, exact latched capability/create matching, policy and surface invalidation, application-override no-split diagnostics, variable-surface enlargement, echoed-presentation feedback suppression, Vulkan-maximum clamping, device-local-memory presentation budgets at 2 GiB, 8 GiB and 24 GiB across Deck, 4K, 5K and 8K cases, disabled and 1.0-factor behavior;
- opaque single-array-layer, unprotected, non-shared-present swapchain-shape policy;
- process-static scaling projection, private Native/MAKO/LS1 method rebuilds, debounced private sharpness rebuilding, private FG model/Flow/capacity replacement, and natural-recreation deferral for extent and pacing changes; and
- GLSL, SPIR-V payload, and hash freshness for both SDR working formats.

Run `scripts/run-mako-gym.sh` with the private sibling MAKO Gym checkout for hardware validation. Gym owns native-Vulkan construction, procedural pixels, repeatability, performance, exact allocation accounting, synchronization validation, real Gamescope WSI, D3D11/DXVK, D3D12/VKD3D-Proton, runtime-family compatibility, assertions, logs, and summaries. Its docs are authoritative for current scenarios and counts; [Testing MAKO](../../TESTING.md#selecting-mako-gym-coverage) decides when each lane is optional or required. These lanes do not replace title-specific compatibility, subjective review, another driver, or another hardware class.

Every spatial-scaling change also needs proportionate real-Vulkan evidence. Use this matrix and record unavailable rows as **not tested**:

| Boundary | Required cases |
| --- | --- |
| Surface ownership | Fixed Gamescope surface with HDR exposure disabled and variable desktop surface; source and presentation extents must match the policy logs, an echoed variable presentation extent must fall back without recursive enlargement, and a variable candidate beyond the device-memory envelope must either reuse a safe proven presentation envelope or remain native. Cover Vulkan-maximum clamping, 4:3 or 5:4 preservation, ultrawide, cold 5K rejection plus 1080p→4K→1440p retained-4K transition on an 8 GiB tier, and an admitted 8K row on a 24 GiB tier. |
| Operation mode | Scaling with Frame Generation Off must perform no per-frame generation work while retaining successfully provisioned resources for live enablement. Also cover startup where backend/interop is unavailable but MAKO Scaler remains independently usable, LS1 DLL translation, every method with Fixed 2x and Adaptive, and restart-pending enablement after failed provisioning. |
| Swapchain shape | Opaque single-array-layer ordinary swapchain plus fail-closed non-opaque, array-layer, protected, shared-demand, and shared-continuous cases. |
| Queue ownership | Ordinary queue 0 from the application-created graphics-and-compute family that presents to the surface; wrong family, wrong index, and protected queue must fail closed. |
| Present batching | One scaled swapchain succeeds; a multi-swapchain batch containing scaling returns before any application wait semaphore is consumed. |
| Format | Standard 8-bit and high-precision SDR for every method; prove all sampled/storage/blit format features and LS1's RGBA8 boundary conversion. |
| Tuning | Factor 1.0 inactive behavior plus representative 1.5 and 2.0 runs; MAKO sharpness 0.0, 0.5, and 1.0; all five LS1 model variants. |
| Lifecycle | Initial create, focus and overlay transitions, private Native/MAKO/LS1 model and sharpness changes with zero application-visible recreation, simultaneous or superseded private FG model/Flow/capacity changes, coalesced Scale Factor request with exactly one retirement-fenced out-of-date signal, combined factor plus Fixed multiplier/capacity and factor plus Fixed-to-Adaptive ceiling/capacity changes, unsupported-path factor deferral, game-owned resolution change, natural and requested swapchain recreation, process-restart scaling changes, profile update, and shutdown. |
| Launch boundary | Native Vulkan, DXVK, and VKD3D-Proton under the supported Gamescope path; direct desktop and each changed Flatpak/runtime path. |
| Frame-generation precision | Combined-path FP32 and FP16 runs; spatial RGBA16F transport is not a substitute for LSFG FP16 evidence. |
| Fail closed | Missing/wrong DLL, missing translator, missing LS1 resource, unsupported LS1 resource format or format feature, Gamescope with HDR exposure allowed, explicit HDR formats, unsupported swapchain shape, incompatible presentation queue, maximum-extent exhaustion, and scaled multi-swapchain batches. LS1-specific failures must log and use MAKO; Frame Generation provisioning remains independent. |
| Quality | Fine text, UI edges, thin geometry, diagonal motion, foliage, particles, gradients, high-contrast edges, and repeated camera motion against native-resolution reference captures. |
| Performance | Scaling GPU time, base real-frame cadence, displayed cadence, device/unified memory, frame pacing, and startup/recreation cost on low-power and desktop hardware. |

Use the Vulkan validation layer when it is installed and preserve `VK_LOADER_DEBUG=layer` evidence that the development build, rather than an older installed layer, was activated. An implicit MAKO layer must be selected through its development manifest path; setting only the explicit-layer path is not proof. Inspect `MAKO Renderer: spatial scaling surface virtualized`, `spatial scaling swapchain policy`, and `spatial scaling active` records for the selected dimensions, factor, format, and active decision. The active record reports both configured `factor` and the actual extent-derived `effective_factor`, which can be lower when a higher source resolution retains a previously proven presentation envelope. The policy record exposes fixed/variable surface mode, `device_local_heap_mib`, `variable_presentation_pixel_budget`, `previous_presentation_budget_reused`, advertised and actual source/presentation extents, current and contract policy revisions, query generation, the stable `inactive_reason`, and `source_presentation_split`. `inactive_reason=variable-surface-memory-budget`, `active=0`, or `source_presentation_split=0` proves that no scaler ran even if a model was selected in configuration. Private spatial changes emit `action=rebuild-private-scaler` followed by `transition=private-context`. Private FG changes emit `action=prepare-private-context`, `runtime-transition-prepared`, and `runtime-transition-applied transition=private-context` with the effective Flow Scale, model, capacity, and history warm-up. An eligible Scale Factor change emits `signal-out-of-date-after-retirement-fenced-present`, exactly one `runtime-transition-recreation-requested`, a lower-role `swapchain-recreation-observed source=guarded-live-profile-request`, and a replacement `spatial scaling active` record with the new factor. Pacing and ineligible factor changes retain natural-recreation diagnostics.

A skipped GPU test is not scaling evidence. Offscreen pixels, timestamp queries, synchronization validation, Gamescope WSI, Proton translation, runtime-family checks, subjective captures, power/latency measurement, and real-title compatibility are separate evidence boundaries.

Scaling teardown follows the natural replacement and surface-terminal retirement contract in [Runtime configuration transitions](RUNTIME-TRANSITIONS.md). MAKO Gym must prove deferred and completed retirement, no forced cleanup, and clean client/Gamescope termination.

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
| DLL identity and compatibility inspection | `mako-backend/src/extraction/dll_reader.cpp`, `mako-backend/src/extraction/model_resource_validation.cpp`, `mako-cli inspect-dll` |
| Embedded shader generation and freshness | `scripts/generate-spatial-scaling-spirv.py`, exercised by CTest |
| Configuration schema and validation | `mako-common/include/mako-common/configuration/config.hpp`, `mako-common/src/configuration/config.cpp` |
| Deterministic policy and private live-transition semantics | `mako-render/tests/spatial_scaling_policy_tests.cpp`, `mako-render/tests/profile_update_tests.cpp` |
| Procedural scenes, ideal references, masks, and scoring | `mako-common/src/quality/image_quality.cpp`, `mako-common/tests/image_quality_tests.cpp` |
| Offscreen spatial and combined real-GPU execution | `mako-cli/src/tools/quality.cpp`, private MAKO Gym quality matrix |
