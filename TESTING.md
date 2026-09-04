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
| SteamOS hardware gate | A pushed commit rebuilds the candidate and validates AMD loading, dual-bitness, Flatpak, package, and explicitly selected Gym boundaries | The complete commercial-game matrix or published download |
| Manual release-candidate matrix | Selected games survive relevant presentation, focus, overlay, hitch, and recreation scenarios | Untested games and hardware |
| Published-package check | The exact GitHub asset installs through the user-facing path | Universal compatibility |

Use [MAKO Decky packaging](plugin/docs/PACKAGING.md) for development and tester builds and [How to release MAKO](HOW_TO_RELEASE.md) for release validation and publication.

## Portable gates

The `Tests` workflow runs on every pull request and push to `main`:

- MAKO Decky backend/frontend tests, type checking, coverage, generated-contract freshness, localization, production bundling, and package-license contracts;
- MAKO Renderer CTest with GCC and Clang, including Qt, localization, synthetic model inspection, launch policy, and generated-SPIR-V freshness;
- portable Renderer policy tests under ASan and UBSan;
- protected-input, trace-producer, and Gym-selection contracts on their supported hosts; and
- Markdown formatting.

The owning component tests remain authoritative for their detailed invariants.

Run `just test` for protected-input and Gym-selection contracts, Renderer CTest, Decky backend/frontend tests, and the trace producer. Add the checks below for the complete local portable gate:

```bash
just check-markdown-format
just test
pnpm --dir plugin run test:frontend:typecheck
pnpm --dir plugin run test:frontend:coverage
pnpm --dir plugin run build
just test-engine-sanitized
```

The focused Renderer policy script compiles Vulkan-facing policy headers, so it needs Vulkan headers even though it does not need a Vulkan device or runtime. The full Renderer suite additionally needs the Vulkan loader and X11 development files:

```bash
cmake -S engine -B engine/build/local -DBUILD_TESTING=ON -DMAKO_BUILD_UI=OFF
cmake --build engine/build/local
ctest --test-dir engine/build/local --output-on-failure
```

Use `-DMAKO_BUILD_UI=ON` when Qt 6 Base and Declarative development packages are installed. Shader changes must regenerate their adjacent embedded payloads with the owning generator; never edit generated SPIR-V arrays or hashes directly.

## Selecting MAKO Gym coverage

MAKO Gym is an optional sibling checkout for local development and a required release-gate dependency. The bridge skips clearly when Gym is absent unless `--require` is used; required mode also rejects a missing runner or incompatible `GYM_CONTRACT_VERSION`.

Before any hardware run, validate Gym's portable contracts with `(cd ../MAKO-Gym && ./scripts/check.sh)` or `just check` from its checkout.

The bridge exposes one selection pattern. `--list-suites` discovers suites, `--all-suites --validate` validates their inventories without hardware, `--suite NAME --list` discovers rows, and `--filter REGEX` runs a subset. Omitting `--filter` runs the complete selected suite:

```bash
./engine/scripts/run-mako-gym.sh --list-suites
./engine/scripts/run-mako-gym.sh --require --all-suites --validate
./engine/scripts/run-mako-gym.sh --suite recovery --list
./engine/scripts/run-mako-gym.sh --suite recovery
```

Run the smallest suite and filter that can observe a change. A filtered pass is evidence only for its selected rows. Widen to the complete affected suite when a shared production owner changes. A release does not automatically select every suite; run all suites only for a genuinely cross-cutting change or an explicit maintainer request.

Retained evidence may be reused instead of duplicated only when the exact gate-built Renderer/package identity and source commit, host and driver, Gym commit, configuration, and required rows match the candidate. Record the prior run identifier in the release rationale. A code, package, driver, scenario, or assertion change invalidates the affected evidence.

