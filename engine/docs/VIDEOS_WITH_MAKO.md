# Videos with MAKO

Use mpv's Vulkan renderer for video Frame Generation. MAKO changes displayed frames, not the video file, playback speed, or audio timing.

## Setup

1. Install mpv. For Flatpak:

    ```bash
    flatpak install --user flathub io.mpv.Mpv
    ```

2. For Flatpak, follow [Flatpak preparation](FLATPAK-GUIDE.md) with `appid=io.mpv.Mpv` and its matching runtime extension. Native mpv skips this step.
3. In **MAKO Renderer Configuration**, create a `Video` profile, enable Frame Generation, and start with Fixed 2x. Leave scaling off initially.

Frame Generation and LS1 need Lossless Scaling's default Steam version and a valid DLL path. MAKO Scaler works without it.

## Open videos normally

For Flathub mpv, add `mpv-bin` under **Profile Matching > Matched Processes**. Add these settings to `~/.var/app/io.mpv.Mpv/config/mpv/mpv.conf`:

```ini
vo=gpu-next
gpu-api=vulkan
gpu-context=waylandvk
target-colorspace-hint=no
target-prim=bt.709
target-trc=srgb
dither-depth=8
```

Fully quit mpv, then open a local DRM-free video from the file manager or mpv. These settings apply to all videos; remove them to restore previous defaults.

## Command-line playback

Use the saved `Video` profile and replace the final path below. These commands select the profile explicitly, so no process match or persistent mpv settings are needed. Press `q` to quit.

**Flatpak:**

```bash
flatpak run \
  --env=MAKO_PROFILE=Video \
  io.mpv.Mpv \
  --no-config \
  --vo=gpu-next \
  --gpu-api=vulkan \
  --gpu-context=waylandvk \
  --target-colorspace-hint=no \
  --target-prim=bt.709 \
  --target-trc=srgb \
  --dither-depth=8 \
  /absolute/path/to/video.mp4
```

**Native mpv:**

```bash
MAKO_PROFILE=Video ~/.local/bin/mako-launch mpv \
  --no-config \
  --vo=gpu-next \
  --gpu-api=vulkan \
  --target-colorspace-hint=no \
  --target-prim=bt.709 \
  --target-trc=srgb \
  --dither-depth=8 \
  /absolute/path/to/video.mp4
```

For native mpv, add `--gpu-context=waylandvk` only if `mpv --gpu-context=help` lists it.

## Scaling and limitations

- Leave scaling off for ordinary playback: mpv usually already scales video to the window or display.
- For a scaling test, use a smaller window, start at 1.5x with Quality Supersampling off, restart mpv, and check diagnostics for `spatial scaling active`.
- Higher output FPS depends on the video cadence, display refresh, and GPU capacity. Interpolation can make grain, compression artifacts, cuts, and subtitles more noticeable.
- FFplay may use an unsupported colour format; use mpv with the SDR settings above.

If MAKO does not activate, check the profile, Flatpak runtime branch, and DLL access, then [collect diagnostics](COLLECT_DIAGNOSTICS.md).
