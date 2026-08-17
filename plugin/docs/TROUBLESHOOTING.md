# Troubleshooting

## HDR (in progress)

HDR exposure is intentionally unavailable in this Decky release. **Disable Experimental HDR (Restart)** is checked,
read-only, and enforced by the backend even if an older profile stored the opposite value. The generated wrapper exports
`MAKO_DISABLE_HDR_EXPOSURE=1` and leaves DXVK at its normal SDR default. Heroic Flatpak launches retain Gamescope WSI
alongside MAKO Renderer. An HDR option being unavailable inside a game is therefore expected and is not
evidence that the `.25` package installed incorrectly.

The pinned engine still contains the HDR10/PQ and linear-scRGB colour-pipeline foundation, Gamescope feedback resolver,
packed HDR10 boundary transport, and safe-passthrough diagnostics. Those pieces remain useful for continued engine
development, but Decky will not expose them until cross-game activation, presentation, colour, and performance have
been validated. The diagnostics helper retains its `hdr` preset for that future testing; ordinary `.25` Decky runs
should remain on SDR.

If MAKO Renderer prevents a title from starting, use **Disable MAKO Renderer on Next
Launch** and restart the game. That control is separate from the locked HDR safety boundary.

## Steam menu / Gamescope recovery

The engine's developing Gamescope HDR transport reserves generated destinations with timeout zero before scheduling
model work, but that transport is not exposed by this Decky release. Current Decky launches use the proven SDR
transport, which intentionally retains ordered FIFO and synchronous fence behaviour. Applying the opportunistic HDR
bypass to SDR caused generated frames to be skipped on ordinary one-frame overlap on Deck-class hardware, so the two
paths remain separate.

Recovery never returns a fabricated out-of-date result merely to make the game rebuild its swapchain. The engine keeps
ordered SDR and its future Gamescope HDR path on different policies. SDR refreshes two real history frames after a cadence discontinuity
and retains the last proven multiplier; sustained model-load collapse falls directly to the cheaper proven generated
level, while validated 2x is retained rather than inserting a visible real-only second. The inactive HDR foundation
uses more conservative bounded discontinuity recovery because a stall can also mean colour-transition or
compositor-admission pressure. It
presents real frames until healthy base cadence has returned for one second, restores the proven level, or starts a
clean ramp after the bounded timeout. This separation prevents HDR safety guards from degrading the established SDR
path.

When Adaptive is capped at 2x and has already validated that level, a short gameplay hitch of up to 250 ms takes a
lighter path: the engine keeps the proven 2x policy, refreshes three real frames of temporal history, and resumes.
Longer interruptions retain the full menu/focus recovery described above.

If the menu interrupts an Adaptive ramp or bridge test, the engine now treats that probe as incomplete rather than
failed and rearms it after two stable seconds. A probe that genuinely fails its throughput checks retains a 15-second
cooldown, with an earlier retry only after a sustained 15% real-only base-rate improvement.

Adaptive also stops at the lowest proven multiplier that can already supply at least 95% of the requested target. If a
newly accepted higher multiplier later causes a sustained real-frame collapse, the engine briefly measures cadence
without generated-frame work. It returns to the lower proven multiplier only when that measurement recovers the prior
rate; otherwise it keeps the higher multiplier because the game scene itself became more demanding. This prevents an
unnecessary 3x load from remaining slower than a capable fixed 2x path while avoiding false backoff in heavy scenes.

## Collecting diagnostics

Published builds keep diagnostics off by default. Local development ZIPs made with `--local-plugin`,
`package:local-engine`, or `package:local-engine-fast`, and direct `dev:*` deployments, enable them automatically.
They are written to the plugin-private file below, rather than Steam's cumulative console log. Starting a game with
diagnostics enabled replaces the previous diagnostic file, so it contains one current test run. Set
`MAKO_PRESENT_DIAGNOSTICS=0` in a game's environment when a development build needs a clean profiling run.

For a Steam game, temporarily replace the normal launch option with:

