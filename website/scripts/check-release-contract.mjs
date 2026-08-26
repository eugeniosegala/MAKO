#!/usr/bin/env node

import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const websiteRoot = dirname(dirname(fileURLToPath(import.meta.url)));
const repositoryRoot = dirname(websiteRoot);
const manifest = JSON.parse(readFileSync(join(repositoryRoot, 'plugin/package.json'), 'utf8'));
const pageSource = readFileSync(join(websiteRoot, 'app/page.tsx'), 'utf8');
const pagesWorkflow = readFileSync(join(repositoryRoot, '.github/workflows/pages.yml'), 'utf8');
const [renderer] = manifest.remote_binary ?? [];
const semver = /^\d+\.\d+\.\d+$/;

if (!semver.test(manifest.version ?? '')) {
  throw new Error('plugin/package.json must define a semantic MAKO Decky version');
}
if (!renderer || !semver.test(renderer.version ?? '')) {
  throw new Error('plugin/package.json must define a semantic MAKO Renderer payload version');
}

const repository = renderer.source_repository?.replace(/\/$/, '');
const rendererTag = `render-v${renderer.version}`;
const rendererArchive = `MAKO-Renderer-v${renderer.version}-linux.tar.xz`;
const flatpakArchive = `MAKO-Renderer-v${renderer.version}-flatpaks.tar.xz`;
const releaseBase = `${repository}/releases/download/${rendererTag}`;

if (!repository?.startsWith('https://github.com/')) {
  throw new Error('The website requires a canonical GitHub source_repository');
}
if (renderer.release_tag !== rendererTag || renderer.name !== rendererArchive || renderer.url !== `${releaseBase}/${rendererArchive}`) {
  throw new Error('The MAKO Renderer release tag, archive name, and URL must agree');
}
if (renderer.flatpak_bundle?.name !== flatpakArchive || renderer.flatpak_bundle?.url !== `${releaseBase}/${flatpakArchive}`) {
  throw new Error('The MAKO Renderer Flatpak archive name and URL must agree');
}
if (/\bv?\d+\.\d+\.\d+\b/.test(pageSource)) {
  throw new Error('website/app/page.tsx must not hardcode a release version');
}
if (!pagesWorkflow.includes('"plugin/package.json"')) {
  throw new Error('The GitHub Pages workflow must redeploy when release metadata changes');
}

console.log(`Website release contract matches MAKO Decky v${manifest.version} and MAKO Renderer v${renderer.version}.`);
