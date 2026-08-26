# AMD Image-Quality Regression

MAKO includes a deterministic GPU regression for image-boundary corruption, motion trails, small-detail loss, occlusion, and disocclusion. The scene is 321×181 pixels so its right and bottom edges do not align with common compute workgroup sizes.

The test always uses the **Flow Scale 1.0 quality preset**:

- full-resolution optical flow (`flow_scale = 1.0`);
- 2× frame generation with an explicit midpoint timestamp;
- performance mode disabled.

Run it on the target AMD system with a built `mako-cli`:

```bash
mako-cli quality-regression --output ./mako-quality-result
```

Use `--dll /path/to/Lossless.dll` when automatic discovery is not available, and `--gpu "GPU name"` on a multi-GPU system. Add `--allow-fp16` for a second pass that matches MAKO Decky's normal AMD acceleration setting.

## MAKO-Gym hardware validation

Portable CTest owns the deterministic scene and scoring policy without a GPU or licensed input. The private sibling MAKO-Gym repository owns real AMD orchestration, DLL discovery, mandatory FP32/FP16 execution, sanitized logs, and comparison-image retention. This keeps normal CMake builds and packaging independent from licensed local inputs while preserving fail-closed hardware evidence in the SteamOS release gate.

After building `mako-cli`, run the MAKO-side bridge from the repository root:

```bash
./engine/scripts/run-mako-gym.sh --suite quality \
  --cli "$PWD/engine/build/mako-cli/mako-cli"
```

Use `--require` in automation that must not skip when Gym is absent. Forward `--dll /custom/path/Lossless.dll`, `--gpu "exact device name"`, or `--output-root <path>` to Gym as needed. The authoritative orchestration, evidence layout, and limitations are documented in `MAKO-Gym/docs/AMD-QUALITY-REGRESSION.md`.

The command exits successfully only when all broad corruption guardrails pass. It reports normalized whole-frame, motion/disocclusion, severe-error, and thin-detail metrics. The output directory contains `previous.ppm`, `current.ppm`, `reference.ppm`, and `generated.ppm` for visual comparison. These guardrails detect regressions; they are intentionally not a claim that a particular metric predicts subjective quality in every game.

At startup, the renderer also reports whether `robustImageAccess2` is enabled. MAKO requests this optional Vulkan feature only when the selected device advertises both `VK_EXT_robustness2` and the `robustImageAccess2` feature bit. Unsupported devices retain the existing path. Robust buffer access and null descriptors are not requested.

The scene generator, scoring rules, feature-selection policy, and preset shape are covered by the full CTest build described in the [root testing guide](../../TESTING.md). From the `engine/` directory:

```bash
cmake -S . -B build/quality-policy -DBUILD_TESTING=ON -DMAKO_BUILD_UI=OFF
cmake --build build/quality-policy --target mako-device-feature-tests mako-image-quality-tests
ctest --test-dir build/quality-policy --output-on-failure -R '^(optional-device-features|amd-image-quality-scene)$'
```

The synthetic tests do not require a GPU or the proprietary shader DLL. MAKO-Gym's hardware suite is separately mandatory in release CI and always fails rather than skips when invoked without its AMD GPU, DLL, CLI, or successful FP32/FP16 results.
