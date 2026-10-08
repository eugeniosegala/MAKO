# Native Steam Remote Play

Apply MAKO on the **receiving device** when using Steam's native **Stream** action. Requires native x86_64 Linux Steam, a current MAKO Renderer installation, and system Python 3.11 or newer. Flatpak Steam, standalone Steam Link, browser streaming, and AArch64 are unsupported.

## Enable Remote Play

1. Close all streams on the receiving device.
2. Open **Remote Play** in MAKO Decky or **MAKO Renderer Configuration** and select **Override Remote Play**.
3. Select a saved profile and choose Frame Generation, Scaling, and/or Shaders.
4. Start streaming through Steam.

The override applies to all native Steam streams until removed. MAKO Scaler and Shaders work without Lossless Scaling; Frame Generation and LS1 require it.

## Profiles

Each stream starts with your selected profile. **Create Profile** can save and select a named copy during a stream for future reuse. In Decky, close the stream before using the profile dropdown, rename, or delete controls.

Settings follow the usual live/restart rules, including AC/battery settings. Restart the stream for options marked **Restart**. Edit in one UI at a time; an alternate Qt configuration must be managed in Qt.

## Restore Steam

Close streams, then select **Remove Remote Play Override** in MAKO Decky or **Restore Steam Client** in Qt. Saved profiles remain available. Restore before manually deleting Renderer files or removing a system package.

## Recovery and limits

If Steam updates and the panel shows **Needs attention**, close streams, restore Steam, then enable the override again. Avoid updating Steam while enabling or removing the override.

If restoration reports a modified wrapper or damaged backup, stop and seek help; do not delete the backup. Remove older experimental overrides with their original tools before enabling this one.

If MAKO's launch files are unavailable, the verified original Steam client can launch without MAKO. A missing or damaged original prevents launch; this fallback cannot recover failures after the client starts.

### SDR output

Streams automatically use sRGB/SDR output while the override is enabled. There is no extra toggle; HDR output is unsupported. Check colours, smoothness, and input response on your display, as results vary by setup. An active MAKO indicator alone does not confirm visible frame generation.

If you need help, [collect diagnostics](../../COLLECT_DIAGNOSTICS.md).
