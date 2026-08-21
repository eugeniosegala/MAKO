# Testing MAKO

MAKO uses separate gates for deterministic product behavior and hardware behavior. A passing CPU-only suite is required for every change, but it is not treated as evidence that a Vulkan driver, Gamescope, or a game presents generated frames correctly.

## Validation cycles

Each cycle answers a different question and the later cycles do not erase evidence required by the earlier ones:

| Cycle | What it proves | What it does not prove |
| --- | --- | --- |
| Portable change gates | Deterministic Renderer, backend, frontend, schema, packaging-policy, and sanitizer behavior | Real Vulkan presentation, AMD image quality, or installation |
| Direct device iteration | The selected change works quickly on the current SteamOS development installation | Archive layout, clean-checkout reproducibility, 32-bit or Flatpak behavior unless explicitly included |
| Full local tester ZIP | The complete local source can be packaged, installed, and upgraded as one self-contained build | A clean remote commit, the release hardware gate, or public assets |
| SteamOS hardware release gate | The pushed commit rebuilds in a clean checkout and passes the real AMD, Vulkan-loader, dual-bitness, Flatpak, package, and optional exact-ZIP deployment boundaries | The full game/runtime matrix or a published release |
| Manual release-candidate matrix | Selected real games survive the presentation, focus, overlay, hitch, and swapchain scenarios in scope | The public download and production release page |
| Published-package check | The exact GitHub asset downloads and installs through the normal user path | Compatibility with every untested game or hardware configuration |

Use [MAKO Decky packaging](plugin/docs/PACKAGING.md) for direct and tester cycles, [How to release MAKO](HOW_TO_RELEASE.md) for the release candidate and publication sequence, and the sections below for the required gates.

## Pull-request gates

The `Tests` GitHub Actions workflow runs on every pull request and push to `main`:

- **MAKO Decky:** the Python backend suite, focused frontend behavior tests with coverage thresholds, and the production Decky bundle build;
- **MAKO Renderer:** the complete non-hardware CTest suite with both GCC and Clang on Linux;
- **Renderer sanitizers:** the portable scheduling, generated-frame-plan, presentation-policy, profile, transition, and colour-math boundaries under AddressSanitizer and UndefinedBehaviorSanitizer; and
- **Trace producer:** safe capture staging, sanitization, metadata, checksum, containment, rollback, and concurrent no-clobber behavior on Linux and macOS.

The Renderer suite also exercises the standalone `mako-launch` contract: deterministic implicit-layer selection, loader activation, LSFG-VK conflict guards, the Gamescope WSI/HDR process-start boundary, advanced environment forwarding, argument quoting, input validation, and child exit-status propagation. The packaged hardware smoke test proves instance/device insertion with `vulkaninfo` and, when a graphical compositor and `vkcube` are available, covers finite swapchain creation and presentation too. Presentation changes must preserve the invariants and expanded matrix in [WSI isolation](engine/docs/WSI-ISOLATION.md) and [HDR pipeline architecture](engine/docs/HDR-PIPELINE.md).

The frontend suite intentionally tests operations where a UI/backend disagreement can damage or misrepresent user state: Renderer installation, configuration persistence, deferred Target FPS writes, profile runtime-session transitions, out-of-order profile loads, profile switching, default-profile protection, persistent section state, and Decky RPC method names. It does not use snapshots or test static labels and layout.

The backend suite characterizes the exact generated wrapper and profile-sidecar bytes at the pure-module/service boundary. These tests ensure refactoring cannot silently change wrapper ordering, safety exports, profile metadata, or the allowlisted Decky-only settings that are merged with Renderer TOML.

The Decky binding freshness gate is `npm --prefix plugin run check:generated-config`. It is read-only and fails when the tracked TypeScript or Python binding differs from `plugin/shared_config.py` and its generators; normal Decky build, watch, and backend-test commands invoke the same check automatically.

The Decky localization gate is `npm --prefix plugin run check:i18n`. It verifies the ordered string-only dictionary contract, language metadata, Steam aliases, named-placeholder parity, static call-site keys and English fallbacks, exact replacement fields, complete template usage, and the tracked `src/i18n/languages.json` bundle without writing files. Intentional dictionary changes regenerate that tracked bundle with `npm --prefix plugin run generate:i18n`; frontend tests exercise language normalization, translated replacement, and safe English/unsupported-language fallbacks.

The Renderer portable suite also verifies the adjacent GLSL source hashes and embedded payload hashes recorded for HDR colour-conversion SPIR-V. Regenerate `engine/mako-backend/src/shaders/color_conversion_spirv.hpp` and its hash manifest with `engine/scripts/generate-color-conversion-spirv.py` and `glslangValidator` when any owned conversion shader changes.

The Decky job also runs the repository Markdown formatting check. Install the tracked local hook once with `just install-hooks`; it formats only staged Markdown through lint-staged, preserves partially staged work, and leaves JS/TS, Python, C++, generated output, and vendored code untouched.

The backend suite treats Armada as an unsupported-host safety boundary: native host detection must survive a translated Decky process, stale x86 files cannot report a successful install, old wrappers must exit before any MAKO exports, and persisted MAKO Flatpak activation must be removed without touching competitor-only settings. These deterministic checks prove fail-closed behavior only. Native Renderer support still requires the real-hardware evidence listed in [Armada and native AArch64 support](plugin/docs/ARMADA.md).

Run the same gates locally with:

