#!/usr/bin/env node

import fs from "fs";
import path from "path";

const { readFile, writeFile } = fs.promises;
const { basename } = path;

const arguments_ = process.argv.slice(2);
const [
    packagePath,
    version,
    releaseTag,
    sourceCommit,
    repository,
    archivePath,
    archiveChecksum,
    flatpakArchivePath,
    flatpakArchiveChecksum,
    archPackagePath,
    archPackageChecksum,
] = arguments_;

if (
    !packagePath ||
    !version ||
    !releaseTag ||
    !sourceCommit ||
    !repository ||
    !archivePath ||
    !archiveChecksum ||
    !flatpakArchivePath ||
    !flatpakArchiveChecksum ||
    ![9, 11].includes(arguments_.length)
) {
    console.error(
        "Usage: pin-renderer-release.mjs <package.json> <version> <release-tag> " +
            "<source-commit> <owner/repository> <archive> <archive-sha256> " +
            "<flatpak-archive> <flatpak-sha256> [arch-package arch-package-sha256]",
    );
    process.exit(2);
}
if (
    (archPackagePath && !archPackageChecksum) ||
    (!archPackagePath && archPackageChecksum)
) {
    throw new Error(
        "The Arch package path and SHA-256 must be provided together",
    );
}

if (releaseTag !== `render-v${version}`) {
    throw new Error(
        `Expected renderer tag render-v${version}, received ${releaseTag}`,
    );
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
if (archPackageChecksum && !/^[0-9a-f]{64}$/i.test(archPackageChecksum)) {
    throw new Error("The Arch package SHA-256 is invalid");
}
if (!/^[^/]+\/[^/]+$/.test(repository)) {
    throw new Error(`Expected owner/repository, received ${repository}`);
}

const archiveName = basename(archivePath);
const flatpakArchiveName = basename(flatpakArchivePath);
const archPackageName = archPackagePath ? basename(archPackagePath) : "";
const expectedArchiveName = `MAKO-Renderer-v${version}-linux.tar.xz`;
const expectedFlatpakArchiveName = `MAKO-Renderer-v${version}-flatpaks.tar.xz`;
if (archiveName !== expectedArchiveName) {
    throw new Error(`Expected ${expectedArchiveName}, received ${archiveName}`);
}
if (flatpakArchiveName !== expectedFlatpakArchiveName) {
    throw new Error(
        `Expected ${expectedFlatpakArchiveName}, received ${flatpakArchiveName}`,
    );
}
if (
    archPackageName &&
    !new RegExp(
        `^mako-renderer-bin-${version.split(".").join("\\.")}-[1-9][0-9]*-x86_64\\.pkg\\.tar\\.zst$`,
    ).test(archPackageName)
) {
    throw new Error(
        `Invalid Arch package name for MAKO Renderer ${version}: ${archPackageName}`,
    );
}

async function pinRelease() {
    const manifest = JSON.parse(await readFile(packagePath, "utf8"));
    if (
        !Array.isArray(manifest.remote_binary) ||
        manifest.remote_binary.length !== 1
    ) {
        throw new Error(
            "package.json must contain exactly one remote_binary entry",
        );
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
    binary.host_architectures = ["x86_64"];
    binary.flatpak_bundle = {
        name: flatpakArchiveName,
        url: `${releaseBase}/${flatpakArchiveName}`,
        sha256hash: flatpakArchiveChecksum.toLowerCase(),
    };
    if (archPackageName) {
        binary.arch_package = {
            name: archPackageName,
            url: `${releaseBase}/${archPackageName}`,
            sha256hash: archPackageChecksum.toLowerCase(),
        };
    } else {
        delete binary.arch_package;
    }

    await writeFile(packagePath, `${JSON.stringify(manifest, null, 2)}\n`);
    console.log(
        `Pinned MAKO Renderer ${version} (${releaseTag}) in ${packagePath}`,
    );
}

pinRelease().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
