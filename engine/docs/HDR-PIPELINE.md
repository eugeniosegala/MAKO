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

Managed launches establish the SDR boundary before Vulkan starts:

```text
DISABLE_GAMESCOPE_WSI=1
MAKO_DISABLE_HDR_EXPOSURE=1
DXVK_HDR unset
```

MAKO Decky generates that environment for native, Heroic/UMU, and supported Flatpak launches. The standalone `mako-launch` helper applies the same policy. `resolvePresentationEnvironmentPolicy()` in `mako-render/src/presentation_policy.hpp` is the engine-side authority: WSI isolation itself conclusively disables the Gamescope HDR bridge, even if a caller forgot the explicit HDR variable or compositor feedback reports HDR. The root resolves this policy once before starting the feedback monitor and passes the same immutable snapshot to swapchain and feedback decisions.

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
- `GamescopeHdr` preserves the experimental Gamescope WSI contract, uses nonblocking generated-image admission, and always lets a real frame win over unfinished private work.

The HDR transport is selectable only when Gamescope is detected, the swapchain is HDR-capable, WSI is present, and HDR exposure is allowed. The current managed launchers intentionally make those conditions false.

Stable SDR/HDR feedback may rebuild MAKO's private images, backend context, and colour conversions in place. It does not recreate the game-owned `VkSwapchainKHR` and does not change its presentation transport. Before a private rebuild, MAKO checks backend and fence readiness. A failed rebuild retains native real-frame passthrough and schedules a bounded retry.

## Future discovery: curated Gamescope WSI injection

MAKO Decky now has an off-by-default, per-profile **Experimental Gamescope WSI** compatibility control that stages the validated host 64-bit WSI manifest and admits it through a managed process-start path. This is deliberately an SDR-only evidence lane: it retains `MAKO_DISABLE_HDR_EXPOSURE=1`, removes inherited `DXVK_HDR`, and fails closed on Flatpak or when the manifest contract is unavailable. Its existence proves that selective UI-driven discovery can be exercised without reopening the global default; it does not validate HDR colour, metadata, presentation order, or performance.

The validated [MangoHud layer chain](LAYER-CHAINING.md) established an important process-start capability: a Steam launch can carry a deliberately expanded implicit-layer set through Steam Runtime Pressure Vessel, and Vulkan loader diagnostics can prove the resulting instance and device order. That result does not prove HDR correctness, but it turns selective Gamescope WSI admission from an architectural assumption into a concrete experiment.

This may be the missing HDR activation mechanism rather than a new colour-processing design. MAKO already contains HDR10/PQ and scRGB classification, high-precision transport, Gamescope application-HDR feedback, colour conversion shaders, private resource transitions, and the native-first `GamescopeHdr` presentation policy. The current managed launch prevents those pieces from meeting because it excludes Gamescope WSI before Vulkan starts.

### 64-bit discovery hypothesis

The SteamOS host used for the initial layer-chain evidence exposes this architecture-specific manifest:

```text
/usr/share/vulkan/implicit_layer.d/VkLayer_FROG_gamescope_wsi.x86_64.json
```

It selects `VK_LAYER_FROG_gamescope_wsi_x86_64`, points to the host's 64-bit Gamescope WSI library, and is gated by `ENABLE_GAMESCOPE_WSI=1` and `DISABLE_GAMESCOPE_WSI=1`. These paths are host evidence, not a portable interface: an implementation must resolve and validate the installed manifest instead of assuming every distribution uses the SteamOS layout.

Gamescope documents that HDR client support requires its WSI layer. The hypothesis is that an opt-in launch can expose only the architecture-correct Gamescope WSI manifest and MAKO's private manifest, allow HDR exposure at process start, and produce this exact chain:

```text
Game / Proton / DXVK or VKD3D-Proton
                  |
                  v
       Gamescope WSI x86_64
                  |
                  v
          MAKO Renderer
                  |
      generated frame(s) + original
                  |
                  v
             Vulkan driver
```

Gamescope WSI must remain above MAKO. It exposes HDR formats to the application, translates Wine/Proton WSI handles, consumes the application's HDR colour space, and forwards a normalized swapchain to lower layers. MAKO then combines the normalized format with confirmed application-owned Gamescope feedback to recover HDR10/PQ or scRGB semantics. The [Gamescope source](https://github.com/ValveSoftware/gamescope/blob/master/src/main.cpp#L2538) describes WSI as required for HDR client support.

