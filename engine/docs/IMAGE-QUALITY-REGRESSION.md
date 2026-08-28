# Procedural image-quality regression

MAKO includes a deterministic procedural renderer and three real-GPU commands for detecting boundary corruption, temporal trails, detail loss, occlusion, disocclusion, parallax, crowds, traffic, particles, and HUD errors. Every source scene is 321×181 so its edges do not align with common compute workgroups. Spatial and combined references are rendered directly at presentation resolution.

The five scenes are `motion-boundary`, `traffic`, `crowd`, `camera-pan`, and `hud-disocclusion`. The original AMD motion-boundary regression remains available through its compatibility entry point and retains its original threshold.

## Real-GPU commands

Run one LSFG interpolation:

```bash
mako-cli quality-regression \
  --scene traffic \
  --interpolation 0.5 \
  --flow 1.0 \
  --output ./mako-quality-result
```

Run one production spatial scaler offscreen:

```bash
mako-cli spatial-quality-regression \
  --scene crowd \
  --method ls1 \
  --factor 1.5 \
  --width 2560 \
  --height 1440 \
  --sharpness 0.5 \
  --scene-time 0.5 \
  --output ./mako-spatial-result
```

Run the production spatial-to-LSFG handoff:

```bash
mako-cli combined-quality-regression \
  --scene camera-pan \
  --method ls1-performance \
  --factor 1.5 \
  --width 3840 \
  --height 2160 \
  --sharpness 0.5 \
  --interpolation 0.67 \
  --flow 0.75 \
  --performance-mode \
  --allow-fp16 \
  --output ./mako-combined-result
```

Use `--dll /path/to/Lossless.dll` when automatic discovery is unavailable and `--gpu "GPU name"` on a multi-GPU system. `--allow-fp16` permits LSFG acceleration; `--performance-mode` selects the lighter frame-generation model. Spatial method names are `mako`, `ls1`, and `ls1-performance`. Spatial and combined commands accept `--width` and `--height` only as a pair; these select the exact presentation resolution while the production factor policy derives the source extent. Omitting them retains the deliberately odd-sized 321×181 portable regression scene.

The spatial command invokes the production `mako-render/src/spatial_scaler.cpp` graph rather than a test implementation. It fails if an LS1 request falls back to MAKO. The combined command scales both low-resolution temporal endpoints into the exported full-resolution source images used by MAKO Renderer, synchronizes them through the shared timeline semaphore, runs the real licensed backend, and scores the final generated frame against a presentation-resolution ideal.

## Scoring

Each command reports normalized whole-frame absolute error, motion/disocclusion focus error, severe focus-pixel fraction, and explicitly marked fine-detail error. Scene-aware focus thresholds preserve the original motion-boundary guardrail while accounting for the deliberately larger disocclusions in the traffic and HUD scenes. Whole-frame, severe-error, and detail limits stay common. The broad guardrails detect corruption, endpoint duplication, destructive ghosting, and catastrophic detail loss; they are not a perceptual ranking of algorithms.

Frame-generation artifacts contain `previous.ppm`, `current.ppm`, `reference.ppm`, and `generated.ppm`. Spatial artifacts contain `source.ppm`, `reference.ppm`, and `generated.ppm`. Combined artifacts contain low-resolution `previous.ppm` and `current.ppm` plus presentation-resolution `reference.ppm` and `generated.ppm`.

## MAKO Gym hardware validation

Portable CTest owns deterministic scene generation, masks, extents, scoring policy, invalid-input behavior, and perfect/corrupted reference checks without a GPU or licensed input. The private sibling MAKO Gym repository owns the evolving AMD visual inventory, licensed-input discovery, parameter and PPM assertions, sanitization, retained summaries, repeatability sentinels, exact-resolution scaler timing, and synchronization validation. Its documentation is authoritative for current case counts and coverage.

After building `mako-cli`, run the MAKO-side bridge from the repository root:

```bash
./engine/scripts/run-mako-gym.sh --suite quality \
  --cli "$PWD/engine/build/mako-cli/mako-cli"
```

Use `--require` in automation that must not skip when Gym is absent. Forward `--dll /custom/path/Lossless.dll`, `--gpu "exact device name"`, `--filter <regex>`, or `--output-root <path>` to Gym as needed. The authoritative inventory, output shape, and limitations are documented in `MAKO-Gym/docs/AMD-QUALITY-REGRESSION.md`.

At startup, the Renderer quality commands report whether `robustImageAccess2` is enabled. MAKO requests this optional Vulkan feature only when the selected device advertises both `VK_EXT_robustness2` and its `robustImageAccess2` feature bit. Unsupported devices retain the existing path.

The synthetic contracts are part of the full CTest build described in the [root testing guide](../../TESTING.md). From `engine/`:

```bash
cmake -S . -B build/quality-policy -DBUILD_TESTING=ON -DMAKO_BUILD_UI=OFF
cmake --build build/quality-policy --target mako-device-feature-tests mako-image-quality-tests mako-cli
ctest --test-dir build/quality-policy --output-on-failure -R '^(optional-device-features|procedural-image-quality-scene|cli-i18n-contract)$'
```

A skipped GPU test is not image-quality evidence. MAKO Gym's offscreen matrix also does not prove WSI presentation, compositor scanout, subjective quality, latency, power, live mutation, another driver or GPU, HDR, DXVK, or VKD3D-Proton; use the corresponding live Vulkan, compositor, Proton, and real-game lanes.
