import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { auditI18n } from "./i18n-contract.mjs";

const usage = "Usage: node scripts/manage-i18n.mjs --check|--generate";

try {
  const [mode, ...extraArguments] = process.argv.slice(2);
  if (!new Set(["--check", "--generate"]).has(mode) || extraArguments.length) {
    throw new Error(usage);
  }

  const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
  const projectDirectory = path.resolve(scriptDirectory, "..");
  const generatedTranslationsPath = path.join(
    projectDirectory,
    "src",
    "i18n",
    "languages.json",
  );
  const { serializedTranslations, translationCalls } =
    await auditI18n(projectDirectory);

  if (mode === "--check") {
    const trackedTranslations = await readFile(
      generatedTranslationsPath,
      "utf8",
    );
    if (trackedTranslations !== serializedTranslations) {
      throw new Error(
        "src/i18n/languages.json is stale; run npm run generate:i18n",
      );
    }
    console.log(
      `i18n contract is current (${translationCalls.length} static call sites)`,
    );
  } else {
    await writeFile(generatedTranslationsPath, serializedTranslations, "utf8");
    console.log(
      `Generated src/i18n/languages.json (${translationCalls.length} static call sites)`,
    );
  }
} catch (error) {
  console.error(error);
  process.exitCode = 1;
}
