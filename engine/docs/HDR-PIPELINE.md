# HDR pipeline architecture

This guide defines MAKO Renderer's colour handling and its Gamescope HDR boundary. [WSI isolation](WSI-ISOLATION.md) owns Vulkan-layer discovery and presentation ownership.

<!-- prettier-ignore -->
> [!IMPORTANT]
> HDR is experimental and disabled by default. Enable it through MAKO’s isolated Gamescope bridge with **Gamescope WSI** off. HDR10/PQ and linear scRGB support Scaling, Frame Generation and Shaders; display accuracy, game compatibility and performance require [hardware qualification](#hardware-qualification).

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

Turning **Disable HDR (Restart)** off exports `MAKO_DISABLE_HDR_EXPOSURE=0` and `DXVK_HDR=1`, retaining `DISABLE_GAMESCOPE_WSI=1`. Decky stores the choice per profile; Qt stores it globally in `launcher.conf`. A game restart applies it. HDR alone can provision the bridge without allocating Scaling or Frame Generation resources.

With HDR disabled, the bridge performs no HDR capability query, format exposure, metadata emulation or protocol request. Scaling can still use its surface association; with both Scaling and HDR disabled, no isolated bridge connection is provisioned.

Steam Deck OLED and external HDR displays use the same output-capability check in Gaming Mode. Gamescope owns panel calibration and display output; MAKO forwards the application’s encoding and metadata. HDR must be allowed in Gamescope, but HDR content need not already be active. Steam Deck LCD’s built-in panel remains SDR.

At surface creation, `GamescopeScalingSurface` validates the X11 window, Gamescope identity and Wayland peer, then reads `GAMESCOPE_HDR_OUTPUT_FEEDBACK`. A newly created game Xwayland server may lack that property. Only a missing property on a nonzero server permits one server-zero query through `GamescopeHdrFeedbackReader`, with the same nonzero Gamescope PID and server-zero identity verified before and after. Explicit SDR, invalid values, failed reads or identity checks, and disabled HDR never permit fallback.

User opt-in, confirmed output capability and lower-driver format support are all required to expose HDR10/PQ packed 10-bit or linear scRGB/RGBA16F pairs. The game must select an explicit HDR colour space. Per-swapchain feedback retains that original pair; only the driver-facing colour-space field becomes nonlinear sRGB. Pixels are never reinterpreted. Capability changes require a new surface or game restart; presentation adds no HDR polling or round trips.

The layer manifest declares `VK_EXT_swapchain_colorspace`. The connected HDR bridge advertises `VK_EXT_hdr_metadata`, consumes it if the driver lacks it, and forwards metadata to the owning Gamescope swapchain in CTA-861 units. It suppresses wire-identical quantized metadata before submission and flushing, ignores SDR/retired objects, and gives replacements fresh metadata ownership.

With **Disable HDR** on, the isolated bridge rejects HDR swapchain creation. On other surface paths, MAKO retains the existing disabled-HDR policy: no generation for an explicit HDR swapchain, with real-frame passthrough.

Native Steam Remote Play additionally enforces SDL3 sRGB renderer creation through its process-scoped packaged helper. `MAKO_DISABLE_HDR_EXPOSURE` is MAKO policy and does not itself override an application's SDL colourspace selection. The shared [Remote Play owner](../../plugin/docs/REMOTE-PLAY.md#sdr-output) supplies that startup policy for both Decky and Qt; it does not relabel HDR pixels or change the Renderer colour classifier. HDR stream input and SDL tone mapping still require independent hardware validation.

## Gamescope application-HDR evidence

The isolated bridge uses the original application colour space, without compositor-wide reclassification or periodic HDR reads. Its background reader still monitors extent, refresh, VRR and focus; the on-demand capability fallback leaves cached application-HDR state unchanged. The legacy full-WSI path uses `GamescopeHdrFeedbackReader` for compositor colour properties and accepts server-zero fallback only within the same Gamescope process. The following evidence order and stabilization rules apply to that legacy path.

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

With Scaling off, HDR10 can use `A2B10G10R10_UNORM_PACK32` when both devices support the external-image and format features, extended storage-image formats are available, and the backend supplies its packed-output shader. This halves exchange storage from eight to four bytes per pixel, with RGBA16F fallback. Full-precision model intermediates remain RGBA16F; [reduced precision](#reduced-precision) also changes interpolation.

## Presentation transport and transitions

`selectPresentationTransport()` makes one create-time choice:

- `OrderedSdr` owns FIFO ordering and filters Gamescope's dynamic MAILBOX override. This is the supported release transport.
- `GamescopeHdr` preserves the experimental WSI bridge and admits generated images nonblockingly so an unavailable synthetic image cannot hold the real frame.

The isolated HDR bridge retains `OrderedSdr` and its Fixed/Adaptive scheduling; only `GamescopeHdr` requires full Gamescope WSI.

Stable SDR/HDR feedback may rebuild private exchange images, backend resources, and colour conversions. It cannot recreate the game swapchain or change its transport. The replacement waits for MAKO-owned completion evidence, retains real-frame passthrough on failure, and retries on the bounded private-resource schedule described in [Runtime configuration transitions](RUNTIME-TRANSITIONS.md).

## Scaling and shaders

Full-precision HDR scaling uses RGBA16F boundaries. Native Resolution and MAKO Scaler preserve source encoding. LS1 caches a high-precision reconstruction shader while retaining model-defined feature formats. For linear scRGB, it converts to bounded PQ BT.2020 before reconstruction and back afterwards; only this path needs extra storage/sampling usages. Where available, the final conversion writes directly into FG input, avoiding a full-resolution copy. Private output remains available for FG-off and transitions. Scaling retains RGBA16F FG transport so live scaler changes preserve compatible bindings.

### Reduced precision

`hdr_reduced_precision` defaults to true per profile, including Handheld/Docked sets, and preserves saved choices. Both UIs show it while launch HDR is allowed; it never enables HDR or changes swapchain encoding or presentation. SDR ignores it. The environment equivalent is `MAKO_HDR_REDUCED_PRECISION`. Turning it off restores the full-precision colour flow shown above.

For HDR10 scaling, supported packed RGB10A2 inputs replace RGBA16F source images without changing output formats, direct FG bindings or arithmetic, or adding copies or conversions. Linear-scRGB scaler inputs remain RGBA16F.

HDR10 Frame Generation interpolates PQ BT.2020 values directly, omitting linear working images and PQ↔scRGB conversion passes. Packed exchange uses a context-local RGB10A2 output shader from the storage-format patcher; Scaling retains RGBA16F output bindings. Model tensors, Flow Scale, resolution, output count and FP16/FP32 arithmetic remain unchanged. Interpolating in PQ can alter bright edges compared with linear light; packed output also clips overshoot. scRGB and SDR generation are unchanged.

Precision is selected per private context, adding no per-frame branch, allocation or format query. Live FG changes use prepare/drain/commit independently of Scaling. Each owner tracks its committed precision so another owner’s commit or unrelated edit cannot cancel pending work. Reverting discards the candidate; preparation failure retains old resources and uses bounded retries.

The optional `makoSetSwapchainHdrPrecisionV1(VkDevice, VkSwapchainKHR, VkBool32) -> VkBool32` hook receives the setting at creation and on changes. It validates ownership and Boolean values, then queues a live graph transaction. Supported HDR graphs use RGB10A2 PQ intermediates and ReShade backbuffers after format validation; a missing hook or unsupported format retains full precision. scRGB keeps its boundary conversions. Application images, output encoding, explicitly declared custom textures and shader arithmetic retain their formats and behavior. Compact storage clips overshoot and has two-bit alpha, which can change blending and multi-effect chains or add banding.

When the graph format matches HDR10 output exactly, the final effect writes directly to output, removing a copy and one image per swapchain slot. Format fallback and alternate packed channel order retain the copy; scRGB retains its conversion.

Shader precision changes prepare a complete graph, drain the queue and retire the old graph only on success. Failure preserves old resources; an uncertain final drain retains the candidate until safe retirement. Unchanged requests add no per-frame work. Scaling follows its own private-resource transition. Runtime records distinguish requested/applied precision; HDR diagnostics retain scaler formats and shader-handoff acceptance, and vkBasalt logs the graph’s `working_format`. `HDR generation precision` reports `pq-code-values` and `pq_conversion=0` for approximate interpolation. Validate transitions, fallback, rollback, pixels and GPU time separately.

### Performance boundaries

Allowing HDR can provision the bridge even if the game selects SDR. In-game HDR changes do not replace that transport. With FG provisioned, [presentation-wait compatibility](WSI-ISOLATION.md#private-bridge-presentation-wait-compatibility) also applies. Compare launch HDR exposure separately from in-game encoding when measuring overhead.

Storage precision is separate from arithmetic: SDR exchange uses RGBA8 (four bytes per pixel), full-precision HDR scaling uses RGBA16F (eight), and supported FG-only HDR10 exchange uses packed 10-bit (four). MAKO Scaler and LSFG retain selected FP16/FP32 arithmetic; LS1 remains FP32. HDR does not require RGBA32F.

Native Resolution and MAKO Scaler add no transfer-function pass; linear-scRGB LS1 adds two. Full-precision HDR10 FG converts PQ↔linear, while reduced precision omits those passes. Shader cost applies to every real and generated output; empty chains add no conversion. HDR10 chains starting with CAS, DLS or ReShade skip the input copy; chains ending with CAS or DLS skip the output copy. Either sharpener alone needs neither an intermediate image nor a copy. Linear-scRGB shader chains require two boundary conversions. Bandwidth, conversions, resolution and effect selection can still lower FPS; measure GPU cost and delivered cadence on the intended hardware.

Before exposing images, the colour-aware vkBasalt fork receives the original colour space through `makoSetSwapchainColorSpaceV1(VkDevice, VkSwapchainKHR, VkColorSpaceKHR) -> VkBool32`. Full-precision graphs use RGBA16F PQ BT.2020 intermediates; reduced precision can use packed 10-bit. Every real/generated frame uses the same chain. Older binaries without the colour-space handoff reject isolated HDR. Custom shaders may remap or clip colours or declare low-precision textures, so their appearance needs separate testing.

CAS and DLS apply HDR contrast, denoise and highlight limits in an sRGB-like signal referenced to 203 nits, retaining BT.2020 primaries. A reversible local range preserves brighter detail before PQ output. This correction stays within the existing pass; CAS reuses neighbourhood extrema to reduce work. SDR math is unchanged. The fork owns transfer-approximation accuracy and specialization tests; compare visual sharpening at matched game settings.

## Hardware qualification

Test Steam Deck OLED and external HDR displays with HDR10/scRGB, HDR on/off, SDR games, HDR-only, and Scaling/FG/Shaders individually and combined. Verify display mode, highlight/black levels, extents, real/generated delivery, metadata, recreation and shutdown. Include LCD/SDR displays and missing-capability cases. Switching HDR/SDR outputs requires a new surface or restart; startup tests do not qualify live docking.

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
