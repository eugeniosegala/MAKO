# Videos with MAKO

MAKO Renderer can apply Frame Generation, spatial scaling, or both to a video player that presents through a supported Vulkan swapchain. It changes displayed frames, not playback speed, duration, audio timing, or the encoded file.

## Use mpv with Vulkan

Use [mpv](https://mpv.io/) with its `gpu-next` Vulkan renderer. The Flatpak is a convenient reproducible path because its runtime and MAKO extension can be selected explicitly:

```bash
flatpak install --user flathub io.mpv.Mpv
```

A local DRM-free 24 or 30 FPS video makes generated output easy to compare. Fixed 2× targets roughly 48 or 60 displayed FPS, and Fixed 3× targets roughly 72 or 90, subject to display refresh, presentation capacity, and GPU headroom.

## Create a profile

Open **MAKO Renderer Configuration** and create a profile named `Video`.

- Start Frame Generation testing with Fixed 2×.
- Enable Scaling before launching mpv, choose a method and factor, and begin with a window smaller than the intended output.
- LSFG and LS1 require a lawful, user-supplied Lossless Scaling installation and `Lossless.dll`; Native Resolution and MAKO Scaler do not.

The commands below select the profile with `MAKO_PROFILE=Video`, so `active_in` does not need to match mpv. Replace `Video` if you use another profile name.

## Prepare Flatpak mpv

Check the application's Freedesktop runtime:

```bash
flatpak info --show-runtime io.mpv.Mpv
```

If the result is `org.freedesktop.Platform/x86_64/25.08`, extract the MAKO Flatpak archive and install its matching extension in the same user scope:

```bash
tar -xJf MAKO-Renderer-v<version>-flatpaks.tar.xz
flatpak install --user org.freedesktop.Platform.VulkanLayer.makorender-25.08.flatpak
```

Use the actual branch reported by `flatpak info`. Then follow [Manual application override](FLATPAK-GUIDE.md#manual-application-override) with `APP_ID=io.mpv.Mpv`. Those app-scoped overrides provide the configuration and Steam-library mounts, select the MAKO extension, isolate implicit layers, and keep HDR exposure off. They do not enable MAKO for other Flatpak applications.

## Play a video

Replace the final argument with an absolute path to the video:

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
  --loop-file=inf \
  --autofit=854x480 \
  /absolute/path/to/video.mp4
```

The explicit colour options keep the player on MAKO's validated SDR/sRGB path. `--autofit=854x480` provides a useful small window for scaling tests; change or remove it for other dimensions. Press `q` to quit.

For a native mpv installation, use the same options through the standalone launcher:

```bash
MAKO_PROFILE=Video ~/.local/bin/mako-launch mpv \
  --no-config \
  --vo=gpu-next \
  --gpu-api=vulkan \
  --target-colorspace-hint=no \
  --target-prim=bt.709 \
  --target-trc=srgb \
  --dither-depth=8 \
  --loop-file=inf \
  --autofit=854x480 \
  /absolute/path/to/video.mp4
```

The native Vulkan context depends on the desktop session. Add `--gpu-context=waylandvk` only when `mpv --gpu-context=help` lists it for that installation.

## Verify activation and delivery

For one short Flatpak run, add `--env=MAKO_PRESENT_DIAGNOSTICS=1` to `flatpak run` and capture the terminal output. Let the clip play for at least 20 seconds, then quit fully and create a focused report:

```bash
mako-diagnostics performance scaling --lines 2000
```

The log should identify `io.mpv.Mpv` or `mpv`, confirm the selected profile and backend resources, and show generated-image delivery when Frame Generation is active. A profile selection or target FPS alone is not delivery evidence.

If MAKO does not activate, confirm that the extension branch matches the reported runtime, the `Video` profile exists, and `Lossless.dll` is visible when using LSFG or LS1. Follow [Collect diagnostics](COLLECT_DIAGNOSTICS.md) for a full report.

## Scaling and quality

Begin around 1.5×, leave Quality Supersampling off on constrained hardware, and compare Native Resolution, MAKO Scaler, LS1 Quality, and LS1 Performance one at a time. Generated frames can make compression artifacts, film grain, scene cuts, subtitles, and other discontinuous overlays more noticeable.

Method and sharpness changes use private live transitions. A factor change that alters the effective extent needs a compatible swapchain recreation; restarting mpv is the dependable boundary when the player does not recreate naturally.

## Why FFplay is not the reference path

FFplay may select `VK_FORMAT_R16G16B16A16_UNORM` with `VK_COLOR_SPACE_PASS_THROUGH_EXT`. MAKO rejects that unvalidated pair and preserves native playback instead of generating with uncertain colour semantics. The mpv command above explicitly requests the validated SDR/sRGB path.
