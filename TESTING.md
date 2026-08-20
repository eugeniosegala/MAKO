# Testing MAKO

MAKO uses separate gates for deterministic product behavior and hardware behavior. A passing CPU-only suite is required for every change, but it is not treated as evidence that a Vulkan driver, Gamescope, or a game presents generated frames correctly.

## Pull-request gates

The `Tests` GitHub Actions workflow runs on every pull request and push to `main`:

- **MAKO Decky:** the Python backend suite, focused frontend behavior tests with coverage thresholds, and the production Decky bundle build;
- **MAKO Renderer:** the complete non-hardware CTest suite with both GCC and Clang on Linux;
- **Renderer sanitizers:** the portable scheduling, presentation-policy, profile, transition, and colour-math boundaries under AddressSanitizer and UndefinedBehaviorSanitizer.

The Renderer suite also exercises the standalone `mako-launch` contract: deterministic implicit-layer selection, loader activation, LSFG-VK conflict guards, the Gamescope WSI/HDR process-start boundary, advanced environment forwarding, argument quoting, input validation, and child exit-status propagation. The packaged hardware smoke test proves instance/device insertion with `vulkaninfo` and, when a graphical compositor and `vkcube` are available, covers finite swapchain creation and presentation too. Presentation changes must preserve the invariants and expanded matrix in [WSI isolation](engine/docs/WSI-ISOLATION.md) and [HDR pipeline architecture](engine/docs/HDR-PIPELINE.md).

The frontend suite intentionally tests operations where a UI/backend disagreement can damage or misrepresent user state: Renderer installation, configuration persistence, out-of-order profile loads, profile switching, default-profile protection, and Decky RPC method names. It does not use snapshots or test static labels and layout.

The Decky job also runs the repository Markdown formatting check. Install the tracked local hook once with `just install-hooks`; it formats only staged Markdown through lint-staged, preserves partially staged work, and leaves JS/TS, Python, C++, generated output, and vendored code untouched.

The backend suite treats Armada as an unsupported-host safety boundary: native host detection must survive a translated Decky process, stale x86 files cannot report a successful install, old wrappers must exit before any MAKO exports, and persisted MAKO Flatpak activation must be removed without touching competitor-only settings. These deterministic checks prove fail-closed behavior only. Native Renderer support still requires the real-hardware evidence listed in [Armada and native AArch64 support](plugin/docs/ARMADA.md).

Run the same gates locally with:

```bash
just check-markdown-format
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

Before publishing a release candidate, run the `SteamOS hardware validation` workflow for that commit. Its dedicated self-hosted runner must have the labels `steamos` and `amd-gpu`, the normal 64-bit and 32-bit Renderer/Flatpak build prerequisites, `vulkaninfo`, and access to the licensed `Lossless.dll` used for validation.

The workflow:

1. forces both FP32 and FP16 AMD image-quality regressions to run rather than skip;
2. builds and tests the native 64-bit and 32-bit Renderer payloads;
3. builds and installs each supported Flatpak runtime extension for verification;
4. builds the complete Decky ZIP from the same source tree;
5. extracts the packaged Renderer and proves that the real Vulkan loader activates `VK_LAYER_MAKO_render` on the runner's AMD GPU;
6. retains packages, environment evidence, GPU comparison images, and logs under the tested commit.

Set the workflow's `deploy_to_decky` input only on a dedicated device with an existing MAKO Decky development installation. That option deploys both host architectures and all Flatpak bundles, then asks Decky Loader to reload the plugin. It intentionally does not run by default because it changes the installed test device.

## Runtime compatibility matrix

The hardware workflow establishes packaging, loader activation, and deterministic image-quality boundaries. Game presentation still requires the release-candidate matrix in [Adaptive validation](engine/docs/ADAPTIVE-VALIDATION.md): DXVK and VKD3D-Proton under Gamescope, overlay and focus transitions, hitches, swapchain recreation, and the supported desktop GPU paths. Record unavailable rows as **not tested** rather than treating the automated smoke test as equivalent coverage.

## Versioned game traces

Use `scripts/capture-trace.sh` after a completed game session to archive comparative evidence into a sibling **private** `MAKO-Traces` checkout. Read the [trace extractor guide](TRACES.md) first. Keep the private repository outside the MAKO worktree so licensed paths, large runtime logs, and subjective notes never become product artifacts; it is not required for normal MAKO builds, tests, packaging, installation, or releases.

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

Close the game first so buffered records are complete, then capture before launching another game because the development wrapper starts each session with a fresh presentation log. Validate the resulting archive with `../MAKO-Traces/scripts/validate.sh`. A stored trace supports repeatable comparison; a single run does not by itself prove a performance or image-quality regression.