```bash
just check-markdown-format
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

The full Renderer suite additionally requires the Vulkan and X11 development headers:

```bash
cmake -S engine -B engine/build/local -DBUILD_TESTING=ON -DMAKO_BUILD_UI=OFF
cmake --build engine/build/local
ctest --test-dir engine/build/local --output-on-failure
```

## SteamOS hardware release gate

Before publishing a release candidate, run the `SteamOS hardware validation` workflow for that commit. On the dedicated SteamOS/AMD test machine, the recommended entry point creates a verified one-job runner, dispatches the selected clean branch, waits for the result, and removes the runner registration, credentials, checkout, staging, generated packages, and local runner logs:

```bash
./scripts/run-steamos-hardware-validation.sh
```

The launcher refuses a dirty or remote-divergent branch, an existing queued hardware run, a non-SteamOS/non-AMD host, `/tmp`-backed runner storage, or less than 30 GiB of free work space. It downloads the latest official Linux x64 Actions runner and verifies the archive against the SHA-256 published in the same official release. The workflow still requires the normal 64-bit and 32-bit Renderer/Flatpak build prerequisites, `vulkaninfo`, and access to the licensed `Lossless.dll` used only for validation.

The workflow:

1. forces both FP32 and FP16 AMD image-quality regressions to run rather than skip;
2. builds and tests the native 64-bit and 32-bit Renderer payloads;
3. builds and installs each supported Flatpak runtime extension for verification;
4. builds the complete Decky ZIP from the same source tree;
5. extracts the packaged Renderer and proves that the real Vulkan loader activates `VK_LAYER_MAKO_render` on the runner's AMD GPU;
6. retains the complete verified Decky ZIP, sanitized environment evidence, and GPU comparison images for 14 days under the tested commit.

Pass `--deploy-to-decky` only on a dedicated device with an existing MAKO Decky development installation. That option safely synchronizes the already-verified ZIP into the existing plugin, asks Decky Loader to reload it, and invokes MAKO Decky's normal installer against that exact bundled Renderer. The production installer owns host libraries, manifests, wrappers, engine state, diagnostics, and refreshes of already-installed Flatpak runtime branches; no component is rebuilt or installed through a second CI-only implementation. It intentionally does not run by default because it changes the installed test device.

The launcher reuses the repository's existing ignored `engine/build/cache` tree for compiler, Flatpak, native SDK, pnpm, and Actions tool caches instead of creating a second large cache under the user's home directory; the location can be changed with `MAKO_HARDWARE_CI_ROOT`. Inspect the retained data without deleting anything with `./scripts/prune-hardware-ci-cache.sh`, or add `--confirm` when the space is more valuable than the next build's warm cache. The workflow itself removes large staging and generated outputs even when a manually configured persistent runner is used.

This gate is a fresh checkout on the real SteamOS/AMD host, not a fresh virtual SteamOS installation. It validates the physical GPU, driver, Vulkan loader, Flatpak, packaging, and optional Decky deployment boundaries. The GitHub-hosted pull-request jobs independently validate a clean portable Ubuntu environment; neither gate replaces the other.

## Runtime compatibility matrix

The hardware workflow establishes packaging, loader activation, and deterministic image-quality boundaries. Game presentation still requires the release-candidate matrix in [Adaptive validation](engine/docs/ADAPTIVE-VALIDATION.md): DXVK and VKD3D-Proton under Gamescope, overlay and focus transitions, hitches, swapchain recreation, and the supported desktop GPU paths. Record unavailable rows as **not tested** rather than treating the automated smoke test as equivalent coverage.

## Versioned game traces

Use `scripts/capture-trace.sh` after a completed game session to archive comparative evidence into a sibling **private** `MAKO-Traces` checkout. Read the [trace extractor guide](TRACES.md) first. Keep the private repository outside the MAKO worktree so licensed paths, large runtime logs, and subjective notes never become product artifacts; it is not required for normal MAKO builds, tests, packaging, installation, or releases.

The public producer contract test is private-data-free and runs in pull-request CI:

```bash
./scripts/test-capture-trace.sh
```

It characterizes safe component normalization and containment, exact offset-aware timestamp ordering, common credential redaction, stable metadata identity fields, canonical checksums, aligned notes headings, validator rollback, concurrent no-clobber publication, and the single-path success output without requiring a MAKO-Traces checkout containing real evidence.

The capture command requires a game name, explicit archive version label, and session start time. Development builds must include their source identity rather than masquerading as a released version. The script stores sanitized diagnostics and configuration, optionally clips Decky and Steam logs to the session window, records the exact MAKO source revision and host identity, generates an event index and notes template, rejects likely credentials, and writes checksums without overwriting an existing capture.

```bash
./scripts/capture-trace.sh \
  --game "Resident Evil 4" \
  --game-id 2050650 \
  --version "2.0.0-dev-f1f6a1c" \
  --label "fixed-adaptive-fifo-pressure" \
  --run-index 1 \
  --session-start "2026-08-20T12:30:52+01:00" \
  --session-end "2026-08-20T12:36:25+01:00" \
  --decky-log "$HOME/homebrew/logs/Mako/<decky-log>.log" \
  --steam-log "$HOME/.steam/steam/logs/console-linux.txt"
```

Close the game first so buffered records are complete, then capture before launching another game because the development wrapper starts each session with a fresh presentation log. The producer invokes the sibling validator when it is available; review the resulting private diff and run `./scripts/check.sh` from MAKO-Traces before committing it (`just check` is the equivalent convenience alias). A stored trace supports repeatable comparison; a single run does not by itself prove a performance or image-quality regression.
