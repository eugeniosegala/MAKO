# Spatial scaling architecture

MAKO Renderer can reconstruct a lower-resolution Vulkan swapchain to the presentation extent, with or without Frame Generation. Scaling is disabled by default and is independent from Fixed or Adaptive scheduling. [Configuration](CONFIGURATION.md) owns user settings; [runtime transitions](RUNTIME-TRANSITIONS.md) owns live, deferred, and restart boundaries. [Memory management](MEMORY-MANAGEMENT.md) explains allocation, pooling, accounting, and resource cleanup around the admission policy below.

## Scaling methods

| Method | Implementation | Requirements |
| --- | --- | --- |
| `native` — Native Resolution | Model-free linear reconstruction through Vulkan blits | None beyond the normal Renderer requirements |
| `mako` — MAKO Scaler | Repository-owned single-pass spatial reconstruction with sharpening and anti-ringing | None beyond the normal Renderer requirements |
| `ls1` — LS1 Quality | Complete proprietary LS1 graph | A lawful, user-supplied `Lossless.dll` and an architecture-matched `libvkd3d-shader.so.1` |
| `ls1-performance` — LS1 Performance | Lower-cost proprietary LS1 graph | A lawful, user-supplied `Lossless.dll` and an architecture-matched `libvkd3d-shader.so.1` |

MAKO never packages, uploads, or modifies `Lossless.dll`. It reads the selected resources from the user's file, validates their containers and bindings, translates supported LS1 or LSFG DirectX bytecode during setup, and keeps the resulting shader modules inside the process. Compatibility is capability-based rather than tied to an allowlist of DLL versions. `mako-cli inspect-dll --dll <path>` performs the GPU-independent checks; Vulkan pipeline construction remains device-specific.

LS1 and LSFG use the shared resolver in `mako-backend/src/extraction/model_resources.cpp`. It first checks the requested model at the established resource IDs, including known LS1 reflection when present. If those resources do not match, it searches for a unique complete supported table whose IDs have shifted by one common offset. LS1 discovery requires all five variants of both modes and their SM5.0 reflected interfaces; LSFG discovery requires both modes, both precision lanes, matching descriptor interfaces, and the established precision order. The runtime LSFG registry resolves both modes together so their shared stages come from one table. Extra unrelated resources and changed file hashes do not prevent loading. Resolution successes and failures are cached on the immutable process-local archive, outside the frame path; replacing the DLL creates a new archive and resolution cache.

