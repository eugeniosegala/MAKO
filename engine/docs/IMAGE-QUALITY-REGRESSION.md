# Procedural image-quality regression

MAKO includes a deterministic procedural renderer and three real-GPU quality commands for image-boundary corruption, temporal trails, fine-detail loss, occlusion, disocclusion, parallax, crowds, traffic, particles, and static HUD composition. Every scene is 321×181 pixels at source so its right and bottom edges do not align with common compute workgroup sizes. Spatial and combined references are vector-rendered directly at presentation resolution.

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
  --sharpness 0.5 \
  --interpolation 0.67 \
  --flow 0.75 \
  --performance-mode \
  --allow-fp16 \
  --output ./mako-combined-result
```

Use `--dll /path/to/Lossless.dll` when automatic discovery is unavailable and `--gpu "GPU name"` on a multi-GPU system. `--allow-fp16` permits LSFG acceleration; `--performance-mode` selects the lighter frame-generation model. Spatial method names are `mako`, `ls1`, and `ls1-performance`.

The spatial command invokes the production `mako-render/src/spatial_scaler.cpp` graph rather than a test implementation. It fails if an LS1 request falls back to MAKO. The combined command scales both low-resolution temporal endpoints into the exported full-resolution source images used by MAKO Renderer, synchronizes them through the shared timeline semaphore, runs the real licensed backend, and scores the final generated frame against a presentation-resolution ideal.

## Scoring

Each command reports normalized whole-frame absolute error, motion/disocclusion focus error, severe focus-pixel fraction, and explicitly marked fine-detail error. Scene-aware focus thresholds preserve the original motion-boundary guardrail while accounting for the deliberately larger disocclusions in the traffic and HUD scenes. Whole-frame, severe-error, and detail limits stay common. The broad guardrails detect corruption, endpoint duplication, destructive ghosting, and catastrophic detail loss; they are not a perceptual ranking of algorithms.

Frame-generation artifacts contain `previous.ppm`, `current.ppm`, `reference.ppm`, and `generated.ppm`. Spatial artifacts contain `source.ppm`, `reference.ppm`, and `generated.ppm`. Combined artifacts contain low-resolution `previous.ppm` and `current.ppm` plus presentation-resolution `reference.ppm` and `generated.ppm`.

## MAKO-Gym hardware validation

Portable CTest owns deterministic scene generation, masks, extents, scoring policy, invalid-input behavior, and perfect/corrupted reference checks without a GPU or licensed input. The private sibling MAKO-Gym repository owns the declarative 66-case AMD visual matrix, DLL discovery, parameter assertions, mandatory execution, PPM evidence validation, sanitization, and retained summaries. Its 18 LSFG cases cover every scene in FP32/FP16 plus Flow Scale, timestamp, and model variations. Its 28 spatial cases cover every scene through every method, scaling factors, MAKO sharpness edges, and all five LS1 variants. Its 20 combined cases cover every scene and spatial method plus representative FP16, lighter-model, lower-flow, factor, sharpness, and non-midpoint interactions.

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

A skipped GPU test is not image-quality evidence. MAKO-Gym's offscreen visual matrix is also not proof of WSI presentation, compositor scanout, subjective game quality, latency, performance, live mutation, another driver or GPU, HDR, DXVK, or VKD3D-Proton. The 47-case live Vulkan matrix and real-game/runtime matrix retain those separate boundaries.