### What the Gamescope source proves

The source review makes the discovery hypothesis more concrete: Gamescope WSI is an active HDR capability and metadata bridge, not merely another place from which MAKO can read an existing HDR flag.

1. **Client exposure:** Gamescope WSI reads `GAMESCOPE_HDR_OUTPUT_FEEDBACK` and, only when HDR output and layer policy allow it, appends HDR10/ST2084 10-bit and extended-linear-sRGB RGBA16F pairs to the application's Vulkan surface formats. This is the step that lets a native Vulkan application or Proton translation layer discover a usable HDR surface at all. DXVK separately documents `DXVK_HDR`/`dxgi.enableHDR` as the switch that tells a Windows game the global Windows HDR mode is enabled; it is exposure, not proof that the game selected HDR. See the Gamescope [WSI implementation](https://github.com/ValveSoftware/gamescope/blob/master/layer/VkLayer_FROG_gamescope_wsi.cpp) and DXVK's [configuration contract](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf).
2. **Original colour-space transport:** On `vkCreateSwapchainKHR`, Gamescope WSI recognizes HDR10/ST2084 and extended-linear-sRGB as HDR, sends the application's original image format and colour space to Gamescope through its swapchain protocol, then normalizes the lower Vulkan swapchain colour space to sRGB. That explains why MAKO must sit below WSI and recover semantics from the normalized lower create structure plus compositor feedback rather than treating the lower colour-space field as the application's original choice. See the [WSI implementation](https://github.com/ValveSoftware/gamescope/blob/master/layer/VkLayer_FROG_gamescope_wsi.cpp) and [Gamescope swapchain protocol](https://github.com/ValveSoftware/gamescope/blob/master/protocol/gamescope-swapchain.xml).
3. **HDR metadata transport:** Gamescope WSI intercepts `vkSetHdrMetadataEXT` and forwards primaries, white point, mastering luminance, MaxCLL, and MaxFALL through the Gamescope swapchain protocol. Gamescope's Wayland server stores that metadata with the swapchain feedback, and the compositor republishes active application metadata through `GAMESCOPE_COLOR_APP_HDR_METADATA_FEEDBACK`. This matches the Vulkan contract: `VK_EXT_hdr_metadata` attaches SMPTE 2086 and CTA 861.3 data to a swapchain for use by the presentation engine; it does not change the image colour space itself. See Gamescope's [WSI layer](https://github.com/ValveSoftware/gamescope/blob/master/layer/VkLayer_FROG_gamescope_wsi.cpp), [Wayland server](https://github.com/ValveSoftware/gamescope/blob/master/src/wlserver.cpp), [compositor feedback](https://github.com/ValveSoftware/gamescope/blob/master/src/steamcompmgr.cpp), and the Khronos [`VK_EXT_hdr_metadata` specification](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_hdr_metadata.html).
4. **Application-owned evidence:** Gamescope derives `GAMESCOPE_COLOR_APP_WANTS_HDR_FEEDBACK` from the colour space of the active held application commit and publishes the associated metadata separately. By contrast, `GAMESCOPE_HDR_OUTPUT_FEEDBACK` reports compositor output state. MAKO's existing evidence priority is therefore correct: output capability may unlock format exposure, but only active application feedback may activate HDR processing.

This closes an important gap in the architecture. MAKO's X11 feedback reader can observe Gamescope's result, but without the WSI bridge it cannot make the game discover HDR surface formats, preserve the application's original HDR colour-space choice across normalization, or carry `vkSetHdrMetadataEXT` into Gamescope. Selective WSI admission may therefore unlock the HDR path already implemented in MAKO rather than require a separate colour pipeline.

The stock layer still cannot be treated as metadata-only injection. In the same swapchain path it forces the lower swapchain to MAILBOX, implements FIFO behavior above it, sends presentation timing, and may recreate swapchains in response to frame-limiter state. Admitting it can therefore reintroduce the pacing conflict that collapsed a nominal 60 FPS game to roughly 13 FPS in the earlier SDR chain.

Two research lanes follow from this result. The near-term lane is to coexist with the complete stock WSI layer through MAKO's existing native-first `GamescopeHdr` transport, where generated work never blocks a real frame. The longer-term breakthrough would be an upstream or experimental Gamescope WSI mode that retains HDR capability negotiation, original colour-space feedback, and metadata forwarding while delegating pacing ownership to MAKO. No such metadata-only stock mode was found in the current Gamescope source, so that split remains a design proposal rather than a supported launch contract.

### Ordering and packaging requirements

Do not expose the complete system implicit-layer directory as the eventual HDR design. A proof must admit only MAKO and the architecture-correct Gamescope WSI layer while continuing to exclude MangoHud, Mesa device selection, Mesa anti-lag, competing frame generation, capture layers, and unrelated vendor hooks.

Do not infer call order from manifest filenames. The Vulkan loader may enumerate multiple manifests in one directory in an unstable order. A durable implementation should use a controlled mechanism such as isolated one-layer directories or an ordered meta-layer, then verify the actual instance and device call stacks with `VK_LOADER_DEBUG=layer`. See the Khronos [Vulkan Loader layer interface](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md) for discovery and ordering constraints.

The HDR lane must be a process-start profile choice that requires a game restart. Its policy would allow Gamescope WSI and HDR exposure before `vkCreateInstance`; it must never remove the validated SDR guards globally or attempt to add WSI to a running Vulkan instance. A first proof should remain 64-bit-only. The x86 manifest, 32-bit runtime dependencies, Flatpak extensions, Heroic/UMU, and other distribution layouts require separate evidence before the lane can expand.

### Staged proof plan

1. **Loader-only proof:** Construct a temporary curated 64-bit discovery path containing only Gamescope WSI and MAKO. Keep frame generation disabled and verify `Application -> Gamescope WSI x86_64 -> MAKO Renderer -> Vulkan driver` at both instance and device creation.
2. **Native HDR passthrough:** Launch one known 64-bit HDR game with MAKO loaded but generation off. Confirm that the game offers HDR, Gamescope reports application HDR intent, MAKO selects `hdr10-pq` or `scrgb-linear`, and the game's original frames remain visually correct.
3. **Fixed 2x generation:** Enable Fixed 2x through the existing `GamescopeHdr` transport. Confirm that generated frames use the correct encoding, native frames win under private-backend pressure, and base FPS does not collapse.
4. **Adaptive generation:** Exercise fractional and steady Adaptive cadences only after Fixed 2x is stable. Compare output cadence, generated-image misses, recovery, latency, and image quality against the native HDR baseline.
5. **Runtime transitions:** Test focus changes, Steam overlays, menus, resolution changes, natural swapchain recreation, HDR metadata changes, hitches, and shutdown without stale feedback or repeated private-resource rebuilds.
6. **Compatibility expansion:** Repeat the proof for native Vulkan, DXVK, VKD3D-Proton, every supported AMD hardware class, FP32 and FP16, then address 32-bit and sandboxed launchers separately.

The discovery succeeds only if the exact layer order is deterministic, HDR intent belongs to the active game, explicit or recovered colour classification is correct, original-frame passthrough remains available, generated colours and highlights match the native baseline, and presentation avoids the former Gamescope/MAKO frame-rate collapse. Washed-out or crushed output, purple or green motion, stale metadata, unexpected layers, blocked native presents, or unexplained cadence loss are release-blocking failures.

If this proof succeeds, MAKO Decky could eventually offer a per-game experimental HDR launch policy with explicit restart semantics and fail-closed fallback. Until then, the supported launch remains the isolated SDR path and this section records a high-value discovery direction rather than a user-facing HDR command.

## Invariants for future HDR work

- Never use output HDR capability as application HDR intent.
- Never infer PQ from a 10-bit format without a colour space or confirmed application feedback.
- Never enable the Gamescope HDR transport when Gamescope WSI was isolated at process start.
- Never expose the complete host implicit-layer directory as an HDR compatibility policy.
- Never assume discovery proves the required `Gamescope WSI -> MAKO Renderer` order; retain loader evidence.
- Never change presentation transport for a live `VkSwapchainKHR`.
- Never force a game-owned swapchain recreation for a Decky/UI setting change.
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
