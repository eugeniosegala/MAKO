# Flatpak guide

MAKO Renderer ships separate Vulkan runtime extensions for Freedesktop 23.08, 24.08, and 25.08. Install the bundle matching the target application's runtime.

## Packaged extensions

Extract a MAKO Renderer Flatpak archive and install the matching bundle:

New releases use the filename below. The existing v2.0.0 release retains its published legacy name, `mako-render-v2.0.0-flatpaks.tar.xz`; substitute that name when installing v2.0.0.

```bash
tar -xJf MAKO-Renderer-v<version>-flatpaks.tar.xz
flatpak install --user org.freedesktop.Platform.VulkanLayer.makorender-24.08.flatpak
```

The extension ID is `org.freedesktop.Platform.VulkanLayer.makorender` and uses its own dedicated installation path.

## Building extensions

From the MAKO monorepo:

```bash
cd engine
./scripts/package-flatpaks.sh
```

The script builds and verifies both 64-bit and 32-bit MAKO Renderer libraries for every supported runtime. The resulting archive is written under `engine/out/`.

## Manual application override

MAKO Decky normally manages Flatpak access per selected application. For direct development, replace `APP_ID` below and grant the application access to MAKO Renderer's configuration and the user's Steam library:

```bash
appid=APP_ID
flatpak override --user --filesystem="$HOME/.config/mako-render:rw" "$appid"
flatpak override --user --filesystem="$HOME/.local/share/Steam/steamapps/common:ro" "$appid"
flatpak override --user --env=MAKO_CONFIG="$HOME/.config/mako-render/conf.toml" "$appid"
flatpak override --user --env=ENABLE_MAKO=1 "$appid"
flatpak override --user --env=DISABLE_LSFG=1 "$appid"
flatpak override --user --env=DISABLE_LSFGVK=1 "$appid"
```

These are one-time application overrides, not launch options that must be repeated for every game. The host `mako-launch` helper cannot cross the Flatpak sandbox boundary, so the sandbox needs the equivalent loader environment when MAKO Decky is not managing it. The extension remains inactive unless `ENABLE_MAKO=1` is set for the application. The two targeted disable overrides prevent separately installed LSFG-VK releases from loading alongside MAKO; other Vulkan layers remain available normally.
