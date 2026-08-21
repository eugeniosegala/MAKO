import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { rollup } from "rollup";
import config from "../rollup.config.js";
import { auditI18n } from "./i18n-contract.mjs";

try {
  const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
  const projectDirectory = path.resolve(scriptDirectory, "..");
  const generatedTranslationsPath = path.join(
    projectDirectory,
    "src",
    "i18n",
    "languages.json",
  );
  const generatedDevBuildInfoPath = path.join(
    projectDirectory,
    "src",
    "config",
    "devBuildInfo.generated.ts",
  );
  const developmentBuildInfoPath = process.env.MAKO_DEV_BUILD_INFO_PATH;
  let developmentBuildInfo = null;
  if (developmentBuildInfoPath) {
    developmentBuildInfo = JSON.parse(
      await readFile(developmentBuildInfoPath, "utf8"),
    );
  }
  await writeFile(
    generatedDevBuildInfoPath,
    'import type { LocalDevelopmentBuildInfo } from "./devBuildInfo";\n\n' +
      "export const localDevelopmentBuildInfo: LocalDevelopmentBuildInfo | null = " +
      `${JSON.stringify(developmentBuildInfo, null, 2)};\n`,
    "utf8",
  );
  const { serializedTranslations } = await auditI18n(projectDirectory);
  await writeFile(generatedTranslationsPath, serializedTranslations, "utf8");

  const configurations = Array.isArray(config) ? config : [config];

  for (const configuration of configurations) {
    const { output, watch: _watch, ...inputOptions } = configuration;
    const outputs = Array.isArray(output) ? output : [output];
    const bundle = await rollup(inputOptions);

    try {
      for (const outputOptions of outputs) {
        if (!outputOptions) {
          throw new Error("Rollup configuration is missing output options");
        }
        await bundle.write(outputOptions);
      }
    } finally {
      await bundle.close();
    }
  }

  // Some Decky Rollup plugin versions retain background handles after a
  // successful one-shot build. All output has been written and closed here,
  // so exit explicitly instead of leaving package scripts waiting forever.
  process.exit(0);
} catch (error) {
  console.error(error);
  process.exit(1);
}
