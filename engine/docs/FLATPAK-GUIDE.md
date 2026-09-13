# Flatpak guide

**A Flatpak application needs its own MAKO extension and sandbox preparation before it can use MAKO Renderer.** Installing the native Renderer, creating a profile, or putting the host `mako-launch` command before `flatpak run` does not complete that setup. After preparation, launch the app normally; the saved application overrides activate MAKO inside its sandbox.

The standalone **MAKO Renderer Configuration** window edits profiles but does not prepare Flatpak programs. The archive's **Install MAKO Flatpak Extensions** helper installs extensions only. If MAKO Decky manages your installation, use **Flatpak Setup** and the [Decky launcher guide](../../plugin/docs/LAUNCHERS.md) instead of the manual overrides below.

For standalone use, complete these steps for each Flatpak application:

1. [Prepare a Renderer profile](#prepare-a-renderer-profile) for the game or emulator.
2. [Identify the application and runtime](#identify-the-application-and-runtime).
3. [Install the matching extension](#packaged-extensions).
4. [Prepare the application's access and environment](#manual-application-override).
5. [Restart, launch, and verify](#launch-and-verify).

## Prepare a Renderer profile

Use the host `~/.local/bin/mako-ui` or edit `~/.config/mako-render/conf.toml`, following [Renderer usage](../README.md#usage). Under **Profile Matching > Matched Processes**, add the rendering process, such as `Game.exe` or `dolphin-emu`. A Flatpak application ID such as `org.DolphinEmu.dolphin-emu` identifies the sandbox for setup commands; it is not the executable name used for profile matching.

For Frame Generation or LS1, install the **default public version** of Lossless Scaling through Steam, with beta participation disabled, then select the absolute path to its licensed `Lossless.dll` under **Lossless.dll Path (Restart)**. An explicit path avoids relying on Steam discovery inside the sandbox. You will grant access to its containing directory below, including when it is on an SD card, another Steam library, or inside a Flatpak Steam installation. For open MAKO Scaler use without the DLL, turn **Frame Generation** off and select **MAKO Scaler**. Enable scaling before starting the application.

## Identify the application and runtime

Run these commands in a host terminal. First list the installed apps:

```bash
flatpak list --app --columns=application,name,runtime
```

Copy the target's application ID into `appid` below. The example selects Dolphin; substitute your installed game, launcher, or emulator. Keep the same terminal open for the remaining commands so `appid` stays set:

```bash
appid=org.DolphinEmu.dolphin-emu
flatpak info --show-runtime "$appid"
```

MAKO ships x86_64 extensions for Freedesktop **23.08, 24.08, and 25.08**, each including layers for 64-bit and 32-bit x86 game processes. If the reported runtime is `org.freedesktop.Platform/x86_64/24.08`, choose MAKO's **24.08** branch.

For a KDE or GNOME runtime, its own version is not necessarily the extension branch. Inspect its installed metadata:

```bash
mako_app_runtime=$(flatpak info --show-runtime "$appid")
flatpak info --show-metadata "$mako_app_runtime"
```

Find the `[Extension org.freedesktop.Platform.VulkanLayer]` section and use its `version=` value or a supported entry in `versions=`. For example, `version=25.08` requires MAKO's **25.08** bundle regardless of the KDE/GNOME version in the runtime name. This follows [Flatpak's extension branch rules](https://docs.flatpak.org/en/latest/extension.html#finding-base-runtime-version). If that section is absent, no listed branch matches MAKO's supported versions, or the app uses another architecture, these packaged extensions do not cover that application; do not guess a branch.

## Packaged extensions

Download `MAKO-Renderer-v<version>-flatpaks.tar.xz` from [Renderer downloads](../README.md#downloads), using the same release as your native installation, and extract it into a folder. It is a separate archive from `MAKO-Renderer-v<version>-linux.tar.xz`. In the same terminal, change to the extracted folder and install the branch identified above. This example selects **24.08**; change it to your matching branch:

```bash
mako_runtime=24.08
flatpak install --user "./org.freedesktop.Platform.VulkanLayer.makorender-$mako_runtime.flatpak"
```

The extension ID is `org.freedesktop.Platform.VulkanLayer.makorender`. Install each branch needed by your apps once; multiple branches can coexist. Every app still needs its own preparation below.

### Graphical extension installation

Alternatively, double-click **Install MAKO Flatpak Extensions** in the extracted folder and choose the Freedesktop branch identified above. Choose **Execute** if your file manager asks. The installer performs the same user-scoped Flatpak installation; run it again to install a different supported runtime.

Installing the extension does not prepare an application to use MAKO. Configure the target application's filesystem access and MAKO environment as described below, or use MAKO Decky's **Flatpak Setup** when Decky owns the installation.

## Manual application override

Fully quit the app and any games it started before preparing it. With `appid` still set to the selected application ID, grant access to the host configuration directory and select the same configuration file used by the Renderer UI:

```bash
flatpak override --user --filesystem="$HOME/.config/mako-render:rw" "$appid"
flatpak override --user --env=MAKO_CONFIG="$HOME/.config/mako-render/conf.toml" "$appid"
```

If using Frame Generation or LS1, replace the example directory below with the directory containing the `Lossless.dll` selected in your profile's global settings. Keep the quotes for paths containing spaces. Skip this grant when using only the open scaler without a configured DLL:

```bash
mako_dll_directory="/absolute/path/to/steamapps/common/Lossless Scaling"
flatpak override --user --filesystem="$mako_dll_directory:ro" "$appid"
```

Finally, save the MAKO activation and loader environment for this application:

```bash
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

These overrides persist for the selected application and apply to processes it starts inside the same sandbox. Repeat preparation with a different `appid` for each additional app. The environment exposes only MAKO's implicit manifests and selects the standalone SDR presentation policy; Gamescope and Game Mode remain active outside the application's layer chain. See [WSI isolation](WSI-ISOLATION.md) for the layer boundary and [Flatpak's command reference](https://docs.flatpak.org/en/latest/flatpak-command-reference.html#flatpak-override) for override options.

For standalone Flatpak Heroic or Lutris, prepare the launcher app and create profiles matching each game's executable. These manual overrides are app-wide; automatic process matching chooses the game profile. Avoid setting one app-wide `MAKO_PROFILE` for a launcher that runs several games. This workflow needs no host `mako-launch` path in a game's wrapper field. MAKO Decky's per-game sandbox wrapper is a separate managed workflow.

For EmuDeck, prepare each emulator installed as a Flatpak and select **Vulkan** in its graphics settings. Keep the existing Steam shortcut target, ROM path, and launch arguments. Matching an emulator process applies that profile to games running in that process; it does not distinguish ROMs. Native or AppImage emulators use the [native Renderer launch instructions](../README.md#usage).

## Launch and verify

After preparing the app, start it from its usual application entry or existing Steam shortcut, or run:

```bash
flatpak run "$appid"
```

Do not add `%command%` in a terminal. The prepared app receives its saved environment at startup, so an already-running launcher or emulator must be fully restarted. The host Renderer UI does not need to remain open. The application's rendering backend must use Vulkan, including DXVK/VKD3D for compatible Windows games.

To inspect the saved per-user overrides:

```bash
flatpak override --user --show "$appid"
```

Check for the configuration path and `ENABLE_MAKO=1`, then check that the extension manifests and configuration file are actually available inside the sandbox:

```bash
flatpak run --command=sh "$appid" -c '
    ls /usr/lib/extensions/vulkan/makorender/share/vulkan/implicit_layer.d &&
    test -r "$MAKO_CONFIG" && echo "MAKO configuration is readable"
'
```

A missing manifest directory points to extension installation or runtime matching. An unreadable configuration points to its path or filesystem grant. If the layer loads but the feature stays inactive, check **Matched Processes**, the selected feature, and access to the configured DLL. The checks above confirm sandbox setup, not successful rendering; use [standalone diagnostics](COLLECT_DIAGNOSTICS.md) for an actual game session.

## Updates and disabling MAKO

Updating the native Renderer does not update Flatpak extensions. Download the newer Flatpak archive, close the affected apps, and rerun its installer for each branch you use. If an app update changes its runtime, recheck the required branch and install the matching extension before relaunching it. Existing app overrides and profiles remain in place.

To bypass MAKO for one launch of a fully closed, prepared app:

```bash
flatpak run --env=DISABLE_MAKO=1 "$appid"
```

To keep MAKO disabled for that app until you choose to re-enable it:

```bash
flatpak override --user --env=DISABLE_MAKO=1 "$appid"
```

To re-enable it, remove that disable variable and restart the app:

```bash
flatpak override --user --unset-env=DISABLE_MAKO "$appid"
```

Bypassing MAKO keeps the extension, filesystem grants, and loader overrides in place. It does not restore the app's previous layer setup. Avoid `flatpak override --reset` as a routine MAKO toggle: it also removes unrelated overrides for the application.

## Building extensions

From the MAKO monorepo:

```bash
cd engine
./scripts/package-flatpaks.sh
```

The script builds and verifies both Renderer roles for 64-bit and 32-bit processes on every supported runtime. The resulting archive is written under `engine/out/`.

`dist/flatpak/mako-render/runtime-versions.txt` owns the ordered Renderer build matrix. Each listed version must have a matching standalone manifest in that directory; MAKO Decky's shared runtime contract is regression-tested against the same ordered versions.
