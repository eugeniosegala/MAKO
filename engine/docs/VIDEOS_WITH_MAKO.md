# Videos with MAKO

MAKO Renderer can apply Frame Generation, spatial scaling, or both to a video player that presents through a supported Vulkan swapchain. This changes only the displayed frames: it does not change the video's playback speed, duration, audio timing, or encoded file.

## Recommended player

Use [mpv](https://mpv.io/) with its Vulkan `gpu-next` renderer. The Flatpak build is a convenient tested path because its Vulkan runtime and MAKO extension can be selected explicitly:

```bash
flatpak install flathub io.mpv.Mpv
```

Any local DRM-free video that mpv can decode is suitable. A 24 or 30 FPS clip makes Frame Generation particularly easy to verify: Fixed 2x should display approximately 48 or 60 FPS, while Fixed 3x should display approximately 72 or 90 FPS when the display and presentation path have enough headroom.

## Configure a video profile

Install MAKO Renderer and open **MAKO Renderer Configuration**. Create a profile named `Video`, then configure Frame Generation, scaling, or both:

- For Frame Generation, enable it and begin with Fixed 2x.
- For scaling, enable it before launching mpv, select the scaling method, factor, and sharpness, and start with a player window smaller than the desired output.
- Frame Generation and LS1 scaling require a lawful, user-supplied Lossless Scaling installation and `Lossless.dll`. The open MAKO Scaler does not require the DLL.

The commands below select this profile explicitly with `MAKO_PROFILE=Video`, so the profile does not need to match mpv's executable name. If you choose another profile name, replace `Video` in the commands.

## Prepare Flatpak mpv

The mpv Flatpak must use the MAKO runtime extension matching its Freedesktop runtime. Check the required branch:

```bash
flatpak info --show-runtime io.mpv.Mpv
```

For output such as `org.freedesktop.Platform/x86_64/25.08`, extract the MAKO Renderer Flatpak archive and install its `25.08` extension:

```bash
tar -xJf MAKO-Renderer-v<version>-flatpaks.tar.xz
flatpak install --user org.freedesktop.Platform.VulkanLayer.makorender-25.08.flatpak
```

Apply the same isolated application boundary used by MAKO's other direct Flatpak integrations:

```bash
appid=io.mpv.Mpv
flatpak override --user --filesystem="$HOME/.config/mako-render:rw" "$appid"
flatpak override --user --filesystem="$HOME/.local/share/Steam/steamapps/common:ro" "$appid"
flatpak override --user --env=MAKO_CONFIG="$HOME/.config/mako-render/conf.toml" "$appid"
flatpak override --user --env=ENABLE_MAKO=1 "$appid"
flatpak override --user --env=DISABLE_LSFG=1 "$appid"
flatpak override --user --env=DISABLE_LSFGVK=1 "$appid"
flatpak override --user --env=DISABLE_GAMESCOPE_WSI=1 "$appid"
flatpak override --user --unset-env=ENABLE_GAMESCOPE_WSI "$appid"
flatpak override --user --env=MAKO_DISABLE_HDR_EXPOSURE=1 "$appid"
flatpak override --user --unset-env=DXVK_HDR "$appid"
flatpak override --user --env=VK_IMPLICIT_LAYER_PATH=/usr/lib/extensions/vulkan/makorender/share/vulkan/implicit_layer.d "$appid"
flatpak override --user --unset-env=VK_ADD_IMPLICIT_LAYER_PATH "$appid"
```

These are one-time, app-scoped overrides. They do not enable MAKO globally or alter other Flatpak applications. See the [Flatpak guide](FLATPAK-GUIDE.md) for the runtime-extension architecture and direct-application contract.

## Play a video

Replace `/absolute/path/to/video.mp4` with the absolute path to the video:

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

The explicit color options keep mpv on MAKO's validated SDR/sRGB presentation path. `--autofit=854x480` supplies a useful low-resolution window for scaling tests; change or remove it when testing another source or presentation size. Press `q` to quit.

To use a native mpv installation instead of Flatpak, keep the same mpv options and launch it through MAKO's standalone helper:

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

The available native Vulkan context depends on the desktop session, so omit the Flatpak-specific `--gpu-context=waylandvk` unless `mpv --gpu-context=help` confirms it for that installation.

## Verify Frame Generation

Enable presentation diagnostics for a short test and save the player's terminal output:

```bash
flatpak run \
  --env=MAKO_PROFILE=Video \
  --env=MAKO_PRESENT_DIAGNOSTICS=1 \
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
  /absolute/path/to/video.mp4 \
  2>&1 | tee "$HOME/MAKO-mpv-video-session.log"
```

Let the clip run for at least 20 seconds. The log should identify `io.mpv.Mpv` or `mpv`, report that the frame-generation backend and resources are available, and show generated images being presented. The displayed rate is limited by the video's real cadence, the selected multiplier or Adaptive target, the display refresh rate, and available GPU headroom.

If the layer does not activate, confirm that the selected MAKO extension branch matches `flatpak info --show-runtime io.mpv.Mpv`, that the `Video` profile exists, and that `Lossless.dll` is visible for Frame Generation or LS1. Use `mako-diagnostics` or the [standalone diagnostics guide](COLLECT_DIAGNOSTICS.md) for a focused report.

## Scaling and quality considerations

Scaling a video adds GPU work just as it does for a game. Begin with a moderate factor such as 1.5x, keep Quality Supersampling disabled on resource-constrained hardware, and compare the MAKO Scaler, LS1 Quality, and LS1 Performance one at a time. Frame Generation can expose compression artifacts, film grain, hard scene cuts, subtitles, or other discontinuous overlays more clearly because generated images interpolate the surrounding real frames.

Changing the scaler method or sharpness uses MAKO's private live transition. A scale-factor change that alters the effective extent still needs a compatible swapchain recreation; restarting mpv is the dependable boundary when the player does not recreate its Vulkan swapchain naturally.

## Why FFplay is not the recommended test path

FFplay may select a 16-bit `VK_FORMAT_R16G16B16A16_UNORM` surface with `VK_COLOR_SPACE_PASS_THROUGH_EXT`. MAKO currently rejects that unvalidated color-space contract instead of generating frames with uncertain color correctness. This can make ordinary playback work while Frame Generation remains inactive. The mpv command above deliberately requests the validated SDR/sRGB path and is therefore the recommended video test.
