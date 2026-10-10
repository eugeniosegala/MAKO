# HDR pipeline architecture

This guide defines MAKO Renderer's colour handling and its Gamescope HDR boundary. [WSI isolation](WSI-ISOLATION.md) owns Vulkan-layer discovery and presentation ownership.

<!-- prettier-ignore -->
> [!IMPORTANT]
> HDR is an explicit, restart-only opt-in for testing through MAKO’s isolated Gamescope surface bridge. Keep **Gamescope WSI** off. HDR10/PQ and linear scRGB are wired through Scaling, Frame Generation, and the colour-aware MAKO vkBasalt shader fork. Portable tests and package builds do not establish display colour accuracy, game compatibility, or HDR performance; the built-in OLED, external-display, and game matrix still needs qualification.

## Separate colour and presentation decisions

Two independent decisions are involved:

1. The game swapchain encoding, cross-device exchange format, and model working space define the colour pipeline. Stable compositor feedback may replace these private resources while the process runs.
2. The isolated surface bridge uses MAKO’s existing ordered presentation for both SDR and HDR. The legacy full-Gamescope-WSI HDR transport remains separate. Transport is selected before the swapchain is created and cannot change live.

Do not treat a private colour-resource transition as permission to add Gamescope WSI or reinterpret an existing swapchain's transport.

## Toggle and launch contract

The normal managed launch establishes this policy before Vulkan starts:

```text
DISABLE_GAMESCOPE_WSI=1
MAKO_DISABLE_HDR_EXPOSURE=1
DXVK_HDR unset
```

Turn **Disable HDR (Restart)** off in the MAKO Decky game profile or the Qt UI’s **Launch Settings**, then restart the game and enable its HDR option. The launch exports `MAKO_DISABLE_HDR_EXPOSURE=0` and `DXVK_HDR=1` while retaining `DISABLE_GAMESCOPE_WSI=1`. Decky stores this per profile; Qt stores it globally in `launcher.conf`. Turning the toggle back on restores the default SDR launch. Scaling and Frame Generation switches remain independent; HDR alone can provision the surface bridge without allocating either processing engine.

With HDR disabled, the bridge does not query HDR output capability, append HDR formats, emulate HDR metadata, or send HDR protocol requests. If Scaling is enabled, its existing surface association still operates. With both Scaling and HDR disabled, no isolated bridge connection is provisioned.

Steam Deck OLED’s built-in panel and external HDR displays use this same capability-driven path in Gaming Mode. There is no external-connector, dock, charger, device-model, or minimum-resolution requirement. Gamescope owns the panel calibration and final display output; MAKO forwards the application’s encoding and metadata through the existing bridge. Steam Deck LCD’s built-in panel remains SDR, while either Deck model can use an HDR-capable external display. An HDR-capable output with HDR allowed in Gamescope is required; the panel need not already be displaying HDR content when the game starts.

The shared `GamescopeScalingSurface` owner validates the X11 window, Gamescope identity and Wayland peer before reading `GAMESCOPE_HDR_OUTPUT_FEEDBACK` once during surface creation. Gamescope can create a game Xwayland server after publishing output capability, leaving that new server's property missing until an output change. For a missing property on a nonzero server, the bridge reuses `GamescopeHdrFeedbackReader`'s server-zero resolver for one on-demand output-capability query. The reader validates the same nonzero Gamescope PID and server-zero identity before and after the read. An explicit local SDR or invalid Boolean value, a malformed or failed property read, an unresolved or foreign root, and disabled HDR never gain HDR from this fallback. No new polling or presentation-path query is added. Only a confirmed HDR output and the user opt-in expose HDR10/PQ packed 10-bit pairs or linear scRGB/RGBA16F, and only when the lower Wayland driver supports their storage format. This is capability, not application HDR intent. A game must select an explicit HDR colour space. The bridge sends that original pair in per-swapchain feedback and normalizes only the driver-facing colour-space field to nonlinear sRGB; it never relabels the pixel encoding.

`VK_EXT_swapchain_colorspace` is declared by the layer manifest. The connected HDR bridge adds `VK_EXT_hdr_metadata` to device extension enumeration, consumes it when the lower driver does not implement it, and forwards metadata to the owning Gamescope swapchain using CTA-861 units. Identical quantized metadata is suppressed before protocol submission and flushing, including insignificant floating-point jitter. Metadata for SDR or retired protocol objects is ignored, and replacements inherit no metadata. Capability changes require a new surface/game restart; no HDR polling or round trip is added to presentation.

