#!/usr/bin/env node

import { createHash } from "node:crypto";
import { createReadStream, mkdtempSync, rmSync, statSync } from "node:fs";
import { basename, join } from "node:path";
import { spawnSync } from "node:child_process";
import { tmpdir } from "node:os";

function usage() {
    console.error(
        "Usage: node scripts/upload-release-assets.mjs OWNER/REPOSITORY TAG ASSET_PATH...",
    );
}

function runGh(arguments_) {
    const result = spawnSync("gh", arguments_, {
        encoding: "utf8",
        stdio: ["ignore", "pipe", "pipe"],
    });
    if (result.error) {
        throw result.error;
    }
    if (result.status !== 0) {
        const detail = result.stderr.trim() || result.stdout.trim();
        throw new Error(
            `gh ${arguments_.join(" ")} failed${detail ? `: ${detail}` : ""}`,
        );
    }
    return result.stdout;
}

function sha256(path) {
    return new Promise((resolve, reject) => {
        const hash = createHash("sha256");
        const input = createReadStream(path);
        input.on("error", reject);
        input.on("data", (chunk) => hash.update(chunk));
        input.on("end", () => resolve(hash.digest("hex")));
    });
}

async function main() {
    const [repository, tag, ...assetPaths] = process.argv.slice(2);
    if (!repository || !tag || assetPaths.length === 0) {
        usage();
        process.exit(2);
    }

    const localAssets = [];
    const localNames = new Set();
    for (const path of assetPaths) {
        let status;
        try {
            status = statSync(path);
        } catch {
            throw new Error(`Release asset does not exist: ${path}`);
        }
        if (!status.isFile()) {
            throw new Error(`Release asset is not a regular file: ${path}`);
        }
        const name = basename(path);
        if (localNames.has(name)) {
            throw new Error(`Duplicate release asset name: ${name}`);
        }
        localNames.add(name);
        localAssets.push({ name, path, checksum: await sha256(path) });
    }

    let release;
    try {
        release = JSON.parse(
            runGh([
                "release",
                "view",
                tag,
                "--repo",
                repository,
                "--json",
                "assets",
            ]),
        );
    } catch (error) {
        throw new Error(`Could not inspect release ${tag}: ${error.message}`);
    }

    const remoteAssets = new Map();
    for (const asset of release.assets ?? []) {
        if (remoteAssets.has(asset.name)) {
            throw new Error(
                `Release ${tag} contains duplicate asset name: ${asset.name}`,
            );
        }
        remoteAssets.set(asset.name, asset);
    }

    const verificationDirectories = [];
    const missingPaths = [];
    try {
        for (const local of localAssets) {
            const remote = remoteAssets.get(local.name);
            if (!remote) {
                missingPaths.push(local.path);
                continue;
            }

            let remoteChecksum = null;
            const digestMatch = /^sha256:([0-9a-f]{64})$/i.exec(
                remote.digest ?? "",
            );
            if (digestMatch) {
                remoteChecksum = digestMatch[1].toLowerCase();
            } else {
                const verificationDirectory = mkdtempSync(
                    join(tmpdir(), "mako-release-verify-"),
                );
                verificationDirectories.push(verificationDirectory);
                runGh([
                    "release",
                    "download",
                    tag,
                    "--repo",
                    repository,
                    "--pattern",
                    local.name,
                    "--dir",
                    verificationDirectory,
                ]);
                remoteChecksum = await sha256(
                    join(verificationDirectory, local.name),
                );
            }

            if (remoteChecksum !== local.checksum) {
                throw new Error(
                    `Published release asset ${local.name} differs from the candidate; published assets are immutable, so bump the release version instead of replacing it.`,
                );
            }
        }

        if (missingPaths.length > 0) {
            runGh([
                "release",
                "upload",
                tag,
                ...missingPaths,
                "--repo",
                repository,
            ]);
            for (const path of missingPaths) {
                console.log(
                    `Uploaded missing release asset: ${basename(path)}`,
                );
            }
        } else {
            console.log(
                "Every existing release asset matches the candidate; nothing to upload.",
            );
        }
    } finally {
        for (const directory of verificationDirectories) {
            rmSync(directory, { recursive: true, force: true });
        }
    }
}

main().catch((error) => {
    console.error(`Release asset publication failed: ${error.message}`);
    process.exitCode = 1;
});
