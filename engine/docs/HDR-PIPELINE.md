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

Turn **Disable HDR (Restart)** off in the MAKO Decky game profile or the Qt UI’s standalone launch settings, then restart the game and enable its HDR option. The launch exports `MAKO_DISABLE_HDR_EXPOSURE=0` and `DXVK_HDR=1` while retaining `DISABLE_GAMESCOPE_WSI=1`. Decky stores this per profile; Qt stores it globally in `launcher.conf`. Turning the toggle back on restores the default SDR launch. Scaling and Frame Generation switches remain independent; HDR alone can provision the surface bridge without allocating either processing engine.

With HDR disabled, the bridge does not query HDR output capability, append HDR formats, emulate HDR metadata, or send HDR protocol requests. If Scaling is enabled, its existing surface association still operates. With both Scaling and HDR disabled, no isolated bridge connection is provisioned.

Steam Deck OLED’s built-in panel and external HDR displays use this same capability-driven path in Gaming Mode. There is no external-connector, dock, charger, device-model, or minimum-resolution requirement. Gamescope owns the panel calibration and final display output; MAKO forwards the application’s encoding and metadata through the existing bridge. Steam Deck LCD’s built-in panel remains SDR, while either Deck model can use an HDR-capable external display. An HDR-capable output with HDR allowed in Gamescope is required; the panel need not already be displaying HDR content when the game starts.

The shared `GamescopeScalingSurface` owner validates the X11 window, Gamescope identity and Wayland peer before reading `GAMESCOPE_HDR_OUTPUT_FEEDBACK` once during surface creation. Only a confirmed HDR output and the user opt-in expose HDR10/PQ packed 10-bit pairs or linear scRGB/RGBA16F, and only when the lower Wayland driver supports their storage format. This is capability, not application HDR intent. A game must select an explicit HDR colour space. The bridge sends that original pair in per-swapchain feedback and normalizes only the driver-facing colour-space field to nonlinear sRGB; it never relabels the pixel encoding.

`VK_EXT_swapchain_colorspace` is declared by the layer manifest. The connected HDR bridge adds `VK_EXT_hdr_metadata` to device extension enumeration, consumes it when the lower driver does not implement it, and forwards metadata to the owning Gamescope swapchain using CTA-861 units. Identical quantized metadata is suppressed before protocol submission and flushing, including insignificant floating-point jitter. Metadata for SDR or retired protocol objects is ignored, and replacements inherit no metadata. Capability changes require a new surface/game restart; no HDR polling or round trip is added to presentation.

With **Disable HDR** on, the isolated bridge rejects HDR swapchain creation. On other surface paths, MAKO retains the existing disabled-HDR policy: no generation for an explicit HDR swapchain, with real-frame passthrough.

Native Steam Remote Play additionally enforces SDL3 sRGB renderer creation through its process-scoped packaged helper. `MAKO_DISABLE_HDR_EXPOSURE` is MAKO policy and does not itself override an application's SDL colourspace selection. The shared [Remote Play owner](../../plugin/docs/REMOTE-PLAY.md#sdr-output) supplies that startup policy for both Decky and Qt; it does not relabel HDR pixels or change the Renderer colour classifier. HDR stream input and SDL tone mapping still require independent hardware validation.

## Gamescope application-HDR evidence

The isolated bridge classifies the original application colour space and never reclassifies it from compositor-wide feedback. Its shared background reader continues to monitor presentation extent, refresh, VRR, and focus, but performs no HDR atom lookup or property read when WSI is isolated or HDR exposure is disabled. The legacy full-WSI HDR path uses `GamescopeHdrFeedbackReader` to sample compositor colour properties outside the presentation path. A nested game server may publish the relevant properties on server zero, so the reader accepts a root display only when it belongs to the same Gamescope process.

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

