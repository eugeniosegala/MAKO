# HDR pipeline architecture

This document is the architectural source of truth for HDR handling in MAKO Renderer. It describes the implemented colour pipeline, the supported release boundary, and the contract with Gamescope WSI. For loader and presentation isolation, read [WSI isolation](WSI-ISOLATION.md) alongside this document.

<!-- prettier-ignore -->
> [!IMPORTANT]
> HDR frame generation is not a supported release path yet. MAKO Decky and the standalone `mako-launch` helper deliberately select the validated SDR path. The Renderer contains HDR10/PQ, linear-scRGB, Gamescope feedback, and private resource-transition groundwork so HDR can be validated without redesigning the frame-generation backend later.

## The two independent splits

HDR development crosses two boundaries that must not be conflated:

1. **Colour/data split:** the game swapchain encoding, the image format shared between the application and private backend devices, and the model's linear working space are separate decisions.
2. **Presentation/WSI split:** ordered SDR presentation and the experimental Gamescope HDR bridge have different swapchain and pacing contracts.

The first split can transition private MAKO resources after stable compositor feedback. The second is selected before Vulkan creates the process's instance and swapchain. It cannot be switched live by the UI.

## Current release contract

The default isolated launch establishes the SDR boundary before Vulkan starts:

```text
DISABLE_GAMESCOPE_WSI=1
MAKO_DISABLE_HDR_EXPOSURE=1
DXVK_HDR unset
```

MAKO Decky uses this environment unless Scaling or explicit Gamescope WSI compatibility selects a guarded WSI chain. Those exceptions remain SDR-only because they still set `MAKO_DISABLE_HDR_EXPOSURE=1` and remove `DXVK_HDR`. Standalone `mako-launch` uses the isolated policy. `resolvePresentationEnvironmentPolicy()` in `mako-render/src/presentation_policy.hpp` is authoritative: either WSI isolation or disabled HDR exposure closes the Gamescope HDR bridge. The root resolves that immutable policy before starting feedback monitoring.

This defence matters because Gamescope WSI membership is process-wide. Once it has been excluded from Vulkan layer discovery, later X11 feedback cannot add it back to the dispatch chain.

## HDR evidence and activation

`GamescopeHdrFeedbackReader` observes compositor properties on a background thread. The game commonly runs on a nested Gamescope Xwayland server while the feedback properties live on server zero, so discovery accepts only a root display belonging to the same Gamescope PID.

Evidence is evaluated in this order:

1. The process-start exposure policy is authoritative. Disabled exposure means confirmed SDR.
2. `GAMESCOPE_COLOR_APP_WANTS_HDR_FEEDBACK` is the primary application intent.
3. Application HDR metadata is accepted as positive evidence when the Boolean property is unavailable.
4. `GAMESCOPE_HDR_OUTPUT_FEEDBACK` is diagnostic only. An HDR-capable display does not mean the current game selected HDR.
5. `DXVK_HDR` describes exposure/capability, not live application intent. `DXVK_HDR=0` is conclusively SDR; `DXVK_HDR=1` alone does not activate HDR.

Gamescope-owned feedback is provisional at process startup because a root property may still describe the previous held commit. `StableBooleanFeedback` requires 750 ms of uninterrupted evidence before changing the confirmed state. Unknown samples reset a pending transition instead of inheriting its elapsed time. Feedback is sampled every 250 ms under Gamescope and every second on an ordinary desktop.

No X11 query runs in `vkQueuePresentKHR`. Presentation consumes the latest sample collected by the monitor.

## Swapchain classification

`classifySwapchainColor()` classifies the complete Vulkan format/colour-space pair. Component width alone never determines HDR semantics.

| Vulkan input | Required evidence | MAKO encoding | Shared image format | Meaning |
| --- | --- | --- | --- | --- |
| 8-bit RGBA/BGRA + nonlinear sRGB | None | `Sdr8` | `R8G8B8A8_UNORM` | Standard SDR |
| 10-bit UNORM or RGBA16F + nonlinear sRGB | No confirmed HDR | `SdrHighPrecision` | `R16G16B16A16_SFLOAT` | High-precision SDR |
| 10-bit UNORM + HDR10/ST2084 | Explicit colour space | `Hdr10Pq` | RGBA16F or packed 10-bit | HDR10/PQ |
| RGBA16F + extended linear sRGB | Explicit colour space | `ScRgbLinear` | `R16G16B16A16_SFLOAT` | Linear scRGB HDR |
| 10-bit UNORM or RGBA16F + normalized sRGB | Confirmed Gamescope app HDR | HDR10/PQ or scRGB | Format-dependent | Gamescope colour-space recovery |
| HLG, Dolby Vision, or an invalid pair | N/A | Unsupported | None | Real-frame passthrough |

