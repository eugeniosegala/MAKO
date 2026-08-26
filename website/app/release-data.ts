import pluginManifest from '../../plugin/package.json';

const renderer = pluginManifest.remote_binary[0];

if (!renderer) {
  throw new Error('plugin/package.json must define one published MAKO Renderer payload');
}

const repository = renderer.source_repository.replace(/\/$/, '');
const deckyVersion = pluginManifest.version;
const rendererVersion = renderer.version;
const deckyTag = `plugin-v${deckyVersion}`;

export const releaseData = {
  deckyVersion,
  rendererVersion,
  links: {
    repository,
    deckyRelease: `${repository}/releases/tag/${deckyTag}`,
    rendererRelease: `${repository}/releases/tag/${renderer.release_tag}`,
    deckyDownload: `${repository}/releases/download/${deckyTag}/MAKO-Decky-v${deckyVersion}.zip`,
    rendererDownload: renderer.url,
    flatpakDownload: renderer.flatpak_bundle.url,
    docs: `${repository}#install-and-use`,
    issues: `${repository}/issues`,
    allReleases: `${repository}/releases`,
    losslessScaling: 'https://store.steampowered.com/app/993090/Lossless_Scaling/',
  },
} as const;
