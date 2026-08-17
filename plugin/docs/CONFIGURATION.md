# Configuration guide

The defaults are a good starting point: fixed **2x**, Flow Scale **0.90**, Performance Mode disabled, and FP16 allowed
where supported. Adaptive mode defaults to a **90 FPS** target with **Smooth Cadence** enabled. Adaptive settings can be
changed while a game is running, but the layer briefly resets its timing and stability calculations afterwards. Let it
settle for a few seconds before judging performance. Fixed and Adaptive reserve one shared generated-frame capacity,
so switching between them applies live when the selected multiplier/ceiling fits that capacity. The layer never forces
the game to rebuild its swapchain for a UI change. The engine includes a private HDR/SDR resource transition for future
HDR work, but this Decky release keeps HDR exposure locked off and therefore remains on the SDR resource path.
Other GPU-backend, flow-scale, or performance-mode changes that cannot be applied safely remain pending until the game
naturally recreates its swapchain or is restarted.

## In-game V-Sync

V-Sync can make LSFG-VK feel smoother by giving it more evenly spaced real frames to work with. It can also add input
lag or clash with a game's FPS cap, VRR, or compositor. Every game is different, so test V-Sync both on and off and
keep the option that feels smoother and more responsive.

For reference, Decky's established SDR path keeps generated and real frames in order before they reach the display.
That engine detail is separate from the V-Sync option inside the game.

## Frame-generation mode

- **Frame Generation (Live On/Off):** Leave this control on to use either Fixed or Adaptive Frame Generation. When
  it is off, neither mode generates frames; your selected mode and settings remain saved for when you turn it back on.
- **FPS Multiplier:** Fixed 2x, 3x, or 4x generation. Use the live **Frame Generation** switch to pass through real
  frames without changing the selected mode. Under Gamescope, the engine uses the confirmed display refresh as a
  delivery budget:
  it suppresses synthetic frames that cannot be scanned out rather than letting a nominal 2x/3x/4x sequence run above
  the display rate. This does not cap the game's real frames.
- **Adaptive Frame Generation:** Optional mode that estimates the real frame rate and schedules zero to three generated
  frames to approach the selected target. It replaces the fixed multiplier controls for that profile. Target, ceiling,
  and Smooth Cadence changes apply live; expect a short settling period while Adaptive recalculates its timing.

  **Live-change settling period:** Changing an Adaptive setting while a game is running resets its timing and stability
  calculations. Give it a few seconds to settle before judging image quality, smoothness, or input responsiveness;
  normal play can continue once it has settled.
- **Target FPS:** 30–240 FPS. This setting is used only by Adaptive mode. It is a target, not a limiter: it cannot
  reduce a game already running above target,
  exceed the selected ceiling, or overcome GPU/model/compositor limits.
- **Maximum Adaptive Multiplier:** Ceiling for generated frames: 2x, 3x, or 4x. 3x is the balanced default; 2x usually
  gives the best image quality, while 4x gives Adaptive more headroom to reach the target. Test per game.
- **Smooth Cadence:** Enabled by default in Adaptive mode. Strict scheduling settles first and constant cadence is
  considered only when the target already needs nearly every matching cadence slot. It can make displayed motion look
  smoother, but does so by holding a continuous generation level; this can lower the real-frame presentation cadence and
  increase input lag. Leave it enabled where its visual smoothness is worthwhile; disable it when strict target
  scheduling or responsiveness matters more for that game. After a severe sustained slowdown, Adaptive briefly
  measures the real-only rate and either resumes fractional
  scheduling or tests one higher level when **Maximum Adaptive Multiplier** permits it. Rescue never exceeds that
  maximum and has a cooldown to avoid repeated switching.

  **Per-game tuning:** There is no universal best mode. Compare Fixed 2x with Adaptive, then try Adaptive with Smooth
  Cadence both enabled and disabled. The best choice depends on a game's frame pacing, real-frame headroom, display
  refresh, and responsiveness, so change one setting at a time and let Adaptive settle before comparing results.

Adaptive also has an automatic Steam-menu discontinuity safeguard, independent of **Smooth Cadence**. After a hard
cadence stall, it temporarily presents real frames, waits for the measured base cadence to remain healthy for one
second, and then restores the last proven generation level. If the old cadence does not recover within five seconds,
Adaptive discards that stale baseline and ramps again from zero. A sustained gameplay cadence drop instead stabilizes
for one second and rebases at the new measured rate, avoiding a five-second wait for an old rate that is no longer
achievable. This can briefly reduce displayed FPS after leaving a menu, but avoids treating its transient frame rate as
normal gameplay. After 2x has already proved stable, an isolated short gameplay hitch instead refreshes three real
history frames and resumes 2x immediately. Fixed mode does not use these Adaptive policies.

## Profiles and per-game selection

The plugin stores multiple MAKO Renderer profiles in its private `conf.toml`; it does not create a separate layer install or
config file for every game. In the plugin's **Profile** section, choose **New Profile**, enter a name, and the plugin
copies the selected profile's settings, then switches to the new profile. Configure it normally afterwards.

