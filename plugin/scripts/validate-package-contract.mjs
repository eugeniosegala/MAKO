#!/usr/bin/env node

import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import path from "node:path";

const [packageDirectoryArgument, mode] = process.argv.slice(2);

if (!packageDirectoryArgument || !["local", "release"].includes(mode)) {
    throw new Error(
        "Usage: validate-package-contract.mjs <package-directory> <local|release>",
    );
}

const packageDirectory = path.resolve(packageDirectoryArgument);
const manifestPath = path.join(packageDirectory, "package.json");
const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
const owns = (name) => Object.prototype.hasOwnProperty.call(manifest, name);

for (const legalPath of [
    "ASSET_PROVENANCE.md",
    "LICENSE.md",
    "THIRD_PARTY_NOTICES.md",
    "third_party_licenses/@decky-api-LGPL-2.1.txt",
    "third_party_licenses/react-icons-LICENSE.txt",
    "third_party_licenses/tslib-0BSD.txt",
]) {
    const absolutePath = path.join(packageDirectory, legalPath);
    let contents;
    try {
        contents = readFileSync(absolutePath, "utf8");
    } catch {
        throw new Error(`Required legal file is missing: ${legalPath}`);
    }
    if (!contents.trim()) {
        throw new Error(`Required legal file is empty: ${legalPath}`);
    }
}

const sourceMapPath = path.join(packageDirectory, "dist", "index.js.map");
let sourceMap;
try {
    sourceMap = JSON.parse(readFileSync(sourceMapPath, "utf8"));
} catch {
    throw new Error("The packaged frontend source map is missing or invalid");
}
if (
    !Array.isArray(sourceMap.sources) ||
    !Array.isArray(sourceMap.sourcesContent) ||
    sourceMap.sources.length !== sourceMap.sourcesContent.length
) {
    throw new Error("The packaged frontend source map has no aligned source content");
}
for (const bundledSource of ["@decky/api/dist/index.js", "react-icons/"]) {
    const sourceIndex = sourceMap.sources.findIndex((source) =>
        source.includes(bundledSource),
    );
    if (
        sourceIndex < 0 ||
        typeof sourceMap.sourcesContent[sourceIndex] !== "string" ||
        !sourceMap.sourcesContent[sourceIndex].trim()
    ) {
        throw new Error(
            `The packaged frontend source map is missing bundled source: ${bundledSource}`,
        );
    }
}

function validateRendererMetadata(renderer, metadataName) {
    if (!renderer || typeof renderer !== "object" || Array.isArray(renderer)) {
        throw new Error(`${metadataName} must be an object`);
    }
    if (
        typeof renderer.name !== "string" ||
        path.basename(renderer.name) !== renderer.name
    ) {
        throw new Error(`${metadataName}.name must be a filename`);
    }
    if (typeof renderer.version !== "string" || !renderer.version) {
        throw new Error(`${metadataName}.version must be a non-empty string`);
    }
    if (!/^[0-9a-f]{64}$/i.test(renderer.sha256hash ?? "")) {
        throw new Error(
            `${metadataName}.sha256hash must be a SHA-256 checksum`,
        );
    }
    if (
        !Array.isArray(renderer.host_architectures) ||
        renderer.host_architectures.length === 0 ||
        renderer.host_architectures.some(
            (architecture) => !["x86_64", "aarch64"].includes(architecture),
        )
    ) {
        throw new Error(
            `${metadataName}.host_architectures must contain supported native host names`,
        );
    }
    if (
        renderer.architectures !== undefined &&
        (!Array.isArray(renderer.architectures) ||
            renderer.architectures.length === 0 ||
            !renderer.architectures.includes("64") ||
            renderer.architectures.some(
                (architecture) => !["64", "32"].includes(architecture),
            ))
    ) {
        throw new Error(
            `${metadataName}.architectures must contain 64 and optional 32`,
        );
    }

    const archivePath = path.join(packageDirectory, "bin", renderer.name);
    const archiveChecksum = createHash("sha256")
        .update(readFileSync(archivePath))
        .digest("hex");
    if (archiveChecksum !== renderer.sha256hash.toLowerCase()) {
        throw new Error(
            `${metadataName} checksum does not match embedded archive ${renderer.name}`,
        );
    }
}

function requireHttpsUrl(value, metadataName) {
    let parsedUrl;
    try {
        parsedUrl = new URL(value);
    } catch {
        throw new Error(`${metadataName} must be a valid HTTPS URL`);
    }
    if (parsedUrl.protocol !== "https:") {
        throw new Error(`${metadataName} must be a valid HTTPS URL`);
    }
}

if (mode === "local") {
    if (owns("remote_binary")) {
        throw new Error(
            "A self-contained local package must not define remote_binary; Decky Loader always downloads it",
        );
    }
    if (owns("remote_binary_bundling")) {
        throw new Error(
            "A self-contained local package must not advertise remote_binary_bundling",
        );
    }
    if (!owns("bundled_renderer")) {
        throw new Error(
            "A self-contained local package must define bundled_renderer",
        );
    }
    if (
        Object.prototype.hasOwnProperty.call(manifest.bundled_renderer, "url")
    ) {
        throw new Error("bundled_renderer must not define a download URL");
    }
    if (
        Object.prototype.hasOwnProperty.call(
            manifest.bundled_renderer,
            "flatpak_bundle",
        )
    ) {
        throw new Error(
            "bundled_renderer must not define remote Flatpak metadata",
        );
    }
    validateRendererMetadata(manifest.bundled_renderer, "bundled_renderer");
} else {
    if (owns("bundled_renderer")) {
        throw new Error("A release package must not define bundled_renderer");
    }
    if (manifest.remote_binary_bundling !== true) {
        throw new Error("A release package must enable remote_binary_bundling");
    }
    if (
        !Array.isArray(manifest.remote_binary) ||
        manifest.remote_binary.length !== 1
    ) {
        throw new Error(
            "A release package must define exactly one remote_binary entry",
        );
    }
    const renderer = manifest.remote_binary[0];
    requireHttpsUrl(renderer.url, "remote_binary.url");
    if (renderer.flatpak_bundle !== undefined) {
        const flatpak = renderer.flatpak_bundle;
        if (!flatpak || typeof flatpak !== "object" || Array.isArray(flatpak)) {
            throw new Error("remote_binary.flatpak_bundle must be an object");
        }
        if (
            typeof flatpak.name !== "string" ||
            path.basename(flatpak.name) !== flatpak.name
        ) {
            throw new Error(
                "remote_binary.flatpak_bundle.name must be a filename",
            );
        }
        if (!/^[0-9a-f]{64}$/i.test(flatpak.sha256hash ?? "")) {
            throw new Error(
                "remote_binary.flatpak_bundle.sha256hash must be a SHA-256 checksum",
            );
        }
        requireHttpsUrl(flatpak.url, "remote_binary.flatpak_bundle.url");
    }
    validateRendererMetadata(renderer, "remote_binary[0]");
}

console.log(`Validated ${mode} Decky package contract: ${manifestPath}`);
