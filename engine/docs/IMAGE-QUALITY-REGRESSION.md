# Procedural image-quality regression

MAKO includes deterministic procedural scenes and three real-GPU commands for catching corruption, temporal trails, detail loss, and disocclusion errors. Default source images are deliberately odd-sized at 321×181; spatial and combined references are rendered directly at the presentation resolution.

The available scenes are `motion-boundary`, `traffic`, `crowd`, `camera-pan`, and `hud-disocclusion`.

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

Run the direct pre-Frame Generation spatial-to-LSFG handoff:

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
  --output ./mako-combined-result
```

Use `--dll /path/to/Lossless.dll` when automatic discovery is unavailable and `--gpu "GPU name"` on a multi-GPU system. LSFG and combined commands allow FP16 by default when supported by the selected GPU, otherwise they use FP32. Pass `--no-fp16` for an explicit FP32 run; `--allow-fp16` remains available for explicit FP16 permission. Precision comparisons must specify the intended flag for each run and check the backend's `precision=fp16` or `precision=fp32` inspection record; `FP16 allowed` alone does not prove hardware support. `--performance-mode` selects the lighter frame-generation model. Spatial methods are `native`, `mako`, `ls1`, and `ls1-performance`. `--width` and `--height` must be supplied together; they select the exact presentation resolution while the production factor policy derives the source extent.

The spatial command runs the production scaler and fails if an LS1 request falls back to MAKO. The combined command reconstructs each endpoint directly into a presentation-sized LSFG source and scores the generated result against a presentation-resolution reference. It does not exercise the high-resolution post-Frame Generation placement or a WSI swapchain; those remain MAKO Gym boundaries.

## Temporal frame-generation sequences

`quality-regression --sequence-plan` keeps one backend context alive across 12–240 source steps. Separate source steps with semicolons. Each step is `history` or one to four strictly increasing interpolation positions between zero and one, separated by commas. The context allocates the maximum requested output count once; the sequence exercises changing output counts, timestamps, and history without replacing it. `--interpolation` belongs to the ordinary single-pair mode; the sequence plan supplies every temporal interpolation position.

```bash
mako-cli quality-regression --scene traffic --no-fp16 --flow 1.0 \
  --sequence-plan '0.5;0.25,0.75;0.2,0.4,0.6,0.8;history;0.33,0.67;0.5;history;0.2,0.4,0.6,0.8;0.25,0.5,0.75;0.5;0.1,0.9;0.5' \
  --output ./mako-temporal-result
```

The scene advances through a deterministic 24-step forward/reverse cycle. Both source images are initialized before priming history, and each subsequent source image is uploaded after previous GPU users complete. Every generated output is read back, scored against its corresponding reference, and returned to its reusable image layout. This mode measures correctness, not throughput: uploads, waits, scoring, and readbacks are intentional and untimed.

Artifacts include `sources/0.ppm` through the final source step, one `frame-N-output-M/` directory per generated image, and `sequence.tsv` with every output or history-only step. The completion marker identifies the recipe, scene clock, source/output/history counts, and output capacity. `--width` and `--height` together override the default 321×181 temporal source extent, with a maximum of 16 megapixels and 16384 pixels per dimension. These flags remain presentation extents for spatial and combined commands.

MAKO Gym owns the temporal plan, case selection, repeated execution, complete artifact validation, and byte-for-byte repeatability checks. A passing sequence establishes bounded procedural correctness; it does not prove perceptual equivalence to another model or correctness for all game motion.

## Defined benchmark inputs

`mako-cli benchmark` uploads the deterministic full-resolution traffic scene at times zero and one before timing begins. It then repeatedly alternates that endpoint pair through the existing capacity loop. `MAKO_BENCHMARK recipe=2 content=traffic-pair-v1` distinguishes this workload from older benchmarks whose allocated source pixels were not explicitly populated. Old and new throughput baselines must not be compared as a Renderer regression. GPU image upload and procedural scene construction are outside the timed loop; the result remains backend capacity rather than live-game performance.

## Scoring

Each command reports normalized whole-frame error, focus-region error, the fraction of severe focus errors, and fine-detail error. These broad guardrails catch corruption, endpoint duplication, destructive ghosting, and major detail loss; they do not rank visual quality perceptually.

Frame-generation artifacts contain `previous.ppm`, `current.ppm`, `reference.ppm`, and `generated.ppm`. Spatial artifacts contain `source.ppm`, `reference.ppm`, and `generated.ppm`. Combined artifacts contain low-resolution `previous.ppm` and `current.ppm` plus presentation-resolution `reference.ppm` and `generated.ppm`.

## MAKO Gym hardware validation

Portable CTest verifies scene generation, masks, extents, scoring, invalid inputs, and perfect or corrupted references without a GPU or licensed input. The private sibling MAKO Gym repository owns real AMD execution, licensed-input discovery, artifacts, repeatability, scaler timing, and synchronization validation.

After building `mako-cli`, run the MAKO-side bridge from the repository root:

```bash
./engine/scripts/run-mako-gym.sh --suite quality \
  --cli "$PWD/engine/build/mako-cli/mako-cli"
```

Use `--require` when absence of MAKO Gym must fail. Other arguments, including `--dll`, `--gpu`, `--filter`, and `--output-root`, are forwarded to Gym. `MAKO-Gym/docs/AMD-QUALITY-REGRESSION.md` owns the current cases and limitations.

The quality commands report whether optional `robustImageAccess2` support was enabled; an unsupported device keeps the normal path.

The synthetic contracts are part of the full CTest build described in the [root testing guide](../../TESTING.md). From `engine/`:

```bash
cmake -S . -B build/quality-policy -DBUILD_TESTING=ON -DMAKO_BUILD_UI=OFF
cmake --build build/quality-policy --target mako-device-feature-tests mako-image-quality-tests mako-cli
ctest --test-dir build/quality-policy --output-on-failure -R '^(optional-device-features|procedural-image-quality-scene|cli-i18n-contract)$'
```

A skipped GPU test is not image-quality evidence. Offscreen tests also do not prove WSI presentation, compositor scanout, subjective quality, latency, power, HDR, Proton translation, or another GPU and driver.
