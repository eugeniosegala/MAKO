# Testing MAKO

MAKO uses separate gates for deterministic product behavior and hardware behavior. A passing CPU-only suite is required for every change, but it is not treated as evidence that a Vulkan driver, Gamescope, or a game presents generated frames correctly.

## Pull-request gates

The `Tests` GitHub Actions workflow runs on every pull request and push to `main`:

- **MAKO Decky:** the Python backend suite, focused frontend behavior tests with coverage thresholds, and the production Decky bundle build;
- **MAKO Renderer:** the complete non-hardware CTest suite with both GCC and Clang on Linux;
- **Renderer sanitizers:** the portable scheduling, presentation-policy, profile, transition, and colour-math boundaries under AddressSanitizer and UndefinedBehaviorSanitizer.

The Renderer suite also exercises the standalone `mako-launch` contract: deterministic implicit-layer selection, loader activation, LSFG-VK conflict guards, the Gamescope WSI presentation guard, advanced environment forwarding, argument quoting, input validation, and child exit-status propagation. The packaged hardware smoke test proves instance/device insertion with `vulkaninfo` and, when a graphical compositor and `vkcube` are available, covers finite swapchain creation and presentation too.

The frontend suite intentionally tests operations where a UI/backend disagreement can damage or misrepresent user state: Renderer installation, configuration persistence, out-of-order profile loads, profile switching, default-profile protection, and Decky RPC method names. It does not use snapshots or test static labels and layout.

Run the same gates locally with:

```bash
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
