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
    releaseTarget: "Latest",
    releaseUrl: `https://github.com/${repository}/releases/latest`,
    tableText: "Latest MAKO Decky release (ZIP under Assets)",
    linkText: "latest MAKO Decky release",
    expectedLinks: {
      "README.md": 1,
      "plugin/README.md": 1,
      "engine/README.md": 1,
    },
  },
  renderer: {
    name: "MAKO Renderer",
    releaseTarget: `render-v${version}`,
    releaseUrl: `https://github.com/${repository}/releases/tag/render-v${version}`,
    tableText: "Latest MAKO Renderer release (Linux archive under Assets)",
    linkText: "latest MAKO Renderer release",
    expectedLinks: {
      "README.md": 1,
      "plugin/README.md": 1,
      "engine/README.md": 2,
    },
  },
}[component];

if (!componentDetails) {
  throw new Error(`Unknown component '${component}'. ${usage}`);
}

const escapeRegExp = (value) => value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const repositoryRoot = dirname(scriptDirectory);
const readmePath = join(repositoryRoot, "README.md");
const escapedName = componentDetails.name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
const rowPattern = new RegExp(
  `^(\\| \\*\\*${escapedName}\\*\\* \\| [^|]+ \\| )\\[[^\\]]+\\]\\([^)]+\\)( \\|)$`,
  "m",
);
const linkPattern = new RegExp(
  `(\\[${escapeRegExp(componentDetails.linkText)}\\]\\()[^)]+(\\))`,
  "g",
);
const changedPaths = [];

for (const [relativePath, expectedLinkCount] of Object.entries(componentDetails.expectedLinks)) {
  const path = join(repositoryRoot, relativePath);
  const original = readFileSync(path, "utf8");
  let updated = original;

  if (path === readmePath) {
    if (!rowPattern.test(original)) {
      throw new Error(`Could not find the ${componentDetails.name} Downloads row in ${path}`);
    }
    updated = updated.replace(
      rowPattern,
      `$1[${componentDetails.tableText}](${componentDetails.releaseUrl})$2`,
    );
  }

  const matchingLinks = [...updated.matchAll(linkPattern)];
  if (matchingLinks.length !== expectedLinkCount) {
    throw new Error(
      `Expected ${expectedLinkCount} '${componentDetails.linkText}' link(s) in ${path}, ` +
      `found ${matchingLinks.length}`,
    );
  }
  updated = updated.replace(
    linkPattern,
    (_match, prefix, suffix) => `${prefix}${componentDetails.releaseUrl}${suffix}`,
  );

  if (updated !== original) {
    writeFileSync(path, updated, "utf8");
    changedPaths.push(relativePath);
  }
}

if (changedPaths.length === 0) {
  console.log(
    `Documentation already links every ${componentDetails.name} reference to ` +
    `${componentDetails.releaseTarget}.`,
  );
} else {
  console.log(
    `Updated ${componentDetails.name} release-page links to ${componentDetails.releaseTarget}: ` +
    changedPaths.join(", "),
  );
}