To select engine settings automatically for a game, enter its executable/process name in **Active In**. MAKO Renderer then
matches the appropriate profile at launch. Once at least one profile has an **Active In** value, the wrapper leaves
engine-profile selection to MAKO Renderer; you can return the UI selector to **Default** without breaking automatic matching.
The following settings are profile-based and support that automatic match:

- Live Frame Generation, Fixed multiplier or Adaptive mode, Target FPS, Maximum Adaptive Multiplier, and Smooth Cadence
- Flow Scale, Performance Mode, GPU matching, and Active In

The `Lossless.dll` path and FP16 permission are shared globally because they apply to the installed engine, not to an
individual game.

Decky also keeps the launcher compatibility options per profile—Disable MAKO Renderer on Next Launch, Disable
Experimental HDR (Restart), Base FPS Cap, Steam Deck Mode, and Zink. They are saved in this plugin's private profile state and
are restored when you select that profile in Decky. They cannot follow **Active In** automatically: those variables must
be set by the wrapper before MAKO Renderer sees the game's process name. Select the profile manually before launching a game
that needs one of those compatibility options, then restart the game after changing profiles.

## Quality and matching

- **Flow Scale:** 0.25–1.0. Lower values generally reduce GPU cost; higher values favour optical-flow quality.
- **Performance Mode:** Uses a lighter model with lower GPU overhead and more artifacts. For the best v2 image quality,
  start with it disabled.
- **Allow FP16:** Allows half-precision processing on supported GPUs. It can improve performance; results vary by game
  and driver.
- **Lossless.dll Path:** Override detection when necessary. Leaving it blank allows MAKO Renderer discovery.
- **Active In:** Optional comma-separated executable/process matching. If set, MAKO Renderer selects matching profiles
  automatically.
- **GPU:** Optional GPU identifier for multi-GPU systems.

## Compatibility options

The bundled engine includes matching 64-bit and 32-bit Vulkan layers. The Vulkan loader selects the correct layer for
the game's process, so native 32-bit Vulkan games do not require a WoW64 launcher option. The CLI and configuration UI
remain 64-bit because they are not loaded into the game process.

- **Disable MAKO Renderer on Next Launch:** Compatibility troubleshooting only. Prevents the MAKO Renderer
  Vulkan layer from loading when the game next starts, so you can test without it or bypass a startup/attachment
  problem. This requires a game restart. It is different from **Frame Generation**, which switches synthesis on or off
  live while the layer remains loaded.
- **Base FPS Cap:** Optionally caps the base DirectX framerate before multiplication.
- **Steam Deck Mode:** A per-game compatibility path.
- **Zink:** Optional Vulkan-based OpenGL path for OpenGL games.

Gamescope WSI and MangoHud controls are deliberately not shown. The wrapper enables MAKO Renderer's uniquely named
layer and disables both public LSFG identities for that game. In this release, **Disable Experimental HDR
(Restart)** is checked and read-only: the wrapper exports `MAKO_DISABLE_HDR_EXPOSURE=1` and leaves DXVK at its normal
SDR default. Heroic Flatpak launches explicitly keep Gamescope WSI ahead of MAKO Renderer so compositor
presentation and frame limiting remain available. Existing profiles that previously opted into HDR are normalized back
to this stable SDR boundary when loaded or saved.

### HDR launch boundary

These controls belong to different components and are deliberately kept separate:

| Setting | Owner | Behaviour in this release |
| --- | --- | --- |
| `MAKO_DISABLE_HDR_EXPOSURE=1` | MAKO Renderer | Blocks this layer's unfinished HDR exposure and transition path. It does not configure the game's renderer or system HDR. |
| `DXVK_HDR` | DXVK | The wrapper unsets it instead of forcing `0`, leaving DXVK at its normal SDR default and avoiding a plugin-wide renderer override. |
| `ENABLE_GAMESCOPE_WSI` | Gamescope | The wrapper preserves the launcher's value and keeps Gamescope's manifest available in Heroic. It never disables Gamescope to enforce LSFG's HDR boundary. |

This means the plugin can keep its own HDR work disabled without changing DXVK policy or removing Gamescope's normal
presentation and frame-limiting integration. Users should not add any of these variables manually for an ordinary SDR
launch.

HDR frame generation remains under development. The pinned engine contains format classification, HDR10/PQ and
linear-scRGB conversion, Gamescope feedback, packed-boundary transport, and safe-passthrough groundwork, but the Decky
launcher does not expose that path in `.25`. In-game HDR controls may therefore be disabled; this is expected. A future
release will unlock the control only after activation, presentation, colour, and performance are validated across
games. Use
**Disable MAKO Renderer on Next Launch** when the layer itself is the suspected cause. The plugin
writes `pacing = 'none'` and does not expose a
dual-GPU control. See
[Troubleshooting](TROUBLESHOOTING.md) and the release notes for build-specific compatibility guidance.
