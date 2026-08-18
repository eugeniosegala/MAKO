#!/usr/bin/env node

import { readFileSync } from "node:fs";
import { resolve } from "node:path";

const fail = (message, status = 1) => {
  console.error(`Release notes error: ${message}`);
  process.exit(status);
};

const [notesPath, component, version] = process.argv.slice(2);
if (!notesPath || !component || !version) {
  fail(
    "Usage: read-release-notes.mjs <notes.md> <component-name> <X.Y.Z>",
    2,
  );
}

if (!/^[0-9]+\.[0-9]+\.[0-9]+$/.test(version)) {
  fail(`Invalid release version: ${version}`, 2);
}

const absolutePath = resolve(notesPath);
let notes;
try {
  notes = readFileSync(absolutePath, "utf8").replace(/\r\n/g, "\n").trim();
} catch (error) {
  fail(`Cannot read ${absolutePath}: ${error.message}`);
}
const lines = notes.split("\n");
const firstContentIndex = lines.findIndex((line) => line.trim() !== "");
const firstContentLine = lines[firstContentIndex]?.trim();
const expectedHeading = `## What's new in ${component} v${version}`;

if (firstContentLine !== expectedHeading) {
  fail(
    `${absolutePath} must start with '${expectedHeading}'. ` +
      "Update and commit its manually curated release notes before publishing.",
  );
}

const meaningfulBody = lines
  .slice(firstContentIndex + 1)
  .join("\n")
  .replace(/<!--[\s\S]*?-->/g, "")
  .trim();
if (!meaningfulBody) {
  fail(`${absolutePath} has a heading but no release-note content.`);
}

process.stdout.write(`${notes}\n`);
