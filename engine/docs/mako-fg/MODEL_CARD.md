<p align="center"><img src="mako-fg-banner.webp" width="100%" alt="MAKO FG: video frame interpolation model" /></p>

# MAKO FG V1.24

**Model card · Version V1.24 (1.24) · Released 2026-11-12**

**Free, non-commercial research release.** Part of the training data is licensed for research and education or non-commercial use only: X4K1000FPS (X-TRAIN, reviewed subset). This model is released free of charge by a non-commercial research project and may not be used commercially.

## Model details

- **Developer:** Eugenio Segala
- **Model type:** Real-time video frame interpolation (frame generation) for games
- **Size:** 211 learned parameters (under 1 KB of weights)
- **Inputs:** Two consecutive real frames on the GPU, SDR (8-bit or high precision) or HDR (HDR10 or scRGB), and the requested times between them
- **Outputs:** One generated frame per requested time, at the input resolution (three for 4x, one for 2x)
- **Precision:** full precision (FP32); half-precision storage with full-precision arithmetic
- **Distribution:** Open weights from a non-commercial research project: free for non-commercial use; the source code is not published
- **Licence:** [Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)](https://creativecommons.org/licenses/by-nc/4.0/)

## Model description

MAKO FG is a state-of-the-art, very fast, memory-efficient, open-weight frame-generation (video frame interpolation) model for games on SteamOS and Linux, developed for [**MAKO**](https://github.com/eugeniosegala/MAKO), the free, open-source graphics layer for frame generation, scaling and shaders. It generates the frames between two consecutive real game frames on the GPU. **3x output is especially strong in this model:** two generated frames share the motion work for each pair of real frames, turning 30 FPS into 90 FPS or 40 FPS into 120 FPS when the game and GPU sustain that rate. It also supports 2x, 4x and other requested output schedules.

Flow Scale controls motion-analysis resolution, and MAKO FG Mode controls how often motion is solved at 2x and 3x. Together they let players balance motion detail and GPU cost for each game. Reusing motion saves GPU time but can add artifacts when motion changes quickly; static HUD text and crosshairs are preserved by a separate screen-overlay stage.

The model has 211 learned parameters, under 1 KB of weights. V1.24 timings below cover all four MAKO FG modes at 1440p. The runtime reuses temporary GPU buffers and releases context memory when resources are replaced; working memory depends on resolution and generated-frame capacity, not just weight size.

MAKO FG is a free, non-commercial research project. Its training used motion supervision derived from the open-source Practical-RIFE 4.25 lite model, as described in the Training section below. No proprietary or commercial models, or their outputs, were used to train it. Its weights and GPU shaders are available under the non-commercial licence below; the model, runtime and training source code are not published.

## Intended use

Generating intermediate frames for games running through MAKO, with Fixed (for example 2x and 4x) or Adaptive frame generation, for smoother motion on high-refresh displays. Other non-commercial software can run the same model standalone through the prebuilt runtime library.

### Target devices

Steam Deck and similar PC handhelds, Steam Machine and desktop GPUs, on SteamOS and Linux with a Vulkan GPU. The main use is 30 to 120 FPS (4x) on handhelds and Steam Machine; 60 to 120 FPS (2x) is a separate target with fewer artifacts. This release was measured on a Steam Machine-class AMD GPU; the other devices are design targets until they are measured.

### Out of scope

Commercial use; producing or altering video as evidence; medical or safety-critical imaging.

## How to use

### In MAKO

1. Install [MAKO](https://github.com/eugeniosegala/MAKO) on SteamOS or Linux. MAKO packages include MAKO FG; there is nothing else to download.
2. MAKO FG is the default model. To select it, open the game's profile in MAKO Decky or the MAKO app and set the Frame Generation Model to MAKO FG.
3. Choose Fixed frame generation (for example 2x or 4x) or Adaptive frame generation; the model generates any requested times between two real frames. SDR and HDR games are both supported.
4. Restart the game. MAKO's log names the loaded release in its `fg-model selected=` line.

### Outside MAKO

MAKO FG also runs standalone. Download the model and the prebuilt runtime from [Hugging Face](https://huggingface.co/eugeniosegala/MAKO-FG); other software runs it through the runtime library in `runtime/`. The files in `model/` need it: the motion search, the HUD and thin-object stages and the order of all stages live in the library. The licence also allows your own non-commercial runtime, but the model's file formats are not documented or supported.

**Requirements**

- Linux on 64-bit x86 (`runtime/x64`) or 32-bit x86 (`runtime/x86`), as new as Ubuntu 22.04 or newer (glibc 2.35, GCC 11), and a C++20 compiler.
- A Vulkan 1.1 GPU and driver with push descriptors (`VK_KHR_push_descriptor`), timeline semaphores, and external memory and semaphores as opaque file descriptors (`VK_KHR_external_memory_fd`, `VK_KHR_external_semaphore_fd`). The library loads Vulkan at run time and creates its own device on the GPU you choose; frames are shared with it as exported Vulkan images.

**Steps**

1. Build with the headers and the library, for example `g++ -std=c++20 -Iruntime/x64/include app.cpp runtime/x64/lib/libmako-fg-runtime.a -lpthread -ldl`, and include `external.hpp`.
2. Load the model with `mfg::readPackageDirectory("model")`, or with `mfg::packageFromFiles(weights, ops, transport, mixture)` when its files are already in memory.
3. Choose a GPU from `mfg::devices()`. On that GPU, in your own Vulkan code, create two source images and one output image per generated frame, at the frame size, with optimal tiling, storage and sampled usage and dedicated memory, in `VK_FORMAT_R8G8B8A8_UNORM` for 8-bit SDR (`mfg::supportsEncoding` and `external.hpp` list the HDR formats), and a timeline semaphore. Export each as an opaque file descriptor.
4. Create `mfg::ExternalContext context(model, device_index, half_storage, width, height, source_a, source_b, outputs, timeline, flow_scale, mfg::Encoding::Sdr8)`. It takes ownership of the file descriptors. `half_storage = true` selects half-precision storage, `flow_scale` (0.25 to 1) the motion-analysis resolution.
5. Keep a timeline value `v`, starting at 1. For each real frame: once the previous frame's generated frames are finished (`context.ready()`), write the frame into source A on even frames and source B on odd frames, signal the timeline to `v` after the write, and call `context.schedule(times, interval)` with the generated times in increasing order between 0 and 1, for example `{0.5f}` for 2x or `{0.25f, 0.5f, 0.75f}` for 4x, and the seconds since the previous real frame (0 when unknown). Frames that generate nothing call `context.schedule({}, interval)`. Generated frame `i` (counting from 1) is in its output image when the timeline reaches `v + i`; then add `times.size() + 1` to `v`. The first frame only primes the model.

**Motion-estimation frequency**

After creating the context, call `setMotionPeriod(period)` before scheduling frames to select the same motion-estimation frequency as MAKO FG Mode. For example, to match MAKO's default Balanced preset:

```cpp
context.setMotionPeriod(2);
```

| MAKO FG Mode | Standalone call | Motion estimation at 2x and 3x |
| --- | --- | --- |
| Quality | `context.setMotionPeriod(1)` | Every real-frame pair |
| Balanced (MAKO default) | `context.setMotionPeriod(2)` | One pair in two; the other corrected |
| Performance | `context.setMotionPeriod(3)` | One pair in three; the others corrected |
| Extra Performance | `context.setMotionPeriod(4)` | One pair in four; the others moved forward while frames arrive evenly and corrected otherwise |

The standalone default is `1` (Quality). Change the period between completed batches without recreating the context; `flow_scale` independently controls analysis resolution. Between full solves, the runtime reuses motion and the HUD map but reconstructs every output from the current two real frames. Correction refines reused motion using the new frame (about 60% of a full solve); uncorrected reuse costs about 25% and assumes steady motion. Fast reversals can still show artifacts outside Quality.

Pass real-frame intervals in seconds to `schedule(times, interval)`. Across four consecutive intervals, the runtime allows uncorrected reuse within 1.1× variation, correction up to 1.5×, and a full solve beyond that. An interval of `0` assumes even spacing. Scene cuts force the next pair to solve; 4× and higher always solve.

`external.hpp` documents the full contract. Use outside MAKO must follow the licence below, including its non-commercial condition and attribution.

## Architecture

A compact model: one small learned stage surrounded by fixed, non-learned algorithms.

- **Motion.** A coarse-to-fine search finds how each part of the picture moved between the two real frames. It starts at very low resolution to catch fast camera turns, compares local contrast so dark scenes track well, and at each step keeps the best of a few candidate motions. While frames arrive in sequence it also continues from the previous frame pair's motion, and where the camera is moving it lets the camera's overall motion settle near-ties on detailed surfaces, which keeps repeating patterns such as stair treads, railings and floor grooves from tearing. Scenery that only one real frame shows, near the frame edges, is taken from that frame.
- **Blending.** A small learned stage blends motion-compensated pixels from the two real frames into each generated frame.
- **HUD.** A fixed stage keeps screen overlays that stay put, such as HUD text, crosshairs, health bars and subtitles, exactly as in the real frames, so they do not drag or double.
- **Thin objects.** At 4x and above, a fixed stage places thin moving objects such as blades, poles and wires at each generated time and removes their ghosts.

Motion is computed once per solved pair of real frames and shared by every frame generated between them. At 2x and 3x, MAKO FG Mode sets how many pairs solve: every pair, or one in two (the default in MAKO), three or four. Pairs in between move the last motion forward along itself and keep the last HUD map, which frees GPU time and can add artifacts in fast changes of direction; a scene cut makes the next pair solve. Every generated pixel is a blend of pixels from the two real frames: there is no generative image prior, so the model cannot reproduce images from its training data.

## Training

The learned blending stage was trained on real high-frame-rate video: given two real frames, it predicts a real frame recorded between them and is scored against that recorded frame. During the final training stage, the motion it blended along came from a separate learned network, distilled from motion computed by the open-source RIFE model (Practical-RIFE 4.25 lite) on the training footage; that network learned from teacher motion only, never from target frames, and is not part of this release. Training also used composites of moving layers cut from the same footage, and static overlays drawn by our own code (text-like marks, crosshairs, meters, panels and icons) to teach HUD handling. The shipped motion, HUD and thin-object stages use fixed, non-learned algorithms.

### Training data

| Name | Used for | Terms | Release rights |
| --- | --- | --- | --- |
| [REDS (120 FPS originals, training split)](https://seungjunnah.github.io/Datasets/reds.html) | Real intermediate-frame targets; footage for teacher motion labels | CC BY 4.0 | cleared |
| [X4K1000FPS (X-TRAIN, reviewed subset)](https://github.com/JihyongOh/XVFI#x4k1000fps) | Real intermediate-frame targets; footage for teacher motion labels | Official licence: the datasets can be freely used for research and education only, and any commercial use needs formal permission first. Its authors have permitted the open release of these weights under a strict non-commercial licence, provided their non-commercial research and educational use only condition passes on to every user; this release does so. Commercial use is not permitted | non-commercial |

### Teacher models

| Name | Used for | Terms | Release rights |
| --- | --- | --- | --- |
| [Practical-RIFE 4.25 lite](https://github.com/hzwer/Practical-RIFE) | Motion labels for the coarse-motion network used during the blending stage's final training, computed on the training footage above; that network is not part of this release | MIT licence; the official README extends it to the released model files. The checkpoint's own training data is not documented by its authors | cleared |

### Training procedure

PyTorch on a single NVIDIA L4 GPU. Blending stage: a final 8,000 updates on 256-pixel crops at batch 8 in mixed precision (about 40 minutes), with half of the natural samples carrying static overlays, after the earlier development stages. The coarse-motion network used during that final training stage was an earlier RIFE-distilled model trained for 8,000 steps. A later coarse-motion network trained for 40,000 steps was substituted after the blending stage had been trained. The current release uses fixed motion algorithms and includes neither coarse-motion network.

### Not used for training

No video game footage or game assets; no code, weights, shaders, binaries or generated outputs from proprietary or commercial models; no frames captured from users or players; no web-scraped video. Game scenes and gameplay recordings appear only in evaluation.

## Evaluation

V1.24 timings use its released model and runtime on one AMD Steam Machine (RADV NAVI33, Mesa 26.2). Each setting has three runs of 500 measured pairs after 1,000 warmups, with fixed SDR inputs, half storage and FP32 arithmetic. GPU rows report mean time per real-frame pair, including all generated frames and excluding game rendering. This is diagnostic profiling with unknown intervals treated as even cadence, not a live-game benchmark. CPU time covers signalling, preparation and submission on the calling thread.

Image-quality scores use full motion estimation (Quality mode); separate scores for Balanced, Performance and Extra Performance are not available. PSNR compares generated frames with held-out real frames at matching timestamps (higher is better). Recordings run sequentially; other suites use isolated pairs after a still frame. Commercial comparisons used licensed software for measurement only.

| Benchmark | Setting | Metric | Result | Notes |
| --- | --- | --- | --- | --- |
| Game scenes captured at high frame rate (4 clips, 240 held-out frames) | 2x | PSNR | 43.46 dB | Camera pans, fast motion and HUD |
| Game scenes captured at high frame rate (4 clips, 360 held-out frames) | 4x | PSNR | 41.70 dB | Three generated frames per real frame |
| Recordings of three third-person games (14 clips, 1704 frames) | 2x, each game's Flow Scale (0.8 or 1.0) | PSNR | 37.46 dB | Running, walking, climbing stairs and fast camera turns, recorded in MAKO at 2560x1440 and 60 to 80 FPS. Each real frame is generated from its neighbours, twice the motion of 60 to 120 FPS play |
| Recordings of a third-person game on a curved staircase (2 clips, 290 frames) | 2x, Flow Scale 0.9 | PSNR | 27.04 dB | Repeating treads and railings while running downstairs; a few frames still tear |
| Recordings of a first-person shooter (3 clips, 411 frames) | 2x, Flow Scale 0.8 | PSNR | 30.90 dB | Aiming, turning and using a scope; one menu frame right after a scroll shows torn text |
| WorldCam gameplay recordings (held-out collection) | Between 30 FPS frames | PSNR | 25.06 dB | Compressed 30 FPS recordings with large motion |
| Game-like rendered 3D scenes (12 scenes, 246 cases) | 2x, Flow Scale 0.8 | PSNR | 28.55 dB | Shooters with aiming HUDs, sword fights, racing, strategy, platforming, weather and menus over live scenes; 29.09 dB at 4x. HUD pixels are off by 6.0 grey levels (0 to 255) on average; a thin aiming reticle over moving scenery, the hardest HUD, by 17.5 |
| Game scenes through a real-time rendering pipeline (13 scenes, 26 cases at 2560x1440) | 2x, Flow Scale 0.8 | PSNR | 27.38 dB | Temporal anti-aliasing, upscaling, motion blur, bloom, sharpening and grain, as in real-time games; 28.10 dB at 4x. The hardest cases are very fast camera turns, with blurred reflections, glows and close scenery entering at the frame edge |
| Camera orbits around a character in rendered 3D scenes (3 scenes, 75 cases) | 2x, Flow Scale 0.8 | PSNR | 30.19 dB | A staircase with railings, a grass hillside and a furnished room, turning 2 to 14 degrees per real frame; 24.95 dB at the fastest turn and 30.70 dB at 4x |
| Third-person camera turns (procedural suite, 5 scenes) | 2x | PSNR | 31.74 dB | A character at the screen centre while the world pans 16 to 80 pixels per real frame; 32.45 dB at 4x |
| Thin moving objects (procedural suite, 8 scenes) | 2x | PSNR | 38.04 dB | Swinging blade, counter-panning pole, wire, turning handle and fast fence; 38.29 dB at 4x with the thin-object stage |
| Complete GPU time per real-frame pair | 1280x720, 2x, Quality, Flow Scale 1.0 | Mean | 0.55 ms | -- |
| Complete GPU time per real-frame pair | 1280x720, 4x, Quality, Flow Scale 1.0 | Mean | 0.95 ms | -- |
| Complete GPU time per real-frame pair | 1280x800, 2x, Quality, Flow Scale 1.0 | Mean | 0.58 ms | -- |
| Complete GPU time per real-frame pair | 1920x1080, 2x, Quality, Flow Scale 1.0 | Mean | 0.97 ms | -- |
| Complete GPU time per real-frame pair | 2560x1440, 2x, Quality, Flow Scale 1.0 | Mean | 1.61 ms | -- |
| Complete GPU time per real-frame pair | 2560x1440, 4x, Quality, Flow Scale 1.0 | Mean | 3.15 ms | -- |
| Complete GPU time per real-frame pair | 2560x1440, 2x, Quality, Flow Scale 0.8 | Mean | 1.35 ms | -- |
| Complete GPU time per real-frame pair | 2560x1440, 2x, Balanced, Flow Scale 0.8 | Mean | 1.16 ms | -- |
| Complete GPU time per real-frame pair | 2560x1440, 2x, Performance, Flow Scale 0.8 | Mean | 1.08 ms | -- |
| Complete GPU time per real-frame pair | 2560x1440, 2x, Extra Performance, Flow Scale 0.8 | Mean | 0.78 ms | -- |
| Complete GPU time per real-frame pair | 2560x1440, 3x, Quality, Flow Scale 0.8 | Mean | 1.93 ms | -- |
| Complete GPU time per real-frame pair | 2560x1440, 3x, Balanced, Flow Scale 0.8 | Mean | 1.73 ms | -- |
| Complete GPU time per real-frame pair | 2560x1440, 3x, Performance, Flow Scale 0.8 | Mean | 1.65 ms | -- |
| Complete GPU time per real-frame pair | 2560x1440, 3x, Extra Performance, Flow Scale 0.8 | Mean | 1.35 ms | -- |
| CPU time to prepare and submit a real-frame pair | 1280x800, 2x, Quality, Flow Scale 1.0 | Median | 0.046 ms | Calling thread, including signal and MAKO FG hand-off; excludes output wait and profile readback |
| GPU output versus independent reference implementation | Six test cases, full and half precision | Maximum / mean absolute error | 8.5e-2 / 2.6e-4 | RGB in [0, 1]. One pixel of one small random-texture case differs by 0.085, where a frame-edge decision flips on rounding; every other pixel is within 4e-4. Along 10 consecutive real 2560x1440 staircase frames, each carrying its own motion from pair to pair, GPU and reference frames score within 0.07 dB |

## Licence and terms

**[Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)](https://creativecommons.org/licenses/by-nc/4.0/)**

The model in `model/` (its weights and GPU shaders) is licensed under CC BY-NC 4.0. You may use, copy and share it and adapt it, including running it in your own software or porting it to another runtime, for non-commercial purposes only, provided you credit "MAKO FG by Eugenio Segala", link to the licence and say whether you made changes. Part of the training data, X4K1000FPS, is provided by its authors for non-commercial research and educational use only. They permitted this open release provided that their condition passes on to every user, so it applies to every use of this model. Commercial use is not permitted. MAKO FG is a non-commercial research project: the model is not sold and the project does not make money from it. The source code of the model, the runtime and the training is not published and is not covered by this licence.

### Runtime library

The prebuilt MAKO FG runtime in `runtime/` (the `mako-fg-host` program, `libmako-fg-runtime.a` and its headers) may be used, run or linked from your own software and redistributed unmodified, free of charge and for non-commercial purposes only, to run MAKO FG models. It is published in binary form; its source code is not published. It contains ncnn and glslang under the licences reproduced in `runtime/THIRD_PARTY_NOTICES`; keep that file with every copy of the runtime and of software built with it.

The model and the runtime library are provided "as is", without warranty of any kind, express or implied, including fitness for a particular purpose. Generated frames are synthetic approximations of the real frames between two inputs. In no event are the authors liable for any claim, damages or other liability arising from their use.

## Acknowledgements

MAKO FG learned from X4K1000FPS, the 4K, 1000 FPS video dataset created by Hyeonjun Sim, Jihyong Oh and Munchurl Kim at KAIST (Korea Advanced Institute of Science and Technology) for their paper XVFI: eXtreme Video Frame Interpolation, an oral presentation at ICCV 2021. Its authors kindly gave their permission for MAKO FG to be released as open weights under a non-commercial licence. We are grateful for their generosity and for the research that made this model possible. MAKO FG also learned from REDS, created by Seungjun Nah and colleagues for the NTIRE 2019 challenge, and the motion it trained with was distilled from Practical-RIFE by Zhewei Huang and colleagues. These acknowledgements do not mean that the authors or their institutions endorse MAKO FG.

## Citations

- S. Nah, S. Baik, S. Hong, G. Moon, S. Son, R. Timofte and K. M. Lee. NTIRE 2019 Challenge on Video Deblurring and Super-Resolution: Dataset and Study. CVPR Workshops, 2019.
- Hyeonjun Sim, Jihyong Oh and Munchurl Kim. XVFI: eXtreme Video Frame Interpolation. In Proceedings of the IEEE International Conference on Computer Vision (ICCV), 2021.
- Z. Huang, T. Zhang, W. Heng, B. Shi and S. Zhou. Real-Time Intermediate Flow Estimation for Video Frame Interpolation. ECCV, 2022.

```bibtex
@inproceedings{sim2021xvfi,
  title={XVFI: eXtreme Video Frame Interpolation},
  author={Sim, Hyeonjun and Oh, Jihyong and Kim, Munchurl},
  booktitle={Proceedings of the IEEE International Conference on Computer Vision (ICCV)},
  year={2021}
}
```

## Release notes

First public release of MAKO FG: open-weight Frame Generation for SteamOS and Linux, with a standalone runtime for other non-commercial software. Includes Quality, Balanced, Performance and Extra Performance modes, SDR/HDR support, and Fixed or Adaptive generation.

## Files and integrity

- `model/`: the model, `weights.bin`, `ops.spv`, `transport.spv`, `mixture.spv` (the weights and the GPU shaders), under the licence above.
- `runtime/x64/` and `runtime/x86/`: the prebuilt MAKO FG runtime for 64-bit and 32-bit Linux: the library `lib/libmako-fg-runtime.a` with its headers in `include/`, and for x64 `bin/mako-fg-host`, the program MAKO runs for 32-bit and 64-bit games; `runtime/RUNTIME.json` records how it was built and `runtime/THIRD_PARTY_NOTICES` holds the licences of the third-party software it contains.
- `LICENSE`: the licence terms of the model and the runtime library.
- `MANIFEST.json` lists every published file with its SHA-256 hash, the licence, the distribution category and the release-rights status.
- Runtime: MAKO builds with MAKO FG support (package protocol `mako-fg-external-v1`), SDR (8-bit or high precision) and HDR (HDR10 or scRGB) input, Flow Scale 25% to 100%, at most 8,192 pixels per axis and 8,294,400 pixels in total.
