# MAKO FG model selection

Choose **MAKO FG**, **LSFG Quality**, or **LSFG Performance** under **Frame Generation → Frame Generation Model** in MAKO Decky or the Qt configuration app. The choice belongs to the selected game profile and its AC/battery settings. MAKO FG is the default for new profiles and profiles without a saved model; saved Lossless Scaling choices remain valid. Switching between MAKO FG and LSFG requires a game restart; switching between LSFG Quality and Performance applies live. Unrelated live settings continue to work while a model change is pending.

Lossless Scaling is optional: its choices are grayed out when its DLL is unavailable. If the selected LSFG backend cannot initialize, the Renderer tries bundled MAKO FG once and retains the saved selection. LS1 similarly falls back to MAKO Scaler. A missing DLL keeps the saved choice; when a present DLL fails its compatibility check, the editors save the MAKO alternative for the affected selection and show a warning. Decky’s Live Status reports the model actually applied to the running game.

## Test a game

1. Close the game and select its profile in MAKO Decky or the Qt app.
2. Enable Frame Generation and choose **MAKO FG**. Start with Fixed **2×** and Scaling off, at 1280×720 or 1280×800 with a 30 FPS source cap. Those are starting conditions for comparison, not a resolution restriction.
3. Launch normally through the existing `mako-run %command%` Decky integration or `mako-launch %command%` standalone integration. Do not stack both launchers.
4. In Decky, check **Live Status → Frame Generation → Model: MAKO FG**. Renderer output also records `fg-model selected=MAKO FG VERSION source=release` (or the package's architecture under a `MAKO_FG_PACKAGE` override) and `mako-fg operation=context-open`. A saved selection alone does not prove successful generation.
5. Compare the same scene with **LSFG Quality** or **LSFG Performance**, restarting after the selection. Retain the same source cap, display settings and scene. Increase resolution and workload one change at a time, recording quality, responsiveness and frame-time behavior.

Every MAKO Renderer package carries one separate 64-bit MAKO FG program serving 32-bit and 64-bit games, in native packages and every Flatpak runtime extension. The native development deployment below updates only the host Renderer; Flatpak extensions change only when you install a new Flatpak archive.

The program must use the game's graphics driver. For a 64-bit game started through its own dynamic linker, such as a self-contained AppImage bringing its own driver, the Renderer starts MAKO FG through that linker with the game's library search path and logs `mako-fg operation=host-launch`. Other games, including Proton and Flatpak games, start it directly. A 32-bit game with its own driver cannot run the 64-bit program through that linker.

The runtime accepts SDR (8-bit or high precision) and HDR (HDR10 as packed 10-bit or half-float PQ, and linear scRGB) frames. Frames other than 8-bit SDR are converted to and from a 10-bit working space around the model. It accepts ordered interpolation times, dimensions up to 8192 per axis and at most 3840×2160 pixels. Compatible packages support Flow Scale from 25% to 100%, changing motion-analysis resolution while leaving RGB reconstruction resolution unchanged.

LSFG Performance remains a Lossless Scaling control; Ultra Performance applies a 60% Flow Scale, active-policy capacity and MAKO Scaler to MAKO FG too. MAKO FG's FP16 option selects half storage with FP32 arithmetic; full FP16 arithmetic is unsupported. Higher resolutions and multipliers increase cost. A successful launch does not establish competitive quality, latency or throughput.

Flow Scale is available in both Decky and Qt and uses the existing private-context rebuild path. Higher values favor motion detail; lower values save GPU work. At 100%, a half-analysis package uses its original half grid; 50% approaches the quarter grid. Intermediate percentages produce stride-aligned intermediate grids, not a fixed choice between those two profiles. The logged `flow_scale`, `analysis_width` and `analysis_height` report what was requested and allocated. A release may cap the analysis grid on large frames, as its model card states (from V1.10, at most Flow Scale 60% at 1440p and 80% at 1080p); the allocated grid then reflects the cap. This control cannot repair missing learned motion by itself.

New operator shaders declare Flow Scale support in the package capability metadata. Older packages retain their original behavior at 100% and reject other values explicitly. Set Flow Scale to 100% before rolling back to such a package. Keep the package, Renderer and shader versions together; do not replace shaders inside an existing immutable package.

Compatible direct-image packages pool motion input and reconstruct the generated output directly from the shared GPU images. This saves full-resolution float RGB conversions without changing trained weights or the selected analysis grid. The runtime reads the transport shader ABI before creating its descriptor layout, retains the older conversion path for archived packages and rejects unsupported declarations. A Renderer rollback requires its matching package. GPU profiling counts direct reconstruction under synthesis, so use complete GPU span when comparing versions.

## MAKO FG Mode

**MAKO FG Mode** sits under the model picker in MAKO Decky and the Qt app and sets how often MAKO FG solves motion for the selected profile. It applies live.

| Mode | Motion solved | Between solved pairs | Use |
| --- | --- | --- | --- |
| **Quality** | Every real-frame pair | — | A game that shows artifacts in the other modes |
| **Balanced** (default) | One pair in two | Corrected | Most games |
| **Performance** | One pair in three | Corrected | A game short of GPU time, or a low-power device |
| **Extra Performance** | One pair in four | Moved forward while frames arrive evenly, corrected otherwise | The most GPU time, with the most risk of artifacts |

Between solved pairs, MAKO FG moves the last motion field forward along its own motion and keeps the last static-overlay map. A corrected pair then analyses its new real frame and refines that motion with the finest level of MAKO FG's motion search; a moved pair uses it as it is and skips that analysis. Each generated frame is still reconstructed from the two real frames. A sparse frame comparison on every pair makes the next pair solve after a scene cut. From 4x, where the thin-structure layer runs, every pair solves.

Moved motion is right only while real frames arrive evenly and the camera keeps its speed and direction, because a game moves its content by its velocity times the time between frames. An uneven rate makes it fall short or overshoot, which fast camera motion shows as judder, uneven speed and doubled edges. Correction reads the frames, so it follows an uneven rate and gradual changes of speed or direction as closely as a solved pair, but it costs about 60% of one where moving costs about a quarter. A camera that reverses at full speed within one frame moves further than the correction reaches; in every mode but Quality that frame is wrong unless it falls on a solved pair.

To choose, the Renderer passes each real frame's interval since the previous one it gave MAKO FG, measured on the same cadence clock Adaptive uses (`mako_fg_protocol.hpp`, host protocol 3). MAKO FG treats the last four intervals as even when the longest is at most 1.1 times the shortest and uneven up to 1.5 times; an even cadence corrects in one search pass and an uneven one in two. Beyond 1.5 times, as after a hitch, the next four pairs solve. This needs a `mako-fg-host` that speaks protocol 3; packaging rejects an older one.

On MAKO FG's 1280×720 camera-pan sequences on a Navi 33 GPU at 2×, these modes save this much GPU time per real-frame pair against Quality (MAKO FG `docs/NATIVE.md` records the measurements):

| Mode                  | Even frame rate | Uneven frame rate |
| --------------------- | --------------- | ----------------- |
| **Balanced**          | about 15–20%    | about 15%         |
| **Performance**       | about 25%       | about 15–20%      |
| **Extra Performance** | about 50%       | about 20–25%      |

A game capped at its frame rate gains spare GPU time from a higher mode, not more frames, so start at Balanced and move up only if the game cannot hold its rate. Ultra Performance uses at least Performance and a 60% Flow Scale.

## Record test frames

To compare frame generation on a scene from your own game, record a few seconds of its real frames and score MAKO FG and Lossless Scaling on them offline. Recording is off unless the game's launch armed it:

```text
MAKO_RECORD_FRAMES=1 ~/.local/bin/mako-run %command%
```

The L3 + R3 shortcut requires MAKO Decky to be running, including when the game uses the standalone `mako-launch` command instead of `mako-run`. There are two modes:

- **Burst** (`MAKO_RECORD_FRAMES=1`): each request pauses frame generation, waits `MAKO_RECORD_FRAMES_DELAY` seconds (default 5) and records `MAKO_RECORD_FRAMES_SECONDS` (default 3, at most 10). With frame generation paused, the game presents every real frame at its own full rate. This gives the closest reference frames, but you must predict the moment.
- **Rolling** (`MAKO_RECORD_FRAMES=ring`): frame generation stays on and you play normally. The Renderer keeps only the last `MAKO_RECORD_FRAMES_SECONDS` (default 10, at most 60) of real frames in memory, discarding the oldest as new ones arrive, within `MAKO_RECORD_FRAMES_RING_GB` (default a quarter of the device's RAM, at least 4). The 4 GB a 16 GB Steam Deck gets holds about 129 frames at 2160p, 290 at 1440p or 518 at 1080p, so about 4.8 s at 1440p and 60 FPS; a 32 GB PC holds about twice as many. Nothing is written until you save, so the moment you just saw is kept without wearing the disk. The saved frames are the real frames at your base rate, the ones frame generation interpolated between.

Both modes keep each launch's saved frames within `MAKO_RECORD_FRAMES_MAX_GB` (default 40, ten full 4 GB rolling saves), and every save keeps 2 GB of the disk free. A save that would pass either limit is shortened to fit: a burst stops early and a rolling save keeps its newest frames. After every save, the oldest saved recordings are deleted until the captures folder fits in `MAKO_RECORD_FRAMES_KEEP_GB` (default 100).

1. Start the game with frame generation enabled as usual, so the Renderer can copy its frames.
2. Burst: press both sticks (L3 + R3) together, then perform the motion to test, such as a fast camera turn, within the delay that the notification announces; frame generation resumes by itself afterwards.
3. Rolling: right after you notice a problem, press both sticks (L3 + R3) together. In either mode L3 + R3 is the only way to record: MAKO Decky watches the game's controller for it only while an armed game runs. Steam Input gives every controller to the game as a virtual gamepad, so this works for any controller that goes through Steam Input. The game also receives the press, so use it where L3 + R3 does nothing harmful. Every press ends in a notification: the frame count once saved, or why nothing was saved, such as a refusal from the Renderer or a game that is not showing frames.
4. The recording is saved under `~/.local/share/mako-render/captures/DATE-TIME-PROCESS/`: raw frames, their present times and `recording.json`. Decky reports the frame count and any dropped frames.

A recording's real frames become the reference frames: dropping every second frame leaves pairs for 2× evaluation, and keeping every fourth leaves pairs for 4×. Higher, steadier game frame rates give smaller steps between real frames and a closer match to normal play, so lower the game's settings or cap it at a steady rate while recording when you can. Continuous copies in rolling mode cost some GPU and memory bandwidth, so keep it for test sessions. Recordings stay on your device; they are game footage for local evaluation only.

## Build and install locally

From this repository, after closing games that use MAKO:

```bash
pnpm --dir plugin run dev:host --ui --reload
```

MAKO FG runs as a separate program. The Renderer starts `mako-fg-host` and links no MAKO FG code, so this repository builds without any MAKO FG file. Deployment and packaging install the active release from the local `engine/models/mako-fg` folder, which MAKO FG's release command writes (see [Installed release](#installed-release)). The SteamOS development builder and every packager verify that no layer contains MAKO FG, and packagers verify the program and model beside each layer. Builds with `-DMAKO_FG=OFF` omit the client. A Renderer without the program cannot generate frames for profiles that select MAKO FG, including the default: the game keeps running with its real frames and the log records why.

Every Renderer package carries MAKO FG and fails without it: `engine/scripts/package-local.sh`, `package-flatpaks.sh`, `package-local-release.sh`, publication and the Decky local-engine ZIPs. [Building from source](BUILDING-FROM-SOURCE.md#mako-fg) explains how packages install and verify the release.

`dev:host --ui` builds and verifies the selected 64-bit and 32-bit Renderer libraries, the CLI, the Qt app and MAKO Decky. The normal development deployment owners manage the active installed paths. It creates no archive and publishes nothing. Qt builds require the documented host development headers; `MAKO_OPENGL_INCLUDE_DIR` can point to already cached compatible OpenGL headers. See [the build guide](BUILDING-FROM-SOURCE.md) and [Decky packaging](../../plugin/docs/PACKAGING.md).

Packages set `-DMAKO_REQUIRE_FG=ON`, which turns a missing or altered release, or one not cleared for public distribution, into a configuration error. MAKO FG does not replace or modify `Lossless.dll`; LS1 scaling still needs the user's licensed DLL.

## Installed release

`engine/models/mako-fg/` is an ignored local folder holding the MAKO FG open-weight release that packages install; this repository tracks no MAKO FG binary, and earlier versions stay in MAKO FG's own release history. The public model card and licence are tracked in [`engine/docs/mako-fg/`](mako-fg/MODEL_CARD.md). `versions.json` names the active version, and its version folder holds the model files in `model/` (the weights and GPU shaders), the 64-bit `mako-fg-host` program in `runtime/x64/bin`, `LICENSE`, its public `version.json`, its `MODEL_CARD.md` with banner (what the model is, its version, target devices, terms, how to use it outside MAKO and how and on what it was trained) and its `MANIFEST.json` (every file's hash, the licence, the distribution category and whether the release is cleared for public distribution). MAKO FG is a non-commercial research project: its releases are open weights under the non-commercial licence in `LICENSE`, and the program is published in binary form. Read the card's release-rights notice before publishing a build that carries a version.

MAKO FG's `release export`, `release select` and `release sync` commands write these files; do not edit them by hand.

`mako-backend/CMakeLists.txt` verifies the active version's model files and program against its `MANIFEST.json`, and the 64-bit build installs them in `mako-fg/` beside the 64-bit Renderer library, printing `MAKO FG: installing release VERSION`. With `MAKO_REQUIRE_FG=ON`, it also requires `MANIFEST.json` to set `public_release_cleared`. Selecting MAKO FG then uses that release, and the log reports `fg-model selected=MAKO FG VERSION source=release`. `MAKO_FG_PACKAGE` (an absolute package directory, such as a MAKO FG native package or a release's `model/` folder) overrides it for a test process; the package must suit the installed program. `MAKO_FG_HOST` overrides the program path for a test process. Reinstall after selecting another version.

## Local mixture model

`mako-shared-mixture-v1` uses shared motion and a small fused full-resolution reconstruction head. Its `MFGBV1` weight header requires operator ABI 4, transport ABI 3 and a hash-bound `mixture.spv` head with ABI 1. Every required shader is checked and retained with the package. The head blends two warped sources and an unwarped temporal source; training and real-game qualification remain separate from release. Older graph packages retain their existing shader layouts.

## Configuration and runtime ownership

`frame_generation_model = "lossless-scaling"` or `"mako-fg"` is a native profile field. `MAKO_FRAME_GENERATION_MODEL` is its environment-only configuration equivalent. `MAKO_FG_PACKAGE` optionally overrides the installed release with a package directory for a test process; it does not override the saved model selection in the layer. CLI benchmarking/quality commands use that explicit package variable to select MAKO FG. A relative package path is rejected.

`mako-backend/src/context.hpp` owns the common internal context interface; `mako_fg.cpp` starts `mako-fg-host` once per process and selects the application GPU through it. `mako_fg_protocol.hpp` owns the Renderer's declaration of the socket messages; the program keeps its own, and packaging compares their versions. Image and timeline descriptors transfer once, including failure paths. Each context has its own socket, and every request waits for its reply, so generated work reaches the GPU in the same order as a direct call and the frame path takes no lock.

A 32-bit layer uses the same 64-bit program, which keeps the model and its driver out of a 32-bit game's address space. Vulkan shares memory and semaphores only within one driver, and a 32-bit driver can be a different build, so the Renderer compares the program's driver and device UUIDs with the game's and refuses a mismatch; the game then runs without MAKO FG.

A failed request reaches the Renderer's existing recovery as an error. A thread that sleeps on the host's socket notices a lost host at once, even when the game's threads are stuck, and signals the shared timeline so application work cannot wait on it forever. Later requests fail at once and the game continues with its real frames. The next present, or the next request a new swapchain makes, starts a replacement, up to three times per process, and rebuilds its context, which pauses that present briefly. Past that limit **Live Status** reports frame generation as stopped until the game restarts. On the Gamescope path a signalled render fence proves the program finished, so ordinary frames need no readiness round trip.

The host ends when the Renderer's process ends, however it ends, and the Renderer keeps no child process. Its environment has `LD_PRELOAD` removed and the layer-disabling variables of the backend instance set. Live frames remain on the GPU. All outputs in a generated batch currently complete together, and backend memory accounting is unavailable. The process-level backend stays fixed across profile changes and swapchain recreation until restart.

MAKO FG owns training and independent native/PyTorch comparisons. MAKO Gym owns real-game trials, licensed controls, synchronization validation and full GPU-cost profiling. Follow their `docs/NATIVE.md`, `docs/MAKO-FG.md` and `docs/RENDER-PERFORMANCE.md`. Preserve the exact model, source, binary hashes, dependencies and raw evidence for each comparison. A one-game trial does not qualify all games, devices, resolutions, display paths or recovery behavior.