When no supported Vulkan LSFG registry exists at the selected precision, `mako-backend/src/extraction/lsfg_shader_set.cpp` validates the existing DirectX LSFG graph and translates it through the same dynamically loaded `libvkd3d-shader.so.1` owner used by LS1. Its canonical and relocated DirectX tables require exact SM5.0 resource reflection, and relocation requires both modes together. The Vulkan registry remains preferred. Ordinary builds translate DirectX only when FP16 is off; an FP16 request requires a supported native FP16 registry and fails explicitly if it is missing. The separately selected [experimental native build](BUILDING-FROM-SOURCE.md#experimental-forced-lsfg-fp16) uses the same FP16 setting to convert a supported FP32-only graph, including DirectX translation, to explicit half arithmetic. Native FP16 still takes priority and FP16-off never invokes conversion. Conversion is unqualified for image quality and logs that status. MAKO never silently substitutes FP32 for an FP16 request. Translation and conversion are confined to initialization and cached on the immutable DLL archive in process memory; replacing the DLL gets a fresh cache. Neither the DLL nor its resources are rewritten or saved.

Discovery is bounded to 4,096 resources and rejects partial or ambiguous relocated tables. It does not infer arbitrary stage reorderings, new model architectures, or compatibility from similar hashes or binding counts alone. Translation and Vulkan pipeline checks still apply. This supports compatible resource relocation on fresh installations without retaining an older DLL, but cannot guarantee compatibility with every future Lossless Scaling update or its separate `lsfg-vk.dll` backend.

`mako-cli inspect-dll --dll <path> --lsfg` performs an LSFG-only preflight through the complete runtime shader loader at the selected FP16 precision, without LS1 translation or GPU setup. Add `--no-fp16` to select FP32, including the DirectX fallback. Native Vulkan registries need no translation library; a DirectX-only LSFG DLL requires an architecture-matched translator. Like the selected LS1 probe, it emits only `{"schema_version":1,"compatible":true|false}` and exits 0 for compatible or 1 for a model failure. Unknown options or unavailable inspectors are not model-failure evidence. MAKO Decky uses this alongside the selected LS1 probe to show a top warning and update guidance; [Decky configuration](../../plugin/docs/CONFIGURATION.md) owns the UI and cache contract.

If LS1 discovery, translation, format support, or pipeline creation fails, the swapchain falls back to MAKO Scaler and records the requested method, active method, and reason. Native Resolution and MAKO Scaler do not require the licensed file or translator.

For one saved selection, `mako-cli inspect-dll --dll <path> --ls1 ls1 --sharpness 0.8 --no-fp16` (or `--ls1 ls1-performance`) calls the same selected-variant loader as the runtime and emits only `{"schema_version":1,"compatible":true|false}` on stdout. Exit 0 means the selected resources translated; exit 1 with a valid status means LS1 setup failed. Command errors, absent inspectors, and malformed output are unknown to consumers. On the established resource layout, unrelated LSFG families or other LS1 variants do not veto this probe; relocated-table discovery requires the complete LS1 table described above. MAKO Decky runs it off its event loop after a 500 ms control debounce, checks file identities every 30 seconds while the LS1 controls are present, and caches one result per model family for at most five minutes. DLL or inspector replacement invalidates that cache immediately at the next check, including same-size/restored-mtime replacements; expiry also retries translator changes. No probe writes a profile or persists licensed resources. Actual runtime fallback retains the requested LS1 method, constructs MAKO Scaler, and publishes the requested/active distinction. A working fallback remains active until another private-scaler construction boundary, avoiding periodic translation in the frame path.

## Arithmetic precision

The experimental forced-LSFG path uses mixed precision: eligible feature arithmetic is converted to FP16, while shared mipmap preparation (logical stage 255) and final image reconstruction (logical stage 256) retain their original FP32 modules. Reducing those image stages to half arithmetic caused severely dark generated frames on the tested beta model; alternating them with real frames can produce brightness flicker. Within feature stages, sampling coordinates, LODs, offsets, and bit-pattern dependencies retain FP32, including values passed through private/function memory. Conservative protection covers all writes to a referenced variable and stops at sampled feature values, preserving half arithmetic where it does not feed these precision-sensitive paths. The tested beta graph reports 28 stages containing actual half arithmetic, while some feature stages remain entirely FP32. Preserving these paths corrects the tested procedural brightness and motion-edge failures without altering native FP16 selection or the FP16-off FP32 path; actual game quality remains unqualified.

The global `allow_fp16` setting is a restart-bound precision choice shared by Frame Generation and scaling, including Ultra Performance, swapchain recreation, live scaler replacement, and fallback construction. MAKO Scaler has generated FP32 and FP16 colour variants for both RGBA8 and RGBA16F storage. Pixel coordinates and cubic sample locations remain FP32 to preserve subpixel accuracy at large extents; SDR storage stays RGBA8 rather than expanding merely because arithmetic is FP16. Native Resolution uses the driver's blit path without a configurable arithmetic precision.

Scaling requests `shaderFloat16` on the application device only when its feature and extension are supported, reuses an existing Vulkan 1.2 or extension feature structure without duplicating it, and copies known feature-chain prefixes without modifying application input storage. An unknown prefix preserves the original request instead of truncating a Vulkan structure. A physical support bit alone is not proof that FP16 was enabled on the logical device. If FP16 is unavailable on that logical device, scaling reports the limitation and uses the existing Native Resolution blit, preserving the source/presentation geometry without substituting FP32 compute. Select FP32 to enable compute scaling on that device.

LS1's currently supported DirectX translation is FP32. With FP16 on, its loader reports that limitation and the existing method fallback constructs MAKO Scaler at FP16, preserving the requested/active method distinction and reason. With FP16 off, LS1 runs at FP32. `inspect-dll --ls1` checks the same precision-specific loader; add `--no-fp16` for LS1 FP32. Spatial quality and profiling commands also accept `--allow-fp16` and `--no-fp16`, and combined validation passes the same choice to both stages. Shader conversion to FP16 is not enabled merely because it compiles: it must also meet image-quality and performance gates.

## Activation and ownership

Scaling must be enabled before the process starts because it changes layer membership and swapchain geometry. Frame Generation provisioning is an independent process-start choice: a Scaling-only profile keeps the combined Renderer but omits LSFG device interop, backend ownership, and generated-frame resources. Once a scaled process is provisioned, method and sharpness changes replace only private scaler resources. A factor or supersampling change may need a game-owned swapchain recreation when it changes the effective source/presentation pair.

Standalone `mako-launch` and MAKO Decky with Gamescope WSI off use one combined Renderer role for scaling and Frame Generation, including inside Gamescope. Scaling does not load Gamescope WSI automatically. When both Scaling and the independent Gamescope WSI compatibility option are enabled in a supported session, MAKO Decky uses three ordered roles:

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

When Scaling is on and WSI is off inside a supported Gamescope session, the combined Renderer uses a minimal X11-to-Wayland surface association. This provides an internal variable-extent surface for games such as Proton titles that explicitly request their window size, while preserving concrete X11 extents in application capability queries. The adapter also exposes the application's X11 surface formats in their original order, restricted to format/color-space pairs supported by the private Wayland surface; failed format proof leaves the lower driver's list untouched. The game request remains the source and the ordinary scaler policy chooses the output; no fixed-extent contract is bypassed. The adapter is provisioned at process start, remains through live scaler changes, and is absent when the full WSI chain already supplies the association. It creates one Gamescope protocol object per Vulkan swapchain, supplies the actual image-count and format feedback at creation, and reasserts the X11 window association during presentation so a Steam UI transition cannot leave Xwayland's mapping in control. Combined ordered Frame Generation uses lower MAILBOX only when the driver advertises it and a valid Gamescope refresh clock is known; otherwise it retains lower FIFO. Each generated and real lower present carries its own Gamescope FIFO annotation and distinct future presentation time. The timing policy spaces outputs at the active output ceiling and reserves the larger of two refresh intervals or one actually scheduled output batch for asynchronous GPU readiness on every submission. A previously reserved future deadline does not prove that new GPU work can meet it; shortening this readiness lead can cause the compositor to discard outputs even while Renderer present counts appear healthy. Application completion waits can therefore reduce throughput on this transport; preserving generated-output delivery remains required. Submission remains bounded to the readiness lead plus one refresh interval (25 ms for 2× at 120 Hz; 33.3 ms for 3×). Unused maximum-multiplier capacity adds no delay. Explicitly disabled Frame Generation (including live 0× with resources retained) removes the generated-batch readiness lead while retaining output spacing and previously queued deadlines; application presentation waits must not turn that unused lead into a two-refresh source limit. Active Fractional one-output turns, warm-up and temporary native relief retain the generated timeline. FIFO annotations alone are insufficient: outputs that become ready together can still be discarded. The bounded timeline avoids the extra lower Wayland FIFO callback wait, retains ordering across live target/refresh changes, and rebases after stalls. Adaptive follows its target capped at refresh; exact Fixed with an explicit source cap follows cap × multiplier capped at refresh; uncapped Fixed and live generation-off follow refresh. Protocol timing state follows the Vulkan swapchain lifetime, including native fallback after backend initialization failure. Scaling-only retains its existing lower present-mode owner. The adapter does not load the Gamescope WSI layer or issue its limiter or HDR control requests. Missing session, protocol, library, or window proof preserves ordinary native surface handling.

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

<a id="extent-ownership"></a>

## Extent policy

Each active scaled swapchain has two extents:

- **Source:** the image size presented to the application and sampled by the scaler.
- **Presentation:** the physical lower WSI size and final output resolution.

Dimensions greater than one are rounded down to even values. Scaling remains inactive when the factor is 1.0, the extents cannot differ safely, or a required capability cannot be proven. An enabled 1.0 profile retains the live reconstruction lane but uses Frame Generation-only WSI relief topology until a factor above 1.0 can activate scaling.

### Fixed-extent surfaces

For a surface with a concrete `currentExtent`, MAKO advertises a source extent derived from the presentation extent and configured factor. Swapchain creation activates scaling only when the application requests the exact advertised source under the current surface and policy contract. If the application instead requests the native presentation extent, MAKO preserves that choice and creates a native context.

In the managed split chain, the upper and lower roles exchange capability and create decisions through same-thread, one-shot relays because Gamescope WSI may replace the surface handle between them. A missing, stale, or mismatched lower decision fails closed instead of creating a transient context with the wrong geometry.

The lower split role must observe a Wayland surface created through Gamescope WSI. If it sees the application's XCB or Xlib surface, Gamescope WSI did not establish the required ownership boundary; scaling stays native with `inactive_reason=gamescope-wsi-surface-unproven`. The direct combined Renderer owns its application surface and does not require this split-only proof.

### Variable-extent surfaces

For a surface whose `currentExtent` is variable, the application request is the source. MAKO enlarges it by one aspect-preserving effective factor, subject to Vulkan surface limits and memory admission.

The managed Gamescope split chain and the combined Renderer's isolated Gamescope surface adapter require a positively identified output target from the server-zero feedback resolver and treat it as the normal presentation ceiling. If the source already fills that target, scaling stays native with `inactive_reason=gamescope-presentation-target-no-headroom`. Quality Supersampling may exceed the target, but it cannot bypass Vulkan limits, memory admission, or the requirement to prove the Gamescope target. Ordinary combined surfaces use available Gamescope target feedback without requiring this adapter-specific proof. Direct non-Gamescope operation applies the factor without inventing a compositor target.

For the isolated X11 surface adapter, applications continue to query a concrete live window size through both Vulkan capability APIs. The private Wayland surface remains variable only inside the Renderer, where the existing source/output and unsupported-scaling checks apply. This preserves startup compatibility for XCB/Xlib applications without a game-specific exclusion. See [WSI isolation](WSI-ISOLATION.md) for the contract and diagnostic fields.

The adapter checks the live window again when creating a swapchain protocol object. An application extent matching the last capability query or the live window follows that observed window contract. If the window changes and makes that extent stale, the next application image acquisition returns `VK_ERROR_OUT_OF_DATE_KHR` before the lower acquisition runs or an application semaphore or fence is signalled. Later capability queries also invalidate mismatched swapchains following the window contract; the application must replace an invalidated swapchain even if the window returns to its former size. Gamescope can retain the window size while the application deliberately requests a different rendering extent: an extent matching neither observation remains an application override, rather than entering a recreation loop. Explicit swapchain-maintenance presentation scaling retains the lower driver's negotiated extent contract. This protects startup and recreation query/create races without inventing a larger source or changing an intentional low-resolution input. Creation and capability queries are the observation boundaries; MAKO does not add per-frame window polling or claim to detect a resize that has not reached either boundary.

The memory policy admits a presentation extent from device-local heap size and, when available, the driver's live budget and usage. For a null-old replacement whose predecessor the application already destroyed, complete the existing fence-protected retirement before sampling that budget. Query the driver again without subtracting guessed released bytes; a delayed budget update can still require native fallback. When a live source-resolution change is rejected only by that live estimate, its requested output remains inside the same-placement presentation envelope already proven on the surface, and the native replacement presents successfully with maintenance1 retirement protection, MAKO preserves the proof and requests one further game-owned recreation after three seconds to re-sample the driver. Output growth, placement changes, cold starts, other inactive reasons, and a second rejection cannot use this retry. The policy otherwise preserves already proven envelopes across safe live transitions and fails closed when the enlarged swapchain and private resources do not fit. Runtime status reports the requested and effective factor plus the active constraint or inactive reason; it does not promise an exact free-memory measurement.

The static memory envelope keeps a 3840×2160 compatibility floor for unified-memory apertures; the live budget has no such floor. Separate graph estimates charge Frame Generation at presentation resolution through 1920×1200 pixels and source resolution above it. Both retain a conservative full-Flow Quality allowance for live changes, and 5× reserves four generated outputs. FP16 changes arithmetic rather than these SDR allocation formats. Proven-envelope reuse requires the same Frame Generation placement; a post-FG-to-pre-FG downshift needs fresh admission even when its dimensions shrink. Source growth is charged at the conservative 256-byte resource rate independently of cheaper output-only pixels.

Memory admission is a conservative allocation estimate, not a measurement of spare GPU execution time or a promise that a scaling factor will sustain the target FPS. It uses the selected application's Vulkan device and its largest device-local heap, not GPU-name tiers, clock rates, or total system RAM. The [Vulkan memory-budget contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMemoryBudgetPropertiesEXT.html) defines budget and usage as changing estimates; the reserve and static fallback cannot guarantee allocation success, predict driver-specific tiling/alignment, or model every multi-heap/cross-device topology. Actual resource creation and the existing failure handling remain authoritative. MAKO Gym compares the cold graph estimate against observed private Vulkan allocations plus nominal WSI bytes, and exercises live memory pressure separately from performance tests.

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

`MAKO_VKD3D_SHADER_PATH=/absolute/path/to/libvkd3d-shader.so.1` overrides the shared LS1/LSFG DirectX translator discovery for a nonstandard installation. It affects setup only.

See [Configuration](CONFIGURATION.md) for exact defaults and ranges. See [Runtime configuration transitions](RUNTIME-TRANSITIONS.md) before changing factor, supersampling, method, or sharpness while a game is running.

## Validation

Portable CTest covers configuration, fixed and variable extent policy, Gamescope target handling, memory admission, swapchain shape and queue requirements, pipeline placement, private transitions, and embedded shader freshness. These tests do not prove Vulkan image quality, layer order, presentation, or hardware memory behavior.

Real-Vulkan changes need proportionate MAKO Gym evidence for both FP32 and FP16 where Frame Generation is involved. Select the applicable suites from [Testing MAKO](../../TESTING.md), including quality, spatial performance, synchronization, Gamescope, recovery, native Vulkan, DXVK, VKD3D-Proton, direct desktop, and supported Flatpak/runtime paths. Record unavailable hardware and matrix rows as not tested.

Positive split-chain scaling evidence requires the ordered three-role loader chain, Wayland provenance at the lower role, a source/presentation split, `inactive_reason=none`, one active upper reconstruction owner, and correct real/generated delivery. A selected method alone does not prove that scaling ran. For the combined path, require isolated Gamescope WSI, no lower spatial role, and an active source/presentation split in the combined Renderer. Compare WSI on/off performance only at the same proven source and presentation sizes, scaler, Frame Generation settings, scene, and refresh rate; native fallback is not a scaling performance improvement. Use `VK_LOADER_DEBUG=layer` only for focused loader captures and the `scaling`, `layers`, and `recovery` diagnostics presets from [Collect diagnostics](COLLECT_DIAGNOSTICS.md).

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Extent, placement, format, queue, and memory policy | `mako-render/src/spatial_scaling_policy.hpp` |
| Surface interception and split-role relays | `mako-render/src/entrypoint.cpp` |
| Swapchain activation and resources | `mako-render/src/instance.cpp`, `mako-render/src/spatial_scaler.cpp` |
| Scaling-only and combined presentation | `mako-render/src/swapchain/present.cpp`, `mako-render/src/swapchain/present/real_frames.cpp` |
| MAKO shader source and generated payload | `mako-render/src/shaders/`, `scripts/generate-spatial-scaling-spirv.py` |
| Shared LS1/LSFG resource resolution, validation, and DirectX translation | `mako-backend/src/extraction/`, `mako-cli inspect-dll` |
| Portable policy and transition coverage | `mako-render/tests/spatial_scaling_policy_tests.cpp`, `mako-render/tests/profile_update_tests.cpp` |
| GPU quality and runtime evidence | `mako-cli/src/tools/quality.cpp`, private MAKO Gym suites |
