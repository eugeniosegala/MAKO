# Testing MAKO

MAKO separates deterministic product checks from real-hardware evidence. Portable tests are required for every change, but they do not prove Vulkan presentation, image quality, Gamescope behavior, or game compatibility.

## Ownership

| Boundary | Owner |
| --- | --- |
| Product code, portable tests, production-path CLI tools, package contracts, and Gym bridge | MAKO |
| Licensed-model execution, declarative hardware scenarios, Vulkan/Gamescope orchestration, runtime assertions, performance budgets, artifact validation, and sanitized local evidence | Private MAKO Gym |
| Comparative real-game sessions, schemas, checksums, and append-only evidence | Private MAKO Traces |

Portable MAKO tests must not require MAKO Gym, `Lossless.dll`, an AMD GPU, Gamescope, or a compositor. MAKO Gym consumes MAKO's public executable and diagnostic boundaries; it must not copy production C++, shaders, thresholds, or configuration owners.

## Validation cycles

| Cycle | Proves | Does not prove |
| --- | --- | --- |
| Portable change gates | Deterministic Renderer, Decky, schema, package-policy, sanitizer, and producer behavior | Real Vulkan presentation, AMD image quality, or installation |
| Direct device iteration | A focused change works on the current development installation | Archive layout, clean-checkout reproducibility, 32-bit, or Flatpak behavior unless selected |
| Complete tester ZIP | Local source packages and installs as one self-contained build | A clean pushed commit, hardware release gate, or public asset |
| SteamOS hardware gate | A pushed commit rebuilds and passes the AMD, loader, Gym, dual-bitness, Flatpak, and package boundaries | The complete commercial-game matrix or published download |
| Manual release-candidate matrix | Selected games survive relevant presentation, focus, overlay, hitch, and recreation scenarios | Untested games and hardware |
| Published-package check | The exact GitHub asset installs through the user-facing path | Universal compatibility |

Use [MAKO Decky packaging](plugin/docs/PACKAGING.md) for development and tester builds and [How to release MAKO](HOW_TO_RELEASE.md) for release validation and publication.

## Portable gates

The `Tests` workflow runs on every pull request and push to `main`:

- MAKO Decky Python tests, frontend behavior and coverage, type checking, localization/configuration freshness, and production bundle;
- MAKO Renderer CTest with GCC and Clang, including the optional Qt UI and localization contract;
- portable Renderer scheduling, frame-plan, presentation, scaling, transition, profile, and colour boundaries under ASan and UBSan;
- synthetic unlicensed PE/DXBC/SPIR-V inspection, fingerprint invalidation, per-model capability isolation, and harmless-extension acceptance;
- trace-producer staging, sanitization, metadata, checksums, containment, rollback, and no-clobber behavior on Linux and macOS;
- protected-input rejection for licensed model content and disguised binary, shader, dump, or archive payloads; and
- Markdown formatting.

The Renderer suite also validates `mako-launch`, generated SPIR-V freshness, and the optional/required MAKO Gym bridge. Decky tests cover installation, typed RPC/configuration contracts, profile transactions, generated wrapper and sidecar bytes, centralized frontend writes, localization, Flatpak/runtime boundaries, and fail-closed Armada behavior. See the owning component documentation for detailed invariants rather than duplicating them here.

Run the portable gates locally from the repository root:

```bash
just check-markdown-format
./scripts/test-protected-inputs.sh
./scripts/test-capture-trace.sh
npm --prefix plugin run check:generated-config
npm --prefix plugin run check:i18n
npm --prefix plugin test
npm --prefix plugin run test:frontend:typecheck
npm --prefix plugin run test:frontend:coverage
npm --prefix plugin run build
(cd engine && ./scripts/test-adaptive-scheduler.sh)
(cd engine && MAKO_ENABLE_SANITIZERS=ON ./scripts/test-adaptive-scheduler.sh)
```

The full Renderer suite additionally needs the Vulkan and X11 development headers:

```bash
cmake -S engine -B engine/build/local -DBUILD_TESTING=ON -DMAKO_BUILD_UI=OFF
cmake --build engine/build/local
ctest --test-dir engine/build/local --output-on-failure
```

