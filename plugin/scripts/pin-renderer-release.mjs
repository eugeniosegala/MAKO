#!/usr/bin/env node

import { basename } from "node:path";
import { readFile, writeFile } from "node:fs/promises";

const [
  packagePath,
  version,
  releaseTag,
  sourceCommit,
  repository,
  archivePath,
  archiveChecksum,
  flatpakArchivePath,
  flatpakArchiveChecksum
] = process.argv.slice(2);

if (
  !packagePath ||
  !version ||
  !releaseTag ||
  !sourceCommit ||
  !repository ||
  !archivePath ||
  !archiveChecksum ||
  !flatpakArchivePath ||
  !flatpakArchiveChecksum
) {
  console.error(
    "Usage: pin-renderer-release.mjs <package.json> <version> <release-tag> " +
      "<source-commit> <owner/repository> <archive> <archive-sha256> " +
      "<flatpak-archive> <flatpak-sha256>"
  );
  process.exit(2);
}

if (releaseTag !== `render-v${version}`) {
  throw new Error(`Expected renderer tag render-v${version}, received ${releaseTag}`);
}
if (!/^[0-9a-f]{40}$/i.test(sourceCommit)) {
  throw new Error(`Invalid source commit: ${sourceCommit}`);
}
if (!/^[0-9a-f]{64}$/i.test(archiveChecksum)) {
  throw new Error("The native archive SHA-256 is invalid");
}
if (!/^[0-9a-f]{64}$/i.test(flatpakArchiveChecksum)) {
  throw new Error("The Flatpak archive SHA-256 is invalid");
}
if (!/^[^/]+\/[^/]+$/.test(repository)) {
  throw new Error(`Expected owner/repository, received ${repository}`);
}

const archiveName = basename(archivePath);
const flatpakArchiveName = basename(flatpakArchivePath);
const expectedArchiveName = `mako-render-v${version}-linux.tar.xz`;
const expectedFlatpakArchiveName = `mako-render-v${version}-flatpaks.tar.xz`;
if (archiveName !== expectedArchiveName) {
  throw new Error(`Expected ${expectedArchiveName}, received ${archiveName}`);
}
if (flatpakArchiveName !== expectedFlatpakArchiveName) {
  throw new Error(
    `Expected ${expectedFlatpakArchiveName}, received ${flatpakArchiveName}`
  );
}

const manifest = JSON.parse(await readFile(packagePath, "utf8"));
if (!Array.isArray(manifest.remote_binary) || manifest.remote_binary.length !== 1) {
  throw new Error("package.json must contain exactly one remote_binary entry");
}

const binary = manifest.remote_binary[0];
const releaseBase = `https://github.com/${repository}/releases/download/${releaseTag}`;
binary.name = archiveName;
binary.version = version;
binary.lineage_version = version;
binary.source_repository = `https://github.com/${repository}`;
binary.release_tag = releaseTag;
binary.source_commit = sourceCommit;
binary.url = `${releaseBase}/${archiveName}`;
binary.sha256hash = archiveChecksum.toLowerCase();
binary.flatpak_bundle = {
  name: flatpakArchiveName,
  url: `${releaseBase}/${flatpakArchiveName}`,
  sha256hash: flatpakArchiveChecksum.toLowerCase()
};

await writeFile(packagePath, `${JSON.stringify(manifest, null, 2)}\n`);
console.log(`Pinned MAKO Renderer ${version} (${releaseTag}) in ${packagePath}`);
