import deckyPlugin from "@decky/rollup";
import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { readReleaseInfo } from "./scripts/read-release-info.mjs";

const projectDirectory = path.dirname(fileURLToPath(import.meta.url));
const packageManifest = JSON.parse(
  readFileSync(path.join(projectDirectory, "package.json"), "utf8"),
);
const releaseInfo = readReleaseInfo(
  path.join(projectDirectory, "RELEASE_NOTES.md"),
  "MAKO Decky",
);
const localReleaseBuild = process.env.MAKO_LOCAL_RELEASE_BUILD;

if (localReleaseBuild !== undefined && localReleaseBuild !== "1") {
  throw new Error("MAKO_LOCAL_RELEASE_BUILD must be '1' when set");
}
if (!localReleaseBuild && releaseInfo.version !== packageManifest.version) {
  const expectedHeading = `## What's new in MAKO Decky v${packageManifest.version}`;
  throw new Error(
    `MAKO Decky release notes must start with '${expectedHeading}'`,
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
      version: localReleaseBuild
        ? releaseInfo.version
        : packageManifest.version,
      codename: releaseInfo.codename,
    })};`;
  },
};

export default deckyPlugin({
  plugins: [releaseInfoPlugin],
});
