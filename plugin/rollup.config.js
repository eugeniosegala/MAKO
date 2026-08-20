import deckyPlugin from "@decky/rollup";
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const projectDirectory = path.dirname(fileURLToPath(import.meta.url));
const packageManifest = JSON.parse(
  readFileSync(path.join(projectDirectory, "package.json"), "utf8")
);
const releaseNotes = readFileSync(
  path.join(projectDirectory, "RELEASE_NOTES.md"),
  "utf8"
).replace(/\r\n/g, "\n");
const expectedHeading = `## What's new in MAKO Decky v${packageManifest.version}`;
const firstContentLine = releaseNotes
  .split("\n")
  .find((line) => line.trim())
  ?.trim();
const releaseCodename = releaseNotes.match(
  /^### Release codename:\s*(.+?)\s*$/m
)?.[1];

if (firstContentLine !== expectedHeading) {
  throw new Error(
    `MAKO Decky release notes must start with '${expectedHeading}'`
  );
}
if (!releaseCodename) {
  throw new Error(
    "MAKO Decky release notes must define '### Release codename: <name>'"
  );
}

const releaseInfoModule = "virtual:mako-release-info";
const resolvedReleaseInfoModule = `\0${releaseInfoModule}`;

const releaseInfoPlugin = {
  name: "mako-release-info",
  resolveId(source) {
    return source === releaseInfoModule ? resolvedReleaseInfoModule : null;
  },
  load(id) {
    if (id !== resolvedReleaseInfoModule) return null;
    return `export const currentRelease = ${JSON.stringify({
      version: packageManifest.version,
      codename: releaseCodename
    })};`;
  }
};

export default deckyPlugin({
  plugins: [releaseInfoPlugin]
});
