# Flatpak guide

**MAKO Decky users:** prepare apps through **Flatpak Setup**, then follow the [launcher guide](../../plugin/docs/LAUNCHERS.md).

**Standalone users:** complete the steps below for each app. Installing the native Renderer or creating a profile alone does not prepare a Flatpak.

## Prepare a Renderer profile

In **MAKO Renderer Configuration**, match the game's executable, such as `Game.exe` or `dolphin-emu`, and enable the features you want. Match the executable, not the Flatpak application ID.

Frame Generation and LS1 require Lossless Scaling's default Steam version: set its absolute **Lossless.dll Path**. Without it, turn Frame Generation off and choose MAKO Scaler and/or Shaders.

## Identify the application and runtime

List apps:

```bash
flatpak list --app --columns=application,name,runtime
```

Replace Dolphin's ID below with your app's ID. Keep this terminal open for the remaining commands:

```bash
appid=org.DolphinEmu.dolphin-emu
flatpak info --show-runtime "$appid"
```

Use the reported Freedesktop branch. Supported x86_64 branches are **23.08, 24.08, 25.08, and 26.08**, with 64-bit and 32-bit game layers.

For KDE/GNOME runtimes, inspect the metadata:

```bash
mako_app_runtime=$(flatpak info --show-runtime "$appid")
flatpak info --show-metadata "$mako_app_runtime"
```

Find `[Extension org.freedesktop.Platform.VulkanLayer]` and use a supported `version=` or `versions=` entry. If no supported branch appears, stop; do not guess.

## Packaged extensions

Download and extract `MAKO-Renderer-v<version>-flatpaks.tar.xz` from [Renderer downloads](../README.md#downloads), matching your native release. In the extracted folder, install your runtime branch; replace `24.08` as needed:

```bash
mako_runtime=24.08
flatpak install --user "./org.freedesktop.Platform.VulkanLayer.makorender-$mako_runtime.flatpak"
```

### Graphical extension installation

Alternatively, open **Install MAKO Flatpak Extensions** and select the matching branch. Install each required branch once, then prepare each app below.

## Manual application override

Fully close the app and its games. Grant configuration access:

```bash
flatpak override --user --filesystem="$HOME/.config/mako-render:rw" "$appid"
flatpak override --user --env=MAKO_CONFIG="$HOME/.config/mako-render/conf.toml" "$appid"
```

For Frame Generation or LS1, replace the example with the folder containing your configured `Lossless.dll`. Otherwise, skip this block:

```bash
mako_dll_directory="/absolute/path/to/steamapps/common/Lossless Scaling"
flatpak override --user --filesystem="$mako_dll_directory:ro" "$appid"
```

Apply the activation settings:

```bash
flatpak override --user --env=ENABLE_MAKO=1 "$appid"
flatpak override --user --env=DISABLE_LSFG=1 "$appid"
flatpak override --user --env=DISABLE_LSFGVK=1 "$appid"
flatpak override --user --env=DISABLE_GAMESCOPE_WSI=1 "$appid"
flatpak override --user --env=DISABLE_VKBASALT=1 "$appid"
flatpak override --user --unset-env=ENABLE_GAMESCOPE_WSI "$appid"
flatpak override --user --unset-env=ENABLE_VKBASALT "$appid"
flatpak override --user --env=MAKO_DISABLE_HDR_EXPOSURE=1 "$appid"
flatpak override --user --unset-env=DXVK_HDR "$appid"
flatpak override --user --env=VK_IMPLICIT_LAYER_PATH=/usr/lib/extensions/vulkan/makorender/share/vulkan/implicit_layer.d "$appid"
flatpak override --user --unset-env=VK_ADD_IMPLICIT_LAYER_PATH "$appid"
```

These settings persist for this app. Repeat with another `appid` for each additional app.

- **Heroic/Lutris:** prepare the launcher and match each game's executable. Avoid an app-wide `MAKO_PROFILE`; no host wrapper is needed.
- **EmuDeck:** prepare each Flatpak emulator, select Vulkan, and keep its Steam shortcut and ROM arguments. Native/AppImage emulators use [native launch setup](../README.md#usage).

## Optional private vkBasalt chain

To add bundled Shaders after the setup above:

```bash
flatpak override --user --unset-env=ENABLE_MAKO "$appid"
flatpak override --user --unset-env=DISABLE_MAKO "$appid"
flatpak override --user --unset-env=ENABLE_VKBASALT "$appid"
flatpak override --user --unset-env=DISABLE_VKBASALT "$appid"
flatpak override --user --env=VK_INSTANCE_LAYERS=VK_LAYER_MAKO_render:VK_LAYER_VKBASALT_post_processing "$appid"
```

Create a vkBasalt configuration at the path below, then enable live reload:

```bash
flatpak override --user --env=VKBASALT_CONFIG_FILE="$HOME/.config/mako-render/vkBasalt.conf" "$appid"
flatpak override --user --env=VKBASALT_CONFIG_RELOAD=1 "$appid"
```

Shader files, includes, and textures must be accessible inside the sandbox. Restart after changing activation. To return to MAKO without Shaders:

```bash
flatpak override --user --unset-env=VK_INSTANCE_LAYERS "$appid"
flatpak override --user --unset-env=VKBASALT_CONFIG_FILE "$appid"
flatpak override --user --unset-env=VKBASALT_CONFIG_RELOAD "$appid"
flatpak override --user --unset-env=ENABLE_VKBASALT "$appid"
flatpak override --user --env=DISABLE_VKBASALT=1 "$appid"
flatpak override --user --unset-env=DISABLE_MAKO "$appid"
flatpak override --user --env=ENABLE_MAKO=1 "$appid"
```

## Launch and verify

Restart the app and launch normally, or run:

```bash
flatpak run "$appid"
```

Use Vulkan, including DXVK/VKD3D for compatible Windows games. The host configuration window can be closed.

If MAKO does not activate, inspect the overrides and sandbox access:

```bash
flatpak override --user --show "$appid"
```

```bash
flatpak run --command=sh "$appid" -c '
    ls /usr/lib/extensions/vulkan/makorender/share/vulkan/implicit_layer.d &&
    test -r "$MAKO_CONFIG" && echo "MAKO configuration is readable"
'
```

Missing manifests usually mean the extension or runtime branch is wrong. An unreadable configuration needs its path or access grant corrected. Also check executable matching, enabled features, and DLL access. For a game report, [collect diagnostics](COLLECT_DIAGNOSTICS.md).

## Updates and disabling MAKO

Native Renderer updates do not update Flatpak extensions. Close apps and rerun the newer Flatpak archive's installer for each branch you use. Recheck the branch if an app changes runtime.

Bypass MAKO for one launch of a closed app:

```bash
flatpak run --env=DISABLE_MAKO=1 "$appid"
```

Keep MAKO disabled:

```bash
flatpak override --user --env=DISABLE_MAKO=1 "$appid"
```

Re-enable and restart:

```bash
flatpak override --user --unset-env=DISABLE_MAKO "$appid"
```

Avoid `flatpak override --reset`: it also removes unrelated app settings.

## Gamescope VRR overrides

MAKO Decky handles host discovery automatically. For standalone On/Off VRR requests, run this on the host as the game's user while playing:

```bash
~/.local/bin/mako-vrr-lease --watch-flatpak
```

Requires Gamescope and a VRR-capable display. **Follow Steam** needs no helper. Do not grant extra host-execution permissions to the sandbox.

## Building extensions

See [Flatpak extension builds](BUILDING-FROM-SOURCE.md#flatpak-extension-builds) for source builds and runtime catalogue maintenance.