```text
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 ~/.local/bin/mako-run %command%
```

For a Heroic game, leave its **Wrapper** as `/home/deck/.local/bin/mako-run` and leave **Arguments**
empty. Under the game's **Environment Variables**, add these two name/value rows:

```text
MAKO_PRESENT_DIAGNOSTICS=1
MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25
```

Do not put `%command%` or either variable in Heroic's Wrapper or Arguments fields.

Reproduce the issue, quit the game, then run this in Desktop Mode:

```bash
~/.local/bin/mako-diagnostics all
```

The plugin installs that read-only helper beside its launch wrapper and refreshes it automatically when the plugin is
loaded. It selects the plugin-private log first and falls back to Steam's console log if necessary. Choose one or more
presets to produce a smaller report:

```bash
# Was HDR10/PQ or linear scRGB selected, and was packed HDR10 transport enabled?
~/.local/bin/mako-diagnostics hdr

# Did a Decky change apply live or wait safely for natural recreation?
~/.local/bin/mako-diagnostics config

# Target selection, stabilization, cadence, ramp, load shedding, bridge and rescue.
~/.local/bin/mako-diagnostics adaptive

# Generated-image timeout, real-frame fallback, history warm-up and in-place recovery.
~/.local/bin/mako-diagnostics recovery

# Fixed multiplier/input-output telemetry plus slow GPU/presentation operations.
~/.local/bin/mako-diagnostics performance

# Vulkan layer discovery, Gamescope WSI, swapchain context and colour selection.
~/.local/bin/mako-diagnostics startup

# Failures, timeouts, fallbacks, passthrough and Vulkan errors.
~/.local/bin/mako-diagnostics errors

# Combine related views without collecting unrelated Adaptive policy traffic.
~/.local/bin/mako-diagnostics hdr config adaptive recovery performance errors

# Every relevant MAKO, Vulkan-loader, and Gamescope WSI line from the run.
~/.local/bin/mako-diagnostics --lines 2000 all > ~/mako-report.txt
```

Run `~/.local/bin/mako-diagnostics --help` for the complete preset list. Use `--log PATH` to inspect a
specific saved log and `--lines N` to change the output limit. Presets only filter an existing log; they do not enable
diagnostics or change the game configuration.

If the helper is unavailable, these raw commands provide the two most common reports:

```bash
# HDR selection, private transition, and safe fallback.
grep -aE 'mako: (Gamescope application HDR feedback|swapchain colour pipeline|HDR10 transport|frame generation disabled|MAKO frame-generation initialization failed)|mako: present diagnostics: operation=(runtime-transition-pending|runtime-transition-applied|runtime-state-applied|gamescope-refresh-rate-applied)' ~/.config/mako-render/present-diagnostics.log | tail -n 200

# Adaptive policy and Gamescope recovery.
grep -aE 'mako: present diagnostics: operation=(runtime-transition-pending|runtime-transition-applied|runtime-state-applied|gamescope-refresh-rate-applied|generated-delivery-miss|fixed-plan|adaptive-|skip-generated-frames|resume-generated-frames|generated-image-recovered|history-warmup|swapchain-recreation-suppressed|swapchain-context-create|swapchain-context-destroy)' ~/.config/mako-render/present-diagnostics.log | tail -n 800
```

Remove the temporary Steam launch variables or Heroic environment rows afterwards: diagnostics can generate substantial
log traffic. Local development builds return to automatic collection when that explicit override is removed.

## Update and Flatpak checks

After installing a new plugin ZIP, select **Install MAKO Renderer (developer build)**. ZIP installation updates
the plugin files but does not replace the private native engine by itself.

For Heroic, also open **Flatpak Setup** and select **Update** for Heroic's matching runtime extension—usually 25.08. Do
not disable Heroic preparation or remove its per-game Wrapper command merely to update the extension.

If Decky does not show or reload a ZIP update, uninstall Mako from Decky, install the ZIP again,
restart your Steam Deck or Steam Machine, then install the private engine again.