With **Disable HDR** on, the isolated bridge rejects HDR swapchain creation. On other surface paths, MAKO retains the existing disabled-HDR policy: no generation for an explicit HDR swapchain, with real-frame passthrough.

Native Steam Remote Play additionally enforces SDL3 sRGB renderer creation through its process-scoped packaged helper. `MAKO_DISABLE_HDR_EXPOSURE` is MAKO policy and does not itself override an application's SDL colourspace selection. The shared [Remote Play owner](../../plugin/docs/REMOTE-PLAY.md#sdr-output) supplies that startup policy for both Decky and Qt; it does not relabel HDR pixels or change the Renderer colour classifier. HDR stream input and SDL tone mapping still require independent hardware validation.

## Gamescope application-HDR evidence

The isolated bridge classifies the original application colour space and never reclassifies it from compositor-wide feedback. Its shared background reader continues to monitor presentation extent, refresh, VRR, and focus, but performs no periodic HDR atom lookup or property read when WSI is isolated. The surface-creation fallback above reads only output capability on demand, without changing the cached application-HDR sample. Disabled HDR performs neither periodic nor on-demand HDR access. The legacy full-WSI HDR path uses `GamescopeHdrFeedbackReader` to sample compositor colour properties outside the presentation path. A nested game server may publish the relevant properties on server zero, so the reader accepts a root display only when it belongs to the same Gamescope process.

Evidence has this precedence:

1. Process-start policy: disabled exposure is confirmed SDR.
2. `GAMESCOPE_COLOR_APP_WANTS_HDR_FEEDBACK`: primary application intent.
3. Application HDR metadata when the Boolean property is unavailable.
4. `GAMESCOPE_HDR_OUTPUT_FEEDBACK`: diagnostic display capability only.
5. `DXVK_HDR`: exposure information, not sufficient application intent. `DXVK_HDR=0` is SDR; `DXVK_HDR=1` alone does not activate HDR.

Feedback can initially describe a previous Gamescope commit. `StableBooleanFeedback` requires 750 ms of uninterrupted evidence before changing the confirmed state; unknown samples cancel the pending change. Sampling runs every 250 ms under Gamescope and every second elsewhere. `vkQueuePresentKHR` reads the latest result and never performs an X11 query.

## Swapchain classification

`classifySwapchainColor()` evaluates the complete Vulkan format and colour-space pair. Bit depth alone never establishes HDR semantics.

| Vulkan input | Required evidence | Encoding | Exchange format |
| --- | --- | --- | --- |
| 8-bit RGBA/BGRA with nonlinear sRGB | None | `Sdr8` | `R8G8B8A8_UNORM` |
| Packed 10-bit or RGBA16F with nonlinear sRGB | No confirmed HDR | `SdrHighPrecision` | `R16G16B16A16_SFLOAT` |
| Packed 10-bit with HDR10/ST2084 | Explicit colour space | `Hdr10Pq` | `R16G16B16A16_SFLOAT`, or validated packed transport |
| RGBA16F with extended linear sRGB | Explicit colour space | `ScRgbLinear` | `R16G16B16A16_SFLOAT` |
| Packed 10-bit or RGBA16F normalized to nonlinear sRGB by Gamescope WSI | Confirmed application HDR | Recovered HDR10/PQ or scRGB | Format-dependent |
| HLG, Dolby Vision, invalid format/colour-space pairs, or unvalidated wide colour | N/A | Unsupported | Real-frame passthrough |

Gamescope WSI can consume the application's original HDR colour space before a lower layer sees the create structure. MAKO recovers that meaning only when Gamescope reports application-owned HDR intent and the normalized format is one of the validated HDR formats. It never promotes an ordinary 8-bit swapchain to HDR.

## Colour flow

The application-facing and private backend devices may differ, so exchange images cross an external-memory boundary with an explicit `FrameEncoding`:

```text
game swapchain image
    -> application-device exchange image
    -> PQ BT.2020 to linear scRGB BT.709 when input is HDR10
    -> private RGBA16F model images
    -> linear scRGB BT.709 to PQ BT.2020 when output is HDR10
    -> application-device generated image
    -> game swapchain
```