When scaling is inactive, MAKO may exchange HDR10 through `A2B10G10R10_UNORM_PACK32` when both Vulkan devices prove the required external-image and format features, extended storage-image formats are available, and the backend exposes its packed-output shader. This reduces only the exchange boundary from eight to four bytes per pixel; model and intermediate images remain RGBA16F. Unsupported hardware falls back to RGBA16F rather than lowering model precision.

## Presentation transport and transitions

`selectPresentationTransport()` makes one create-time choice:

- `OrderedSdr` owns FIFO ordering and filters Gamescope's dynamic MAILBOX override. This is the supported release transport.
- `GamescopeHdr` preserves the experimental WSI bridge and admits generated images nonblockingly so an unavailable synthetic image cannot hold the real frame.

This legacy transport requires full Gamescope WSI. The isolated HDR bridge instead retains `OrderedSdr` (the established enum name for private ordered delivery), including its existing Fixed/Adaptive scheduling and bounded output timeline.

Stable SDR/HDR feedback may rebuild private exchange images, backend resources, and colour conversions. It cannot recreate the game swapchain or change its transport. The replacement waits for MAKO-owned completion evidence, retains real-frame passthrough on failure, and retries on the bounded private-resource schedule described in [Runtime configuration transitions](RUNTIME-TRANSITIONS.md).

## Scaling and shaders

All classified HDR scaler boundaries use RGBA16F rather than an 8-bit SDR image. Native Resolution and MAKO Scaler retain the source encoding. LS1 uses a separately cached high-precision reconstruction shader and output; its feature images retain their model-defined formats. Linear-scRGB LS1 inputs are converted to bounded PQ BT.2020 before the network and restored afterwards through the existing colour-conversion shaders. When pre-generation direct output is available, that final conversion writes into the exported Frame Generation input without an additional full-resolution copy. Private output remains available for FG-off and replacement transitions. Only this linear-scRGB LS1 path requests the extra storage/sampling usages for its conversion boundaries. Scaling keeps RGBA16F Frame Generation transport so live scaler changes retain compatible storage bindings; the packed HDR optimization remains FG-only.

### Performance boundaries

Storage precision and shader arithmetic are independent. SDR transport remains RGBA8 (four bytes per pixel); HDR scaling uses RGBA16F (eight bytes per pixel), and supported HDR10 FG-only transport can use packed 10-bit (four bytes per pixel). MAKO Scaler and LSFG retain the selected FP16/FP32 arithmetic, while LS1 remains FP32. HDR does not promote the pipeline to RGBA32F.

Native Resolution and MAKO Scaler add no transfer-function pass for HDR. LS1 adds two conversion dispatches only for linear scRGB. HDR10 Frame Generation uses the existing PQ-to-linear input and linear-to-PQ output conversion owners; scRGB is already in the model's linear space. Active HDR shader chains keep RGBA16F intermediates. Linear scRGB needs two graph-boundary conversion passes. HDR10 skips the input copy for a chain starting with CAS and the output copy for a chain ending with CAS; CAS alone needs neither copy nor an intermediate image. Avoiding the intermediate rounding can slightly change CAS-only pixels without lowering effect precision. Shader cost applies to every real and generated output. Empty shader chains add no HDR conversion. Equal frame rate cannot be promised from unchanged arithmetic precision: HDR storage bandwidth, conversion passes, output resolution, shader selection, and available GPU headroom still matter. Qualify GPU time and delivered cadence on the intended hardware.

The colour-aware vkBasalt fork receives the original swapchain colour space through the private startup-only `makoSetSwapchainColorSpaceV1` device-dispatch hook before it exposes images. The hook’s native signature is `VkBool32(VkDevice, VkSwapchainKHR, VkColorSpaceKHR)`. HDR effect graphs use RGBA16F PQ BT.2020 intermediates and convert linear scRGB at the chain boundaries. Every generated and real frame follows that same lower shader chain. An empty chain copies original pixels without colour conversion. An older shader binary without the handoff cannot safely process isolated HDR and rejects that combination. Custom shaders can still intentionally remap or clip colour or declare low-precision intermediates; their individual visual behavior needs testing.

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
