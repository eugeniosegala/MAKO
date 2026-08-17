import { readdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { rollup } from "rollup";
import ts from "typescript";
import config from "../rollup.config.js";

const listFiles = async (directory) => (
  await Promise.all(
    (await readdir(directory, { withFileTypes: true })).map((entry) => {
      const entryPath = path.join(directory, entry.name);
      return entry.isDirectory() ? listFiles(entryPath) : entryPath;
    })
  )
).flat();

const placeholders = (value) => [
  ...new Set([...value.matchAll(/\{([a-z_]+)\}/g)].map((match) => match[1]))
].sort();

try {
  const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
  const projectDirectory = path.resolve(scriptDirectory, "..");
  const translationsDirectory = path.join(projectDirectory, "defaults", "i18n");
  const generatedTranslationsPath = path.join(projectDirectory, "src", "i18n", "languages.json");
  const generatedDevBuildInfoPath = path.join(projectDirectory, "src", "config", "devBuildInfo.generated.ts");
  const developmentBuildInfoPath = process.env.MAKO_DEV_BUILD_INFO_PATH;
  let developmentBuildInfo = null;
  if (developmentBuildInfoPath) {
    developmentBuildInfo = JSON.parse(await readFile(developmentBuildInfoPath, "utf8"));
  }
  await writeFile(
    generatedDevBuildInfoPath,
    "import type { LocalDevelopmentBuildInfo } from \"./devBuildInfo\";\n\n" +
    "export const localDevelopmentBuildInfo: LocalDevelopmentBuildInfo | null = " +
    `${JSON.stringify(developmentBuildInfo, null, 2)};\n`,
    "utf8"
  );
  const translationFiles = (await readdir(translationsDirectory))
    .filter((file) => file.endsWith(".json"))
    .sort();
  const translations = {};

  for (const file of translationFiles) {
    const language = path.basename(file, ".json");
    translations[language] = JSON.parse(
      await readFile(path.join(translationsDirectory, file), "utf8")
    );
  }

  const template = translations.template;
  if (!template || typeof template !== "object" || Array.isArray(template)) {
    throw new Error("defaults/i18n/template.json must contain a translation object");
  }

  const metadataFiles = new Set(["language_metadata", "steam_language_map", "template"]);
  for (const [language, strings] of Object.entries(translations)) {
    if (metadataFiles.has(language)) continue;

    const missing = Object.keys(template).filter((key) => !(key in strings));
    const extra = Object.keys(strings).filter((key) => !(key in template));
    const placeholderMismatches = Object.keys(template).filter(
      (key) => String(placeholders(template[key])) !== String(placeholders(strings[key] ?? ""))
    );
    if (missing.length || extra.length || placeholderMismatches.length) {
      throw new Error(
        `${language} translation mismatch: missing=[${missing.join(", ")}], ` +
        `extra=[${extra.join(", ")}], placeholders=[${placeholderMismatches.join(", ")}]`
      );
    }
  }

  const translationCalls = [];
  const sourceFiles = (await listFiles(path.join(projectDirectory, "src")))
    .filter((file) => file.endsWith(".ts") || file.endsWith(".tsx"));
  for (const file of sourceFiles) {
    const sourceText = await readFile(file, "utf8");
    const sourceFile = ts.createSourceFile(
      file,
      sourceText,
      ts.ScriptTarget.Latest,
      true,
      file.endsWith(".tsx") ? ts.ScriptKind.TSX : ts.ScriptKind.TS
    );
    const inspect = (node) => {
      if (
        ts.isCallExpression(node)
        && ts.isIdentifier(node.expression)
        && node.expression.text === "t"
      ) {
        const [keyNode, fallbackNode, replacementsNode] = node.arguments;
        if (!ts.isStringLiteralLike(keyNode) || !ts.isStringLiteralLike(fallbackNode)) {
          const { line } = sourceFile.getLineAndCharacterOfPosition(node.getStart(sourceFile));
          throw new Error(`${path.relative(projectDirectory, file)}:${line + 1} uses a non-static translation key or fallback`);
        }
        const expectedReplacements = placeholders(fallbackNode.text);
        let suppliedReplacements = [];
        if (replacementsNode && ts.isObjectLiteralExpression(replacementsNode)) {
          suppliedReplacements = replacementsNode.properties.flatMap((property) => {
            if (ts.isPropertyAssignment(property) || ts.isShorthandPropertyAssignment(property)) {
              return [property.name.getText(sourceFile).replace(/^['"]|['"]$/g, "")];
            }
            return [];
          }).sort();
        }
        translationCalls.push({
          key: keyNode.text,
          fallback: fallbackNode.text,
          expectedReplacements,
          suppliedReplacements,
          file: path.relative(projectDirectory, file),
          line: sourceFile.getLineAndCharacterOfPosition(node.getStart(sourceFile)).line + 1
        });
      }
      ts.forEachChild(node, inspect);
    };
    inspect(sourceFile);
  }

  const missingKeys = translationCalls.filter(({ key }) => !(key in template));
  const fallbackMismatches = translationCalls.filter(
    ({ key, fallback }) => key in template && template[key] !== fallback
  );
  const replacementMismatches = translationCalls.filter(
    ({ expectedReplacements, suppliedReplacements }) =>
      String(expectedReplacements) !== String(suppliedReplacements)
  );
  const unusedKeys = Object.keys(template).filter(
    (key) => !translationCalls.some((call) => call.key === key)
  );
  if (missingKeys.length || fallbackMismatches.length || replacementMismatches.length || unusedKeys.length) {
    const location = ({ file, line, key }) => `${file}:${line} (${key})`;
    throw new Error(
      `Translation source mismatch: missing=[${missingKeys.map(location).join(", ")}], ` +
      `fallbacks=[${fallbackMismatches.map(location).join(", ")}], ` +
      `replacements=[${replacementMismatches.map(location).join(", ")}], ` +
      `unused=[${unusedKeys.join(", ")}]`
    );
  }

  await writeFile(
    generatedTranslationsPath,
    `${JSON.stringify(translations, null, 2)}\n`,
    "utf8"
  );

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