Gamescope WSI can consume the application's HDR colour space and forward an sRGB-normalized create structure to a lower layer. MAKO recovers HDR semantics only when application-owned Gamescope feedback agrees and the format is one of the validated HDR-capable formats. An ordinary 8-bit swapchain is never promoted to HDR.

If exposure is disabled and classification nevertheless sees an explicit HDR pair, frame generation is disabled for that swapchain and the game's real frames continue through the native presentation path.

## Colour and resource flow

The application-facing layer and private backend may use different Vulkan devices, so source and generated images cross an external-memory boundary. `FrameEncoding` records the meaning of those images.

```text
Game swapchain image
        |
        v
Application-device exchange image
        |
        |  HDR10/PQ only: PQ BT.2020 -> linear scRGB BT.709
        v
Private backend RGBA16F working images
        |
        v
High-precision frame-generation model
        |
        |  HDR10/PQ only: linear scRGB BT.709 -> PQ BT.2020
        v
Application-device generated image -> game swapchain
```

- HDR10/PQ is converted to linear scRGB before inference and converted back afterwards. ST 2084 is normalized to 10,000 nits; scRGB uses 80 nits per unit. The shaders also convert between BT.2020 and BT.709 primaries.
- Linear scRGB already matches the model's working representation and does not need the PQ conversion passes.
- High-precision SDR uses RGBA16F transport but does not acquire HDR transfer semantics.
- Every non-`Sdr8` encoding selects the backend's high-precision generate shader. Only scRGB and HDR10 select HDR model constants.

### Packed HDR10 boundary transport

HDR10 normally crosses the device boundary as RGBA16F. MAKO may instead use `A2B10G10R10_UNORM_PACK32` when both devices prove the required external-image and format features, the backend exposes the packed output shader, and storage image extended formats are supported.

This optimization compresses only the application/backend boundary from eight to four bytes per pixel. The model and intermediate images remain RGBA16F, so interpolation precision is not reduced. Unsupported hardware automatically falls back to RGBA16F; it must never fall back to a lower-precision model.

## Presentation transport and live transitions

`selectPresentationTransport()` makes an immutable create-time choice:

- `OrderedSdr` forces the lower swapchain to FIFO and filters Gamescope's dynamic MAILBOX override. This is the supported release transport.
- `GamescopeHdr` preserves the experimental Gamescope HDR bridge contract, uses nonblocking generated-image admission, and always lets a real frame win over unfinished private work.

The HDR transport is selectable only when Gamescope is detected, the swapchain is HDR-capable, WSI is present, and HDR exposure is allowed. The current managed launchers intentionally make those conditions false.

Stable SDR/HDR feedback may rebuild MAKO's private images, backend context, and colour conversions in place. It does not recreate the game-owned `VkSwapchainKHR` and does not change its presentation transport. Before a private rebuild, MAKO checks backend and fence readiness. A failed rebuild retains native real-frame passthrough and schedules a bounded retry.

## Future HDR lane

MAKO Decky's off-by-default Gamescope WSI compatibility path proves that a curated 64-bit host manifest can cross Steam Runtime Pressure Vessel with deterministic loader evidence. It remains SDR-only: HDR exposure is disabled, inherited `DXVK_HDR` is removed, and unsupported packaging fails closed. It is infrastructure for HDR research, not HDR validation.

Gamescope WSI is an active HDR bridge. It exposes HDR10 and scRGB surface formats, carries the application's original format and colour space while normalizing the lower create structure, forwards `vkSetHdrMetadataEXT`, and publishes application-owned intent separately from output capability. MAKO's X11 reader can observe the result but cannot replace this discovery and metadata path. See Gamescope's [WSI implementation](https://github.com/ValveSoftware/gamescope/blob/master/layer/VkLayer_FROG_gamescope_wsi.cpp), [swapchain protocol](https://github.com/ValveSoftware/gamescope/blob/master/protocol/gamescope-swapchain.xml), and the Khronos [`VK_EXT_hdr_metadata` specification](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_hdr_metadata.html).

Stock Gamescope WSI also owns presentation policy: it can force a lower MAILBOX swapchain, implement FIFO above it, submit timing, and recreate swapchains for limiter state. It is not a metadata-only layer and can reintroduce pacing conflicts. The near-term research path must coexist with that complete layer through MAKO's native-first `GamescopeHdr` transport. A future metadata-only Gamescope mode that delegates pacing to MAKO remains an upstream design idea, not an available contract.

### Discovery and ordering

A proof must admit only MAKO's role manifests and the resolved architecture-correct Gamescope WSI manifest; never expose the complete host implicit-layer directory. Continue excluding unrelated overlays, Mesa helpers, capture/vendor hooks, and competing frame generation. Do not infer order from manifest filenames: use controlled directories or a proven meta-layer and verify instance and device call stacks with `VK_LOADER_DEBUG=layer`. The [Vulkan Loader layer interface](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md) defines the discovery constraints.

