# Videos with MAKO

MAKO Renderer can apply Frame Generation, spatial scaling, or both to a video player that presents through a supported Vulkan swapchain. It changes displayed frames, not playback speed, duration, audio timing, or the encoded file.

## Use mpv with Vulkan

Use [mpv](https://mpv.io/) with its `gpu-next` Vulkan renderer. The Flatpak is a convenient reproducible path because its runtime and MAKO extension can be selected explicitly:

```bash
flatpak install --user flathub io.mpv.Mpv
```

A local DRM-free 24 or 30 FPS video makes generated output easy to compare. Fixed 2× targets roughly 48 or 60 displayed FPS, and Fixed 3× targets roughly 72 or 90, subject to display refresh, presentation capacity, and GPU headroom.

## One-time Flatpak preparation

Complete this section once when using Flathub `io.mpv.Mpv`. Native mpv users can skip directly to the command-line workflow. The mpv Flatpak must use the MAKO runtime extension matching its Freedesktop runtime. Check the required branch:

```bash
flatpak info --show-runtime io.mpv.Mpv
```

If the result is `org.freedesktop.Platform/x86_64/25.08`, extract the MAKO Flatpak archive and install its matching extension in the same user scope:

```bash
tar -xJf MAKO-Renderer-v<version>-flatpaks.tar.xz
flatpak install --user org.freedesktop.Platform.VulkanLayer.makorender-25.08.flatpak
```

Use the actual branch reported by `flatpak info`. Then follow [Manual application override](FLATPAK-GUIDE.md#manual-application-override) with `APP_ID=io.mpv.Mpv`. Those app-scoped overrides provide the configuration and Steam-library mounts, select the MAKO extension, isolate implicit layers, and keep HDR exposure off. They do not enable MAKO for other Flatpak applications.

Choose exactly one workflow below after completing the Flatpak preparation. The UI workflow uses automatic `mpv-bin` process matching plus persistent mpv settings so videos can be opened normally. The command-line workflow uses an explicit `MAKO_PROFILE` selection plus per-launch mpv arguments, so it does not need a matched process or persistent mpv settings.

## Workflow A: MAKO UI and normal desktop opening

Use this workflow to configure MAKO graphically and open videos from the file manager or mpv desktop application.

### 1. Create the MAKO profile

1. Fully quit mpv by pressing `q` or closing every mpv window.
2. Open **MAKO Renderer Configuration** from the application launcher, or run `~/.local/bin/mako-ui`.
3. Select **Create New Profile** and name the profile `Video`.
4. Open **Profile Matching** > **Matched Processes** > **Edit…**, enter `mpv-bin`, and select the add button.
5. Enable Frame Generation and begin with Fixed 2x. Enable scaling before starting mpv only when a supported swapchain has a smaller source extent than its presentation extent.

Only `mpv-bin` is needed for the Flathub package. Its `/app/bin/mpv` command is a shell wrapper that replaces itself with the real `/app/bin/mpv-bin` process before Vulkan starts; `io.mpv.Mpv` is the Flatpak application ID, not the Vulkan executable identity. The UI saves profile changes automatically.

Frame Generation and LS1 scaling require a lawful, user-supplied Lossless Scaling installation and `Lossless.dll`. The open MAKO Scaler does not require the DLL.

### 2. Configure Flatpak mpv for Vulkan

Open `~/.var/app/io.mpv.Mpv/config/mpv/mpv.conf` in a text editor such as Kate and add these persistent Vulkan and validated SDR presentation settings:

```ini
vo=gpu-next
gpu-api=vulkan
gpu-context=waylandvk
target-colorspace-hint=no
target-prim=bt.709
target-trc=srgb
dither-depth=8
```

These settings apply to every video opened by this Flatpak mpv installation. Remove them to restore mpv's previous defaults.

### 3. Open the video

Open a local DRM-free video normally from the file manager or mpv desktop application. The Flatpak overrides load MAKO, `mpv-bin` selects the `Video` profile, and the saved mpv settings keep the presentation path on Vulkan SDR. Fully quit and reopen mpv after changing the matched process or enabling scaling because MAKO cannot attach to an already-running process and scaling activation is process-static.

### 4. Check the result

Validate the saved Renderer configuration:

```bash
mako-cli validate
```

With the video running, inspect the current session log:

```bash
journalctl --user -b --no-pager | grep 'MAKO Renderer:' | tail -n 30
```

A working session reports `render layer active`, `using profile with name 'Video' (identified via executable)`, backend initialization, and supported swapchain colour-pipeline records.

## Workflow B: manual command line

Use this workflow for an explicit named profile and per-launch mpv arguments. Do not add the persistent mpv settings or `mpv-bin` match from Workflow A unless normal desktop opening is also required.

### 1. Create or select the profile

Add a `Video` profile to `~/.config/mako-render/conf.toml`, or reuse an existing profile with that exact name:

```toml
[[profile]]
name = "Video"
multiplier = 2
frame_generation_enabled = true
scaling_enabled = false
```

The command below uses `MAKO_PROFILE=Video`, so this profile does not need an `active_in` process match. Validate the file before starting mpv:

```bash
mako-cli validate
```

`mako-cli` validates configuration and tests Renderer resources; it does not launch applications or attach MAKO to an existing process.

### 2. Launch Flatpak mpv

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

### 3. Launch native mpv

Use MAKO's standalone helper for a native mpv installation:

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

### 4. Capture detailed Frame Generation diagnostics

For a short Flatpak test, add `--env=MAKO_PRESENT_DIAGNOSTICS=1` to the `flatpak run` command above, before `io.mpv.Mpv`, and append `2>&1 | tee "$HOME/MAKO-mpv-video-session.log"` to capture its output. Let the clip play for at least 20 seconds, quit fully, then create a focused report:

```bash
mako-diagnostics performance scaling --log "$HOME/MAKO-mpv-video-session.log" --lines 2000
```

Let the clip run for at least 20 seconds. The log should report that the frame-generation backend and resources are available and show generated images being presented. The displayed rate is limited by the video's real cadence, the selected multiplier or Adaptive target, the display refresh rate, and available GPU headroom.

If MAKO does not activate, confirm that the extension branch matches the reported runtime, the `Video` profile exists, and `Lossless.dll` is visible when using LSFG or LS1. Follow [Collect diagnostics](COLLECT_DIAGNOSTICS.md) for a full report.

## Should I enable scaling for video?

For ordinary video playback, leave MAKO spatial scaling disabled and use Frame Generation by itself. mpv normally scales the decoded video to its window or the screen before MAKO receives the Vulkan frame, so a fullscreen player often already presents at the display resolution. Press `Shift+i` in mpv to see the video's encoded resolution and current rendering statistics.

Enable MAKO scaling only for a deliberate test with a smaller player window. Start at 1.5x with Quality Supersampling disabled, fully restart mpv after enabling it, and inspect the session log described above. Scaling is running only when the log reports `spatial scaling active`; `active=0` or an `inactive_reason` means MAKO safely left the player at its native swapchain size. Disable scaling again when the player does not provide a compatible lower-resolution swapchain.

When scaling does activate, compare the MAKO Scaler, LS1 Quality, and LS1 Performance one at a time. Frame Generation may make compression artifacts, film grain, hard scene cuts, and subtitles more noticeable because generated images interpolate surrounding real frames.

## Why FFplay is not the reference path

FFplay may select `VK_FORMAT_R16G16B16A16_UNORM` with `VK_COLOR_SPACE_PASS_THROUGH_EXT`. MAKO rejects that unvalidated pair and preserves native playback instead of generating with uncertain colour semantics. The mpv command above explicitly requests the validated SDR/sRGB path.