| Change boundary | Start with |
| --- | --- |
| Documentation, website, Decky-only UI/backend, or portable schema | Owning portable tests; no Gym unless a Renderer-facing contract changed |
| Fixed/Adaptive configuration, construction, option combinations, scaler selection, or fallback | `vulkan` feature matrix with matching labels |
| LSFG/spatial pixels, shaders, colour, precision, Flow Scale, model selection, or combined handoff | `quality`; add `vulkan` for lifecycle changes |
| LSFG dispatch cost, model/precision/Flow throughput, or generated capacity | `performance` |
| Spatial graph, copy, factor, resolution, or combined GPU cost | `spatial-performance` |
| Present/frame tails, process CPU/RSS, Vulkan allocations, or “feels heavy” reports | `runtime-overhead` |
| Barriers, image transitions, command recording, or exported-resource synchronization | `sync-validation` |
| Initialization or unexplained pixel instability | `repeatability` |
| Cadence, Steady/Fractional Adaptive, recovery, stalls, or swapchain lifetime | `recovery`; add `vulkan` if construction changed |
| External full-cover overlay pause/throttle or workload-proven source-return recovery | `external-recovery` |
| Native compositor, WSI, cross-layer live transition, or resolution lifecycle | `gamescope-e2e` |
| Native direct scaling/Frame Generation or Steam Desktop Proton Frame Generation and extent-override fallback | `direct-desktop-e2e` |
| Repeated private-resource memory plateau or sustained thermal/performance health | `sustained-health` |
| D3D11/DXVK or D3D12/VKD3D-Proton translation | `proton-e2e` |
| Proton-family/version behavior | `proton-compatibility` |

`just test-engine-gym-*` provides equivalent shortcuts. Set `MAKO_GYM_REPO` or pass `--gym-repo <path>` when the checkout is not a sibling.

MAKO Gym's manifests and guides are authoritative for current rows, thresholds, prerequisites, artifacts, and claim limits. Portable validation proves inventory correctness only. A GPU suite does not prove subjective quality, input-to-photon latency, power, scanout timing, arbitrary games, other GPUs or drivers, 32-bit presentation, Flatpak behavior, or HDR unless a selected row covers that boundary. Record unselected and unavailable coverage as **not tested**.

Spatial changes must also follow the surface, extent, queue, format, startup, live-transition, synchronization, and quality matrix in [Spatial scaling architecture](engine/docs/SCALING.md). Scheduler and presentation changes must follow [Adaptive validation](engine/docs/ADAPTIVE-VALIDATION.md). A successful `vulkaninfo` or finite `vkcube` run proves only its narrow loader or presentation boundary.

## SteamOS hardware release gate

Before publishing, choose the affected Gym suites from the table above and run the gate against a clean, pushed commit on the dedicated SteamOS/AMD machine:

```bash
./scripts/run-steamos-hardware-validation.sh \
  --gym-suite recovery \
  --gym-suite gamescope-e2e \
  --gym-reason 'Adaptive presentation and Gamescope lifecycle changed'
```

When an explicit maintainer request or genuinely cross-cutting change requires every hardware suite, use the release gate's aggregate mode so each suite receives its correct candidate CLI or packaged launcher inputs:

```bash
./scripts/run-steamos-hardware-validation.sh \
  --all-gym-suites \
  --gym-reason 'Explicit complete Renderer hardware audit'
```

Choose exactly one mode: repeat `--gym-suite`, use `--no-gym-suites` when no Renderer-facing boundary changed or exact evidence is reused, or use `--all-gym-suites` for a genuinely cross-cutting or explicitly requested audit. `--gym-reason` is always required and must identify reused evidence. Inventory validation is portable; only selected suites produce hardware evidence. The host still needs the package prerequisites, `vulkaninfo`, `vkcube`, Gamescope, a local licensed `Lossless.dll`, and a clean compatible MAKO Gym checkout.

The gate creates a disposable one-job GitHub Actions runner and always rebuilds the complete dual-bitness/Flatpak package. CLI suites use the clean source-built `mako-cli`; runtime suites use the extracted candidate package. Direct-desktop coverage must start from a graphical session outside Gamescope. The gate records the selection, rationale, Gym identity, and sanitized results, retains the evidence and ZIP for 14 days, then removes credentials and staging. Publication later makes its own release-version and pin commits and independently rebuilds in the enforced order rather than promoting this ZIP byte-for-byte, so the published-package check remains separate.

Pass `--deploy-to-decky` only on the dedicated MAKO Decky test installation. It deploys the verified ZIP through the production installer. The gate does not replace the applicable manual game matrix or final published-package installation check.

## Real-game traces

After a completed game session, `scripts/capture-trace.sh` can archive sanitized comparative evidence into a sibling private MAKO Traces checkout. [The trace extractor guide](TRACES.md) owns setup, commands, privacy, checksums, and contract changes. Run the producer test without private data using:

```bash
./scripts/test-capture-trace.sh
```

A stored trace supports comparison; one playthrough does not prove a performance or image-quality regression.
