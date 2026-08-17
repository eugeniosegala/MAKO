# Flatpak guide

MAKO Renderer ships separate Vulkan runtime extensions for Freedesktop 23.08, 24.08, and 25.08. Install the bundle matching the target application's runtime.

## Packaged extensions

Extract a MAKO Renderer Flatpak archive and install the matching bundle:

```bash
tar -xJf mako-render-<version>-flatpaks.tar.xz
flatpak install --user org.freedesktop.Platform.VulkanLayer.makorender-24.08.flatpak
```

The extension ID is `org.freedesktop.Platform.VulkanLayer.makorender`. It is separate from the public `lsfgvk` extension and does not overwrite it.

## Building extensions

From the MAKO monorepo:

```bash
cd engine
./scripts/package-flatpaks.sh
```

The script builds and verifies both 64-bit and 32-bit MAKO Renderer libraries for every supported runtime. The resulting archive is written under `engine/out/`.

## Manual application override

The MAKO Decky plugin normally manages Flatpak access per selected application. For direct development, replace `APP_ID` below and grant the application access to MAKO Renderer's configuration and the user's Steam library:

```bash
appid=APP_ID
flatpak override --user --filesystem="$HOME/.config/mako-render:rw" "$appid"
flatpak override --user --filesystem="$HOME/.local/share/Steam/steamapps/common:ro" "$appid"
flatpak override --user --env=MAKO_CONFIG="$HOME/.config/mako-render/conf.toml" "$appid"
flatpak override --user --env=ENABLE_MAKO=1 --env=DISABLE_LSFGVK=1 --env=DISABLE_LSFG=1 "$appid"
```

The extension remains inactive unless `ENABLE_MAKO=1` is set for the application. `DISABLE_LSFGVK` and `DISABLE_LSFG` prevent competing public Lossless Scaling layers from entering the same process.