Use `-DMAKO_BUILD_UI=ON` when Qt 6 Base and Declarative development packages are installed. Shader changes must regenerate their adjacent embedded payloads with the owning generator; never edit generated SPIR-V arrays or hashes directly.

## Selecting MAKO Gym coverage

MAKO Gym is an optional sibling checkout for local development and a required release-gate dependency. The bridge skips clearly when Gym is absent unless `--require` is used; required mode also rejects a missing runner or incompatible `GYM_CONTRACT_VERSION`.

Before any hardware run, validate Gym's portable contracts with `(cd ../MAKO-Gym && ./scripts/check.sh)` or `just check` from its checkout.

Run the smallest suite and filter that can observe a change. A filtered pass is evidence only for its selected rows. Widen to the complete affected suite when a shared production owner changes; run every suite only for the release gate, a genuinely cross-cutting change, or an explicit final-validation request.

| Change boundary | Start with |
| --- | --- |
| Documentation, website, Decky-only UI/backend, or portable schema | Owning portable tests; no Gym unless a Renderer-facing contract changed |
| Fixed/Adaptive configuration, construction, option combinations, scaler selection, or fallback | `vulkan` feature matrix with matching labels |
| LSFG/spatial pixels, shaders, colour, precision, Flow Scale, model selection, or combined handoff | `quality`; add `vulkan` for lifecycle changes |
| LSFG dispatch cost, model/precision/Flow throughput, or generated capacity | `performance` |
| Spatial graph, copy, factor, resolution, or combined GPU cost | `spatial-performance` |
| Present/frame tails, process CPU/RSS, Vulkan allocations, or “feels heavy” reports | `runtime-overhead --tier fast` |
| Barriers, image transitions, command recording, or exported-resource synchronization | `sync-validation` |
| Initialization or unexplained pixel instability | `repeatability` |
| Cadence, Steady/Fractional Adaptive, recovery, stalls, or swapchain lifetime | `recovery`; add `vulkan` if construction changed |
| Native compositor, WSI, cross-layer live transition, or resolution lifecycle | `gamescope-e2e` |
| D3D11/DXVK or D3D12/VKD3D-Proton translation | `proton-e2e` |
| Proton-family/version behavior | `proton-compatibility --runtime-tier core --case-tier fast` |

Common commands:

```bash
./engine/scripts/run-mako-gym.sh --suite vulkan --list
./engine/scripts/run-mako-gym.sh --suite vulkan --filter '^(fixed-|adaptive-)'
./engine/scripts/run-mako-gym.sh --suite quality --filter '^combined-.*traffic' --cli "$PWD/engine/build/mako-cli/mako-cli"
./engine/scripts/run-mako-gym.sh --suite performance --cli "$PWD/engine/build/mako-cli/mako-cli"
./engine/scripts/run-mako-gym.sh --suite spatial-performance --cli "$PWD/engine/build/mako-cli/mako-cli"
./engine/scripts/run-mako-gym.sh --suite runtime-overhead --tier fast --launcher /path/to/mako-launch
./engine/scripts/run-mako-gym.sh --suite sync-validation --cli "$PWD/engine/build/mako-cli/mako-cli"
./engine/scripts/run-mako-gym.sh --suite repeatability --cli "$PWD/engine/build/mako-cli/mako-cli"
./engine/scripts/run-mako-gym.sh --suite recovery --filter '(stall|cadence-drop|recreate)$'
./engine/scripts/run-mako-gym.sh --suite gamescope-e2e --filter '^gamescope-live-'
./engine/scripts/run-mako-gym.sh --suite proton-e2e --filter '^proton-vkd3d-hud-'
./engine/scripts/run-mako-gym.sh --suite proton-compatibility --runtime-tier core --case-tier fast
```

`just test-engine-gym-*` provides equivalent shortcuts. Set `MAKO_GYM_REPO` or pass `--gym-repo <path>` when the checkout is not a sibling.

### Suite inventory

The manifests and guides in MAKO Gym are authoritative for row semantics, thresholds, prerequisites, artifacts, and claim limits.

