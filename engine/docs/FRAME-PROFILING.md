# Frame Generation profiling

Use the opt-in CLI diagnostic mode to separate GPU work from CPU submission and synchronization:

```bash
mako-cli benchmark --dll "/path/to/Lossless.dll" --width 1280 --height 800 \
    --multiplier 2 --flow 1 --no-fp16 --profile \
    --profile-samples 1000 --profile-warmup 5000
```

Choose `--allow-fp16` or `--no-fp16` explicitly and add `--performance-mode` for the lighter model. This mode uses the same defined traffic pair and production backend graph as the capacity benchmark, warms every cached source/history phase, then measures a bounded number of iterations instead of `--duration`. Ordinary capacity runs retain their existing recipe and allocate no timing queries.

Add `--profile-hdr10` to exercise packed RGB10A2 HDR10 exchange and the production linear HDR generation path. This flag requires `--profile`; it cannot change the ordinary SDR capacity recipe. Before timing, the source traffic pair is decoded from sRGB/BT.709, mapped to BT.2020 with 203-nit reference white, encoded as PQ, and quantized to packed 10-bit. This is a repeatable bounded-luminance diagnostic scene, not a game capture or a bright-highlight quality test. Unsupported packed HDR10 devices fail explicitly. MAKO Gym's `renderer_profile.py --color paired` alternates SDR/HDR order across repeated runs, retains the encoding, and checks the stage sums.

The six-iteration minimum warms command recordings; GPU power and clock behavior can take longer to settle. Use a longer warm-up such as the example above, inspect early/late timing drift, and extend it when necessary. A fixed iteration count alone does not establish steady performance.

`MAKO Renderer: benchmark-profile operation=summary` schema 2 identifies the encoding, recipe and Vulkan timestamp properties. `MAKO Renderer: benchmark-profile operation=sample` retains every measured iteration in microseconds, and `MAKO Renderer: benchmark-profile operation=metric` reports minimum, median, p95, maximum and population coefficient of variation. GPU fields cover preprocessing, each generated output, their generation sum and the full preprocessing-to-last-output span including gaps. Preprocessing is split into input colour conversion and motion estimation; generated work is split into synthesis and output colour conversion. SDR conversion fields are exactly zero. These substage values partition their parent spans; adding parents and children double-counts work. CPU fields cover the shared-semaphore signal, scheduling, previous-work fence wait, preprocessing/output submissions, output wait and complete iteration. The previous wait and submission fields are contained in scheduling; CPU output wait overlaps GPU execution, so these timings are not additive. GPU spans include command/barrier costs rather than isolated shader instructions. Extra timestamp boundaries may perturb GPU overlap, so retain ordinary capacity and live game evidence separately.

Query readback is outside the measured CPU iteration and is reported separately as `cpu_profile_read`; it still changes spacing between iterations. Profiling is diagnostic evidence, not interchangeable capacity FPS, game FPS, scanout or input-latency evidence. MAKO Gym owns repeated collection, hardware/identity checks, strict raw-sample validation and sanitized local reports through `scripts/renderer_profile.py`.

Add `--profile-hdr-reduced-precision` with `--profile-hdr10` to exercise the explicit PQ interpolation approximation. The summary reports `encoding=hdr10-pq-approximate`, retaining the same packed input recipe and output format while both conversion spans are zero. Gym’s `--color precision-paired` alternates this against full HDR10 with identical workload settings. Do not infer a game FPS gain or acceptable visual quality from this backend-only timing.
