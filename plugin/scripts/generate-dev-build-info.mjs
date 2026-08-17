import { createHash } from "node:crypto";
import { execFile } from "node:child_process";
import { readFile, stat, writeFile } from "node:fs/promises";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

const usage = () => {
  console.error(
    "Usage: generate-dev-build-info.mjs --output PATH --plugin-repo PATH " +
    "--frontend-deployed true|false --backend-deployed true|false " +
    "[--engine-repo PATH --engine-layer-64 PATH --engine-layer-32 PATH " +
    "--flatpak-archive PATH]"
  );
};

const options = new Map();
for (let index = 2; index < process.argv.length; index += 2) {
  const key = process.argv[index];
  const value = process.argv[index + 1];
  if (!key?.startsWith("--") || value === undefined || options.has(key)) {
    usage();
    process.exit(2);
  }
  options.set(key, value);
}

const required = (key) => {
  const value = options.get(key);
  if (!value) {
    usage();
    process.exit(2);
  }
  return value;
};

const booleanOption = (key) => {
  const value = required(key);
  if (value !== "true" && value !== "false") {
    console.error(`${key} must be true or false`);
    process.exit(2);
  }
  return value === "true";
};

const git = async (repository, args) => {
  const { stdout } = await execFileAsync("git", ["-C", repository, ...args]);
  return stdout.trim();
};

const gitInfo = async (repository) => ({
  commit: await git(repository, ["rev-parse", "--short=8", "HEAD"]),
  dirty: Boolean(await git(repository, [
    "status", "--porcelain", "--untracked-files=normal", "--", "."
  ]))
});

const outputPath = required("--output");
const pluginRepository = required("--plugin-repo");
const engineRepository = options.get("--engine-repo");
const engineLayer64 = options.get("--engine-layer-64");
const engineLayer32 = options.get("--engine-layer-32");
const flatpakArchive = options.get("--flatpak-archive");
const hasEngineArtifact = Boolean(engineLayer64 || engineLayer32 || flatpakArchive);
if (Boolean(engineRepository) !== hasEngineArtifact) {
  console.error("--engine-repo must be supplied when an engine artifact is supplied");
  process.exit(2);
}

const plugin = {
  ...(await gitInfo(pluginRepository)),
  frontendDeployed: booleanOption("--frontend-deployed"),
  backendDeployed: booleanOption("--backend-deployed")
};

let engine = null;
if (engineRepository) {
  const sha256 = async (artifactPath) => {
    if (!artifactPath) return null;
    await stat(artifactPath);
    return createHash("sha256").update(await readFile(artifactPath)).digest("hex");
  };
  engine = {
    ...(await gitInfo(engineRepository)),
    layer64Sha256: await sha256(engineLayer64),
    layer32Sha256: await sha256(engineLayer32),
    flatpakArchiveSha256: await sha256(flatpakArchive)
  };
}

await writeFile(outputPath, `${JSON.stringify({
  generatedAt: new Date().toISOString(),
  plugin,
  engine
}, null, 2)}\n`);