| Suite | Rows | Primary evidence | Guide |
| --- | --: | --- | --- |
| `vulkan` | 48 | Real Vulkan construction and configured Fixed/Adaptive/scaler combinations | `docs/VULKAN-FEATURE-MATRIX.md` |
| `quality` | 74 | Procedural LSFG, spatial, and combined output against references | `docs/AMD-QUALITY-REGRESSION.md` |
| `performance` | 18 | Repeated LSFG throughput and variance through 5120×2160 | `docs/RENDER-PERFORMANCE.md` |
| `spatial-performance` | 36 | Pixel-qualified production spatial graphs with Vulkan timestamps | `docs/SPATIAL-PERFORMANCE.md` |
| `runtime-overhead` | 14 | Paired idle/active present, frame, CPU, RSS, and MAKO allocation budgets | `docs/RUNTIME-OVERHEAD.md` |
| `sync-validation` | 8 | Khronos synchronization validation on canonical quality paths | `docs/SYNCHRONIZATION-VALIDATION.md` |
| `repeatability` | 9 | Byte-identical output across three independent initializations | `docs/QUALITY-REPEATABILITY.md` |
| `recovery` | 31 | Scripted cadence, hitch, stall, live-scaling, and swapchain lifecycle | `docs/RUNTIME-RECOVERY-MATRIX.md` |
| `gamescope-e2e` | 27 | Native Gamescope WSI, fixed/variable surfaces, live controls, resolution transitions, and Adaptive efficiency-backoff endurance | `docs/GAMESCOPE-END-TO-END.md` |
| `proton-e2e` | 12 | Six deterministic scenes through both DXVK and VKD3D-Proton | `docs/PROTON-END-TO-END.md` |
| `proton-compatibility` | 10 | Stratified sentinels across provenance-checked Proton families | `docs/PROTON-COMPATIBILITY.md` |

The complete release lane runs all rows and the extended Proton compatibility set across at least four runtime families. Portable validators prove inventory correctness only. GPU suites do not prove subjective quality, input-to-photon latency, power, scanout timing, arbitrary games, other GPUs/drivers, 32-bit presentation, Flatpak behavior, or HDR unless the owning row explicitly covers that boundary. Record unavailable coverage as **not tested**.

Spatial changes must also follow the surface, extent, queue, format, startup, live-transition, synchronization, and quality matrix in [Spatial scaling architecture](engine/docs/SCALING.md). Scheduler and presentation changes must follow [Adaptive validation](engine/docs/ADAPTIVE-VALIDATION.md). A successful `vulkaninfo` or finite `vkcube` run proves only its narrow loader or presentation boundary.

## SteamOS hardware release gate

Before publishing, run the gate against a clean, pushed commit on the dedicated SteamOS/AMD machine:

```bash
./scripts/run-steamos-hardware-validation.sh
```

The launcher requires the normal host/Flatpak build prerequisites, `vulkaninfo`, `vkcube`, Gamescope, a local licensed `Lossless.dll`, and a clean compatible MAKO Gym checkout. It creates a disposable one-job GitHub Actions runner, verifies the official runner archive, and rebuilds the complete dual-bitness/Flatpak package. Quality, LSFG/spatial performance, synchronization, and repeatability run against the clean source-built `mako-cli`; the feature, runtime-overhead, recovery, Gamescope, Proton E2E, and Proton-compatibility suites use the exact extracted Renderer package. The gate records the Gym commit and contract version, retains sanitized evidence and the verified ZIP for 14 days, and removes runner credentials and staging afterward.

Pass `--deploy-to-decky` only on the dedicated MAKO Decky test installation. It deploys the already-verified ZIP and invokes the production installer; it does not rebuild through a separate CI path. The hardware gate does not replace the manual DXVK, VKD3D-Proton, Gamescope, focus, overlay, hitch, recreation, and supported desktop matrix in the Renderer guides, nor the final published-package installation check.

## Real-game traces

After a completed game session, `scripts/capture-trace.sh` can archive sanitized comparative evidence into a sibling private MAKO Traces checkout. [The trace extractor guide](TRACES.md) owns setup, commands, privacy, checksums, and contract changes. Run the producer test without private data using:

```bash
./scripts/test-capture-trace.sh
```

A stored trace supports comparison; one playthrough does not prove a performance or image-quality regression.