HDR10 uses ST 2084 normalized to 10,000 nits, scRGB uses 80 nits per unit, and conversion includes BT.2020/BT.709 primaries. Linear scRGB already matches the model working representation. High-precision SDR can use RGBA16F transport without gaining HDR transfer semantics. Non-`Sdr8` encodings select the high-precision generation shader; only HDR10 and scRGB use HDR model constants.

### Packed HDR10 transport

When scaling is inactive, MAKO may exchange HDR10 through `A2B10G10R10_UNORM_PACK32` when both Vulkan devices prove the required external-image and format features, extended storage-image formats are available, and the backend exposes its packed-output shader. This reduces only the exchange boundary from eight to four bytes per pixel; the default model and intermediate images remain RGBA16F. Unsupported hardware falls back to RGBA16F. The explicit reduced-precision experiment below additionally changes HDR10 interpolation.

## Presentation transport and transitions

`selectPresentationTransport()` makes one create-time choice:

- `OrderedSdr` owns FIFO ordering and filters Gamescope's dynamic MAILBOX override. This is the supported release transport.
- `GamescopeHdr` preserves the experimental WSI bridge and admits generated images nonblockingly so an unavailable synthetic image cannot hold the real frame.

This legacy transport requires full Gamescope WSI. The isolated HDR bridge instead retains `OrderedSdr` (the established enum name for private ordered delivery), including its existing Fixed/Adaptive scheduling and bounded output timeline.

Stable SDR/HDR feedback may rebuild private exchange images, backend resources, and colour conversions. It cannot recreate the game swapchain or change its transport. The replacement waits for MAKO-owned completion evidence, retains real-frame passthrough on failure, and retries on the bounded private-resource schedule described in [Runtime configuration transitions](RUNTIME-TRANSITIONS.md).

## Scaling and shaders

Full-precision HDR scaler boundaries use RGBA16F rather than an 8-bit SDR image. Native Resolution and MAKO Scaler retain the source encoding. LS1 uses a separately cached high-precision reconstruction shader and output; its feature images retain their model-defined formats. Linear-scRGB LS1 inputs are converted to bounded PQ BT.2020 before the network and restored afterwards through the existing colour-conversion shaders. When pre-generation direct output is available, that final conversion writes into the exported Frame Generation input without an additional full-resolution copy. Private output remains available for FG-off and replacement transitions. Only this linear-scRGB LS1 path requests the extra storage/sampling usages for its conversion boundaries. Scaling keeps RGBA16F Frame Generation transport so live scaler changes retain compatible storage bindings; the packed HDR optimization remains FG-only.

### Experimental reduced precision

`hdr_reduced_precision` defaults to false and is a per-profile native setting, including Handheld/Docked sets. Both UIs show **Reduced HDR Precision** only while launch HDR is allowed. It never enables HDR, changes a swapchain's colour space, or changes the bridge/presentation contract. SDR ignores it. The environment-only equivalent is `MAKO_HDR_REDUCED_PRECISION`.

For HDR10 scaling, supported packed RGB10A2 inputs replace RGBA16F source images. MAKO Scaler, Native Resolution, LS1 Quality and LS1 Performance keep their existing output format, direct FG bindings and arithmetic. No new full-resolution copy, conversion or shader variant is introduced. Linear scRGB scaler inputs remain RGBA16F. FG-only HDR10 exchange was already packed where supported.

With the toggle on, HDR10 Frame Generation takes an additional, explicitly approximate path: the model samples the exchanged PQ BT.2020 values directly with its bounded-colour constants and writes PQ output directly in the existing exchange format. Packed exchange uses a context-local RGB10A2 output shader derived through the canonical storage-format patcher; float exchange used by Scaling keeps its RGBA16F output binding. Neither path creates full-resolution linear working images or dispatches PQ↔scRGB conversions. Motion-model tensors, Flow Scale, resolution, output count and the independent FP16/FP32 setting are unchanged. This avoids conversion and linear-HDR model work, but interpolation in PQ is not equivalent to linear-light interpolation: moving highlights and high-contrast edges can change, and packed output clips overshoot. It never tone-maps the result to SDR or changes the application/compositor colour contract. scRGB and SDR ignore this FG approximation. Off restores the existing full linear path.

The optional backend argument is selected per private context, so ordinary contexts build no packed approximation pipeline and presentation adds no precision branch, allocation or format query per generated frame. Live changes use the existing prepare/drain/commit FG coordinator, independently of the scaler’s coordinator. FG records the precision of its committed resource set so a faster scaler commit or unrelated profile edit cannot cancel its pending replacement; reverting discards a superseded candidate. A failed preparation retains the old resources and retries on the existing bounded schedule.

