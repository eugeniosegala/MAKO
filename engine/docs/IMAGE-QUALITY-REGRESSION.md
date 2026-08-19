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

## Automatic Steam Machine validation

CTest registers `amd-image-quality-gpu` automatically. On a detected SteamOS or Steam Machine host, the default `AUTO` policy becomes mandatory: both FP32 and FP16 passes must run, and a missing AMD Vulkan GPU, `Lossless.dll`, or other prerequisite fails the build. Macs and other unsupported development hosts report the hardware test as skipped, so the portable test suite still runs there.

Comparison artifacts are retained under the build's `quality-regression/fp32` and `quality-regression/fp16` directories. Local package builds keep them in `build/cache/quality-regression` so a failed packaging workspace cannot discard the evidence.

The fast SteamOS development builder also runs both passes after every successful 64-bit layer build:

```bash
./scripts/build-steamos-dev.sh
```

Use `--skip-quality-regression` only for an intentional short iteration. Set `MAKO_QUALITY_DLL=/custom/path/Lossless.dll` when Lossless Scaling is installed outside a standard Steam library, and set `MAKO_QUALITY_GPU="exact device name"` to choose a particular AMD GPU. If the development hardware uses a non-SteamOS distribution whose identity cannot be detected automatically, set `MAKO_STEAM_MACHINE=1` once in its development environment to apply the same mandatory policy.

Recognized Steam Machine hosts already enforce the regression without extra configuration. Release CI can still set `REQUIRED` explicitly to document that hardware contract; the environment form also applies to `build-steamos-dev.sh`, `test-adaptive-scheduler.sh`, and `package-local.sh`:

```bash
cmake -S . -B build -DMAKO_GPU_QUALITY_TEST=REQUIRED
cmake --build build
ctest --test-dir build --output-on-failure
```

```bash
MAKO_GPU_QUALITY_TEST=REQUIRED ./scripts/package-local.sh
```

The command exits successfully only when all broad corruption guardrails pass. It reports normalized whole-frame, motion/disocclusion, severe-error, and thin-detail metrics. The output directory contains `previous.ppm`, `current.ppm`, `reference.ppm`, and `generated.ppm` for visual comparison. These guardrails detect regressions; they are intentionally not a claim that a particular metric predicts subjective quality in every game.

At startup, the renderer also reports whether `robustImageAccess2` is enabled. MAKO requests this optional Vulkan feature only when the selected device advertises both `VK_EXT_robustness2` and the `robustImageAccess2` feature bit. Unsupported devices retain the existing path. Robust buffer access and null descriptors are not requested.

The scene generator, scoring rules, feature-selection policy, and preset shape are covered by CTest:

```bash
./scripts/test-adaptive-scheduler.sh
```

The synthetic tests do not require a GPU or the proprietary shader DLL. The hardware integration test is mandatory by default on recognized Steam Machine development and packaging builds, while `REQUIRED` records the same policy explicitly for release CI.
