# HDR pipeline architecture

This guide defines MAKO Renderer's colour handling and its Gamescope HDR boundary. [WSI isolation](WSI-ISOLATION.md) owns Vulkan-layer discovery and presentation ownership.

<!-- prettier-ignore -->
> [!IMPORTANT]
> HDR Frame Generation is not a supported release path. MAKO Decky and standalone `mako-launch` deliberately select validated SDR presentation. The Renderer contains HDR10/PQ, linear-scRGB, feedback, conversion, and fallback groundwork for continued validation.

## Separate colour and presentation decisions

Two independent decisions are involved:

1. The game swapchain encoding, cross-device exchange format, and model working space define the colour pipeline. Stable compositor feedback may replace these private resources while the process runs.
2. Ordered SDR and the experimental Gamescope HDR bridge use different Vulkan layer and presentation contracts. This transport is selected before the swapchain is created and cannot change live.

Do not treat a private colour-resource transition as permission to add Gamescope WSI or reinterpret an existing swapchain's transport.

## Supported release contract

The normal managed launch establishes this policy before Vulkan starts:

```text
DISABLE_GAMESCOPE_WSI=1
MAKO_DISABLE_HDR_EXPOSURE=1
DXVK_HDR unset
```

MAKO Decky's scaling and explicit Gamescope WSI compatibility paths admit a guarded WSI chain but keep HDR exposure disabled and remove `DXVK_HDR`. `resolvePresentationEnvironmentPolicy()` treats either WSI isolation or disabled exposure as closing the experimental HDR bridge.

If an explicit HDR swapchain reaches this SDR-only policy, MAKO disables generation for that swapchain and passes real frames through.

## Gamescope application-HDR evidence

`GamescopeHdrFeedbackReader` samples compositor properties outside the presentation path. A nested game server may publish the relevant properties on server zero, so the reader accepts a root display only when it belongs to the same Gamescope process.

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

MAKO may exchange HDR10 through `A2B10G10R10_UNORM_PACK32` when both Vulkan devices prove the required external-image and format features, extended storage-image formats are available, and the backend exposes its packed-output shader. This reduces only the exchange boundary from eight to four bytes per pixel; model and intermediate images remain RGBA16F. Unsupported hardware falls back to RGBA16F rather than lowering model precision.

## Presentation transport and transitions

`selectPresentationTransport()` makes one create-time choice:

- `OrderedSdr` owns FIFO ordering and filters Gamescope's dynamic MAILBOX override. This is the supported release transport.
- `GamescopeHdr` preserves the experimental WSI bridge and admits generated images nonblockingly so an unavailable synthetic image cannot hold the real frame.

The HDR transport is possible only when Gamescope is detected, WSI is present, HDR exposure is allowed, and the swapchain is HDR-capable. Current managed launchers intentionally prevent that combination.

Stable SDR/HDR feedback may rebuild private exchange images, backend resources, and colour conversions. It cannot recreate the game swapchain or change its transport. The replacement waits for MAKO-owned completion evidence, retains real-frame passthrough on failure, and retries on the bounded private-resource schedule described in [Runtime configuration transitions](RUNTIME-TRANSITIONS.md).

## Requirements before exposing HDR

A supported HDR launch must be a restart-only per-game choice with a deliberately constructed and verified MAKO/Gamescope WSI chain. It must fail to native presentation when the chain, application-HDR evidence, format, or transport cannot be proven.

Required real-hardware evidence includes native Vulkan, DXVK, and VKD3D-Proton; explicit HDR10 and scRGB; Gamescope-normalized colour recovery and metadata; packed and RGBA16F transport; FP32 and FP16; Fixed and Adaptive scheduling; focus, overlay, resolution, recreation, metadata transition, hitch, and shutdown paths; and each claimed Steam Runtime, Flatpak, architecture, GPU, and driver boundary. Unit tests and loader discovery do not establish colour correctness or pacing.

Release blockers include washed-out or crushed output, coloured motion artifacts, SDR interpreted as PQ, stale application intent, repeated rebuild loops, blocked real presents, unexplained cadence loss, or unexpected layers.

Keep these invariants:

- Output HDR capability is not application HDR intent.
- A 10-bit format is not PQ without an HDR colour space or confirmed application feedback.
- An isolated process cannot enter the Gamescope HDR transport later.
- Discovery is not proof of `MAKO render -> Gamescope WSI -> optional MAKO spatial` call order.
- Presentation transport never changes for a live swapchain.
- Generated images never take priority over the real frame on the HDR bridge.
- Unsupported encodings and failed private transitions remain on real-frame passthrough.
- Packed transport and model precision are independent; validate FP32 and FP16 separately.

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Process-start HDR/WSI policy and transport | `mako-render/src/presentation_policy.hpp` |
| Gamescope discovery, feedback, and diagnostics | `mako-render/src/gamescope_hdr_feedback.cpp` |
| Feedback stabilization | `mako-render/src/runtime_transition.hpp` |
| Format and colour-space classification | `mako-render/src/color_pipeline.cpp` |
| Swapchain resources and private transitions | `mako-render/src/swapchain.cpp` |
| Native-first experimental presentation | `mako-render/src/swapchain_present.cpp` |
| Backend encodings and conversion shaders | `mako-backend/src/mako.cpp`, `mako-backend/src/shaders/` |
| Embedded shader generation and freshness | `scripts/generate-color-conversion-spirv.py`, portable CTest |
| Deterministic policy coverage | `mako-render/tests/color_pipeline_tests.cpp`, `mako-render/tests/presentation_policy_tests.cpp`, related Renderer tests |

Use the `hdr`, `layers`, and `recovery` presets from [Collect diagnostics](COLLECT_DIAGNOSTICS.md) for a focused report.
