#!/usr/bin/env node

import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const [component, version, repository] = process.argv.slice(2);
const usage = "Usage: update-release-links.mjs <decky|renderer> <version> <owner/repository>";

if (!component || !version || !repository) {
  throw new Error(usage);
}

if (!/^[A-Za-z0-9._-]+\/[A-Za-z0-9._-]+$/.test(repository)) {
  throw new Error(`Invalid GitHub repository: ${repository}`);
}
if (!/^[A-Za-z0-9._-]+$/.test(version)) {
  throw new Error(`Invalid release version: ${version}`);
}

const componentDetails = {
  decky: {
    name: "MAKO Decky",
    tag: `plugin-v${version}`,
    text: "Download latest MAKO Decky ZIP",
    asset: `MAKO-Decky-v${version}.zip`,
  },
  renderer: {
    name: "MAKO Renderer",
    tag: `render-v${version}`,
    text: "Download MAKO Renderer",
  },
}[component];

if (!componentDetails) {
  throw new Error(`Unknown component '${component}'. ${usage}`);
}

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const readmePath = join(dirname(scriptDirectory), "README.md");
const original = readFileSync(readmePath, "utf8");
const escapedName = componentDetails.name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
const rowPattern = new RegExp(
  `^(\\| \\*\\*${escapedName}\\*\\* \\| [^|]+ \\| )\\[[^\\]]+\\]\\([^)]+\\)( \\|)$`,
  "m",
);
const releaseUrl = componentDetails.asset
  ? `https://github.com/${repository}/releases/download/${componentDetails.tag}/${componentDetails.asset}`
  : `https://github.com/${repository}/releases/tag/${componentDetails.tag}`;
const updated = original.replace(
  rowPattern,
  `$1[${componentDetails.text}](${releaseUrl})$2`,
);

if (updated === original) {
  if (!rowPattern.test(original)) {
    throw new Error(`Could not find the ${componentDetails.name} Downloads row in ${readmePath}`);
  }
  console.log(`README already links ${componentDetails.name} to ${componentDetails.tag}.`);
} else {
  writeFileSync(readmePath, updated, "utf8");
  console.log(`Updated README link for ${componentDetails.name} to ${componentDetails.tag}.`);
}
