## What's new in MAKO Renderer v5.0.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/phoenix.png" alt="Phoenix release artwork: a blue-and-white mako shark swims through a drowned city beneath a fiery phoenix in the ocean abyss" width="100%">

### Release codename: Phoenix

> _“In the deepest dark, the phoenix kindled a tide the abyss had never known.”_
>
> **Tavian Rook, _Annals of the Ember Tide_**

---

Phoenix introduces MAKO FG, bundled open-weight Frame Generation for SteamOS and Linux. At 3×, shared motion work can turn 30 FPS into 90 FPS when the game and GPU sustain it; 2×, 4× and other output schedules are also supported. Free for non-commercial use, with no Lossless Scaling required. See the [model card](https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/mako-fg/MODEL_CARD.md) for design, benchmarks and limitations, or download the model and runtime from [Hugging Face](https://huggingface.co/eugeniosegala/MAKO-FG).

- **MAKO FG:** V1.24 ships for 64-bit and 32-bit x86 games, Flatpak runtimes and self-contained AppImages. Supports Fixed and Adaptive Frame Generation, SDR, HDR and Flow Scale. If MAKO FG fails, it restarts while the game keeps its real frames. It is the default for profiles without a saved model, including profiles from earlier releases. Developed by Eugenio Segala. See the [setup guide](https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/MAKO-FG.md).
- **Choose your model:** Pick **MAKO FG**, **LSFG Quality** or **LSFG Performance** per profile and power mode. Switching between MAKO FG and LSFG needs a restart; LSFG Quality and Performance switch live. LSFG and LS1 require your own Lossless Scaling installation. Failed LSFG starts fall back to MAKO FG without changing the saved choice. A missing DLL keeps that choice; the editor changes it only when a present DLL fails validation.
- **Tune MAKO FG:** Flow Scale trades motion detail for GPU cost. Ultra Performance uses 60% Flow Scale and MAKO Scaler when Scaling is on. Supports frames up to 8,294,400 pixels (the area of 3840×2160) and 8192 pixels per side; the analysis grid may be capped at higher resolutions.
- **MAKO FG Mode:** Quality, Balanced, Performance and Extra Performance switch live and control how often motion is fully estimated at 2× and 3×. Balanced and Performance correct motion between full estimates; Extra Performance uses cheaper reuse when frames arrive evenly and correction otherwise. Start with Balanced; try Quality for artifacts in fast motion. At 4× and above, every pair gets a full estimate. Ultra Performance uses at least Performance.
- **MAKO Scaler by default:** Profiles without a saved scaling method use MAKO Scaler; existing choices stay. Eligible paths write directly to presentation images, and HDR bridge paths resize without a game-side resize.
- **Record frames for comparison:** Opt-in burst and rolling capture save real frames locally for offline comparisons. Rolling mode buffers recent frames until saved; burst pauses Frame Generation. MAKO Decky provides the L3 + R3 shortcut when armed. See [recording setup and limits](https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/MAKO-FG.md#record-test-frames).
- **Mailbox Presentation:** Optional compatibility setting for FIFO hangs or failures, off by default. Requires a restart. Where supported, MAILBOX replaces queued frames with newer ones, which may reduce smoothness.
- **MangoHud real FPS:** **Count Real Frames Only** excludes generated frames from MangoHud's FPS counter. Requires a restart and follows the existing host MangoHud restrictions.
- **Adaptive and bridge pacing:** Skips recovery probes that cannot succeed and drains queued Gamescope refreshes after sustained timing drift. Adaptive restores normal pacing within seconds when a game's clock outpaces the display at the chosen multiplier, reducing queue buildup and stutter.
- **OpenGL games through Zink:** Fixes swapchain failures and uneven pacing after loading screens. With Base FPS Cap off and a known Gamescope refresh, Fixed mode paces eligible Zink games at refresh divided by the multiplier; Dynamic Cadence Recovery keeps that cap off. RADV display-compression and Game Mode composition workarounds address colour speckles and tearing. See [setup and limits](https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/CONFIGURATION.md#launch-settings).
- **Even pacing on the desktop:** Outside Gamescope, generated frames are spaced evenly when total output is below display refresh, helping capped games and low-FPS video. This can add up to one real-frame interval of latency; spacing switches off when it would slow the game. Game Mode is unchanged.
- **Games Steam starts with its older Vulkan loader:** Fixes Frame Generation when Steam's shader pre-caching layer loads above MAKO, including non-Steam AppImages, and prevents related X11 window crashes.
- **Proton 8 and 9 offscreen windows:** Applies Wine's workaround on the first enabled Renderer launch, fixing offscreen Frame Generation in games such as Half-Life 2. If video or UI displays incorrectly, set `WINE_DISABLE_VK_CHILD_WINDOW_RENDERING_HACK=0` before `mako-launch`. Other Proton versions may use different paths; see [setup and limits](https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/CONFIGURATION.md#proton-offscreen-windows).
- **MAKO FG in games with large per-thread data:** Fixes MAKO FG startup in games with large thread-local allocations, including some native ports.