The shader fork receives the saved setting through the optional private `makoSetSwapchainHdrPrecisionV1(VkDevice, VkSwapchainKHR, VkBool32) -> VkBool32` device-dispatch hook at creation and only when the setting changes. The hook validates ownership and Boolean values and queues a request; the existing live graph transaction applies it. Explicit HDR shader graphs may use RGB10A2 PQ intermediates and ReShade backbuffers after format-feature validation. scRGB retains the existing boundary conversions. Application images, output encoding, custom explicitly declared textures and shader arithmetic stay unchanged. An absent hook or unsupported packed format retains full precision. Compact UNORM clips intermediate overshoot and reduces alpha to two bits, so custom blending and strong multi-effect chains can change as well as showing banding.

When that validated graph format exactly matches the HDR10 output, the final effect writes directly to the output, removing its redundant copy pass and one intermediate image per swapchain slot. This includes ReShade endings such as Levels Plus. Format fallback and alternate packed channel order retain the copy, and linear scRGB retains its colour conversion. The optimization preserves the selected working precision; Frame Generation’s separate approximation avoids its linear buffers rather than changing this shader graph.

Precision changes prepare a complete replacement graph with its own images, drain the existing queue, and retire the previous graph only on success. Failure preserves its images and descriptors; an uncertain final drain retains the candidate until safe retirement. Identical requests add no per-frame format query, allocation or queue wait. HDR10 scaler changes use the existing private spatial transition and keep old inputs until its drain completes. Concurrent cap/mode edits remain independent, reverting cancels a pending request, and enabling precision in SDR remains dormant. Runtime records retain requested/applied native settings; HDR diagnostics report scaler input/output formats and shader-handoff acceptance, while the fork logs the constructed graph format. Qualify live on/off/recreation, compact-format fallback, failure rollback, pixels and GPU time independently; byte counts alone are not performance evidence.

### Performance boundaries

Allowing HDR can provision the isolated Wayland surface bridge even when the game selects SDR. Switching HDR off inside the game restores the SDR colour pipeline but does not replace that process's surface transport. When Frame Generation is provisioned, the bridge also applies the process-wide [presentation-wait compatibility policy](WSI-ISOLATION.md#private-bridge-presentation-wait-compatibility), including for SDR content and HDR-only profiles with Scaling disabled. Therefore compare HDR exposure at launch separately from the game's selected encoding; an in-game HDR toggle alone does not measure the cost of enabling the bridge.

Storage precision and shader arithmetic are independent. SDR transport remains RGBA8 (four bytes per pixel); full-precision HDR scaling uses RGBA16F (eight bytes per pixel), and supported HDR10 FG-only transport can use packed 10-bit (four bytes per pixel). MAKO Scaler and LSFG retain the selected FP16/FP32 arithmetic, while LS1 remains FP32. HDR does not promote the pipeline to RGBA32F.

Native Resolution and MAKO Scaler add no transfer-function pass for HDR. LS1 adds two conversion dispatches only for linear scRGB. Full-precision HDR10 Frame Generation uses the existing PQ-to-linear input and linear-to-PQ output conversion owners; the opt-in approximation omits both; scRGB is already in the model's linear space. By default, active HDR shader chains keep RGBA16F intermediates. Linear scRGB needs two graph-boundary conversion passes. HDR10 skips the input copy for a chain starting with CAS, DLS, or a ReShade effect and the output copy for a chain ending with either sharpener; either sharpener alone needs neither copy nor an intermediate image. Direct HDR10 sampling removes one full-resolution image per swapchain slot from ReShade-first chains while retaining RGBA16F outputs and private multi-pass images. Avoiding intermediate rounding can slightly change pixels without lowering effect precision. Shader cost applies to every real and generated output. Empty shader chains add no HDR conversion. Equal frame rate cannot be promised from unchanged arithmetic precision: HDR storage bandwidth, conversion passes, output resolution, shader selection, and available GPU headroom still matter. Qualify GPU time and delivered cadence on the intended hardware.