HDR admission is a process-start profile choice and requires restart. It must preserve WSI's application-facing HDR semantics while placing MAKO's frame-generation role above WSI so every generated present traverses it; any future spatial role remains below WSI. Begin with a 64-bit native-host proof. Validate 32-bit, Flatpak, Heroic/UMU, and non-SteamOS layouts independently before expanding support.

### Staged proof plan

1. **Loader:** admit only MAKO and Gamescope WSI, keep generation off, and prove the exact instance/device order.
2. **Passthrough:** confirm one known 64-bit HDR game exposes HDR, reports application intent, selects `hdr10-pq` or `scrgb-linear`, and preserves correct native frames.
3. **Fixed 2x:** prove correct generated encoding, native-first pressure behavior, and no base-FPS collapse.
4. **Adaptive:** compare Fractional and Steady cadence, misses, recovery, latency, and quality with native HDR.
5. **Lifecycle:** cover focus, overlays, menus, resolution, natural recreation, metadata changes, hitches, and shutdown without stale feedback or rebuild loops.
6. **Compatibility:** repeat on native Vulkan, DXVK, VKD3D-Proton, supported AMD classes, FP32 and FP16, then validate other architectures and sandboxes.

Release requires deterministic layer order, active-game HDR evidence, correct explicit or recovered classification, native fallback, native-matching colour/highlights, and stable pacing. Washed-out or crushed output, coloured motion artifacts, stale metadata, unexpected layers, blocked native presents, or unexplained cadence loss are blockers. Until this proof passes, the supported launch remains isolated SDR.

## Invariants for future HDR work

- Never use output HDR capability as application HDR intent.
- Never infer PQ from a 10-bit format without a colour space or confirmed application feedback.
- Never enable the Gamescope HDR transport when Gamescope WSI was isolated at process start.
- Never expose the complete host implicit-layer directory as an HDR compatibility policy.
- Never assume discovery proves the required `MAKO frame generation -> Gamescope WSI -> optional MAKO spatial scaling` role order; retain loader evidence.
- Never change presentation transport for a live `VkSwapchainKHR`.
- Never force game-owned recreation for HDR or transport changes. Spatial and frame-generation resource controls keep their independent boundaries from [Runtime configuration transitions](RUNTIME-TRANSITIONS.md); none changes transport on a live swapchain.
- Never wait for a generated image on the Gamescope HDR bridge; native frames take priority under pressure.
- Never treat an unavailable generated image as temporal-history corruption.
- Keep unsupported encodings and failed private transitions on real-frame passthrough.
- Validate FP32 and FP16 separately; packed transport support is independent of model precision.

## Known risks and required validation

HDR groundwork passing unit tests is not proof of game compatibility. Before exposing HDR, validate at minimum:

- native Vulkan, DXVK, and VKD3D-Proton HDR games;
- explicit HDR10/PQ and linear-scRGB swapchains;
- Gamescope normalized colour spaces and metadata-only activation;
- SDR/HDR changes, focus changes, overlays, resolution changes, and natural swapchain recreation;
- packed HDR10 and RGBA16F fallback on every supported AMD hardware class;
- colour ramps, neutral greys, highlights, saturated colours, motion, and UI;
- Fixed and Adaptive delivery at 40, 60, 90, and 120 Hz;
- native, Steam/Pressure Vessel, Heroic/UMU, and Flatpak launch boundaries.

Look specifically for washed-out or crushed output, purple/green motion artifacts, SDR being interpreted as PQ, HDR feedback inherited from another game, repeated private-context rebuilds, generated delivery misses, frame-rate collapse, and extra device-memory pressure.

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Process-start WSI/HDR policy and transport choice | `mako-render/src/presentation_policy.hpp` |
| Gamescope X11 discovery, evidence, and diagnostics | `mako-render/src/gamescope_hdr_feedback.cpp` |
| Feedback stabilization | `mako-render/src/runtime_transition.hpp` |
| Format and colour-space classification | `mako-render/src/color_pipeline.cpp` |
| Swapchain mutation and private resource transitions | `mako-render/src/swapchain.cpp` |
| Native-first HDR presentation behavior | `mako-render/src/swapchain_present.cpp` |
| Backend encoding and conversion passes | `mako-backend/src/mako.cpp` |
| PQ/scRGB shaders | `mako-backend/src/shaders/` |
| Embedded conversion SPIR-V generation and source/payload freshness | `scripts/generate-color-conversion-spirv.py`, exercised by the portable CTest suite |
| Deterministic HDR and transport tests | `mako-render/tests/` |

Useful logs include `swapchain colour pipeline`, `HDR10 transport`, `Gamescope application HDR feedback`, `runtime-transition-pending`, and `runtime-transition-applied`. Collect them with the `hdr`, `layers`, and `recovery` diagnostics presets described in [Collect diagnostics](COLLECT_DIAGNOSTICS.md).
