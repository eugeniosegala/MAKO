#!/usr/bin/env node

import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const escapeRegExp = (value) => value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");

export function readReleaseInfo(notesPath, component) {
    if (!notesPath || !component) {
        throw new Error("A release-notes path and component name are required");
    }

    const absolutePath = path.resolve(notesPath);
    const notes = readFileSync(absolutePath, "utf8").replace(/\r\n/g, "\n");
    const firstContentLine = notes
        .split("\n")
        .find((line) => line.trim())
        ?.trim();
    const headingMatch = firstContentLine?.match(
        new RegExp(
            `^## What's new in ${escapeRegExp(component)} v([0-9]+\\.[0-9]+\\.[0-9]+)$`,
        ),
    );
    const codename = notes.match(/^### Release codename:\s*(.+?)\s*$/m)?.[1];

    if (!headingMatch) {
        throw new Error(
            `${absolutePath} must start with "## What's new in ${component} v<X.Y.Z>"`,
        );
    }
    if (!codename) {
        throw new Error(
            `${absolutePath} must define "### Release codename: <name>"`,
        );
    }

    return { version: headingMatch[1], codename };
}

const invokedPath = process.argv[1] ? path.resolve(process.argv[1]) : "";
if (invokedPath === fileURLToPath(import.meta.url)) {
    const [notesPath, component, field] = process.argv.slice(2);
    if (
        !notesPath ||
        !component ||
        ![undefined, "--version", "--codename"].includes(field)
    ) {
        console.error(
            "Usage: read-release-info.mjs <notes.md> <component-name> [--version|--codename]",
        );
        process.exit(2);
    }

    try {
        const releaseInfo = readReleaseInfo(notesPath, component);
        if (field === "--version") {
            process.stdout.write(`${releaseInfo.version}\n`);
        } else if (field === "--codename") {
            process.stdout.write(`${releaseInfo.codename}\n`);
        } else {
            process.stdout.write(`${JSON.stringify(releaseInfo)}\n`);
        }
    } catch (error) {
        console.error(`Release information error: ${error.message}`);
        process.exit(1);
    }
}