The colour-aware vkBasalt fork receives the original swapchain colour space through the private startup-only `makoSetSwapchainColorSpaceV1` device-dispatch hook before it exposes images. The hook’s native signature is `VkBool32(VkDevice, VkSwapchainKHR, VkColorSpaceKHR)`. HDR effect graphs default to RGBA16F PQ BT.2020 intermediates and convert linear scRGB at the chain boundaries. The reduced-precision experiment can use packed 10-bit PQ for those intermediates. Every generated and real frame follows that same lower shader chain. An empty chain copies original pixels without colour conversion. An older shader binary without the handoff cannot safely process isolated HDR and rejects that combination. Custom shaders can still intentionally remap or clip colour or declare low-precision intermediates; their individual visual behavior needs testing.

CAS and DLS apply an HDR-only transfer correction within their existing shader pass. Their contrast, denoise, and highlight limits operate on an sRGB-like signal referenced to 203 nits, retaining BT.2020 primaries. A shared local range preserves brighter HDR detail and is reversed before PQ output; the image is not tone-mapped into SDR. The fork owns the generated transfer approximation, analytic float32 accuracy gate, and pipeline-specialization tests. CAS reuses monotonic neighbourhood extrema to reduce conversion work, and DLS shares the direct packed-HDR10 boundary optimization. These changes add arithmetic without adding full-resolution conversion passes or widening storage. SDR uses the existing effect math. Physical appearance and game FPS still require comparison at the same settings.

## Hardware qualification

Qualify the isolated bridge on the Steam Deck OLED built-in panel and external HDR displays, with explicit HDR10 and scRGB, the HDR toggle on/off, an SDR game on an HDR desktop, HDR-only, Scaling-only, FG-only, Shaders-only, and every enabled combination. Confirm display HDR mode, highlight and black levels, source/presentation extents, real/generated delivery, metadata updates, recreation, and shutdown. Missing output capability must leave HDR unadvertised rather than inventing a display mode. Also check the Steam Deck LCD built-in panel and external SDR displays. Docking or undocking across HDR/SDR outputs requires a new surface or game restart to refresh capability; live display switching is not qualified by startup tests.

Required real-hardware evidence includes native Vulkan, DXVK, and VKD3D-Proton; explicit HDR10 and scRGB; Gamescope-normalized colour recovery and metadata; packed and RGBA16F transport; FP32 and FP16; Fixed and Adaptive scheduling; focus, overlay, resolution, recreation, metadata transition, hitch, and shutdown paths; and each claimed Steam Runtime, Flatpak, architecture, GPU, and driver boundary. Unit tests and loader discovery do not establish colour correctness or pacing.

Release blockers include washed-out or crushed output, coloured motion artifacts, SDR interpreted as PQ, stale application intent, repeated rebuild loops, blocked real presents, unexplained cadence loss, or unexpected layers.

Keep these invariants:

- Output HDR capability is not application HDR intent.
- A 10-bit format is not PQ without an HDR colour space or confirmed application feedback.
- An isolated process never loads full Gamescope WSI or changes its presentation transport to enable HDR.
- Discovery is not proof of `MAKO render -> Gamescope WSI -> optional MAKO spatial` call order.
- Presentation transport never changes for a live swapchain.
- Generated images never take priority over the real frame on the HDR bridge.
- Unsupported encodings and failed private transitions remain on real-frame passthrough.
- Packed transport and model precision are independent; validate FP32 and FP16 separately.

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Process-start HDR/WSI policy and transport | `mako-render/src/presentation_policy.hpp` |
| Isolated HDR surface, format exposure, metadata and lifetime | `mako-render/src/gamescope_scaling_surface.cpp` |
| Gamescope discovery, feedback, and diagnostics | `mako-render/src/gamescope_hdr_feedback.cpp` |
| Feedback stabilization | `mako-render/src/runtime_transition.hpp` |
| Format and colour-space classification | `mako-render/src/color_pipeline.cpp` |
| Swapchain resources and private transitions | `mako-render/src/swapchain/resources.cpp` |
| Native-first experimental presentation | `mako-render/src/swapchain/present.cpp`, `mako-render/src/swapchain/present/real_frames.cpp` |
| Backend encodings and conversion shaders | `mako-backend/src/mako.cpp`, `mako-backend/src/shaders/` |
| Embedded shader generation and freshness | `scripts/generate-color-conversion-spirv.py`, portable CTest |
| Deterministic policy coverage | `mako-render/tests/color_pipeline_tests.cpp`, `mako-render/tests/presentation_policy_tests.cpp`, related Renderer tests |

Use the `hdr`, `layers`, and `recovery` presets from [Collect diagnostics](COLLECT_DIAGNOSTICS.md) for a focused report.
