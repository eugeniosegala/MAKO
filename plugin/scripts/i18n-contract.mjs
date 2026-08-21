import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import ts from "typescript";

const metadataFileNames = new Set([
  "language_metadata",
  "steam_language_map",
  "template",
]);

const isRecord = (value) =>
  value !== null && typeof value === "object" && !Array.isArray(value);

const listFiles = async (directory) =>
  (
    await Promise.all(
      (await readdir(directory, { withFileTypes: true })).map((entry) => {
        const entryPath = path.join(directory, entry.name);
        return entry.isDirectory() ? listFiles(entryPath) : entryPath;
      }),
    )
  ).flat();

const placeholders = (value) =>
  [
    ...new Set(
      [...value.matchAll(/\{([a-z][a-z0-9_]*)\}/g)].map((match) => match[1]),
    ),
  ].sort();

const invalidPlaceholderSyntax = (value) =>
  /[{}]/.test(value.replaceAll(/\{[a-z][a-z0-9_]*\}/g, ""));

const sameOrderedValues = (left, right) =>
  left.length === right.length &&
  left.every((value, index) => value === right[index]);

const sourceLocation = (projectDirectory, sourceFile, node) => {
  const { line } = sourceFile.getLineAndCharacterOfPosition(
    node.getStart(sourceFile),
  );
  return `${path.relative(projectDirectory, sourceFile.fileName)}:${line + 1}`;
};

const validateTranslationCalls = async (projectDirectory, template) => {
  const translationCalls = [];
  const invalidCalls = [];
  const sourceFiles = (await listFiles(path.join(projectDirectory, "src")))
    .filter((file) => file.endsWith(".ts") || file.endsWith(".tsx"))
    .sort();

  for (const file of sourceFiles) {
    const sourceText = await readFile(file, "utf8");
    const sourceFile = ts.createSourceFile(
      file,
      sourceText,
      ts.ScriptTarget.Latest,
      true,
      file.endsWith(".tsx") ? ts.ScriptKind.TSX : ts.ScriptKind.TS,
    );
    const translationIdentifiers = new Set();

    for (const statement of sourceFile.statements) {
      if (
        ts.isImportDeclaration(statement) &&
        ts.isStringLiteral(statement.moduleSpecifier) &&
        statement.moduleSpecifier.text.endsWith("/i18n/i18n") &&
        statement.importClause?.name
      ) {
        translationIdentifiers.add(statement.importClause.name.text);
      }
    }

    const inspect = (node) => {
      if (
        ts.isCallExpression(node) &&
        ts.isIdentifier(node.expression) &&
        translationIdentifiers.has(node.expression.text)
      ) {
        const [keyNode, fallbackNode, replacementsNode] = node.arguments;
        const location = sourceLocation(projectDirectory, sourceFile, node);
        if (
          node.arguments.length < 2 ||
          node.arguments.length > 3 ||
          !ts.isStringLiteralLike(keyNode) ||
          !ts.isStringLiteralLike(fallbackNode)
        ) {
          invalidCalls.push(
            `${location} uses non-static translation arguments`,
          );
        } else {
          const expectedReplacements = placeholders(fallbackNode.text);
          let suppliedReplacements = [];
          let replacementsAreStatic = true;

          if (replacementsNode) {
            if (!ts.isObjectLiteralExpression(replacementsNode)) {
              replacementsAreStatic = false;
            } else {
              suppliedReplacements = replacementsNode.properties.flatMap(
                (property) => {
                  if (
                    (ts.isPropertyAssignment(property) ||
                      ts.isShorthandPropertyAssignment(property)) &&
                    !property.name.getText(sourceFile).startsWith("[")
                  ) {
                    return [
                      property.name
                        .getText(sourceFile)
                        .replace(/^['"]|['"]$/g, ""),
                    ];
                  }
                  replacementsAreStatic = false;
                  return [];
                },
              );
            }
          }

          if (!replacementsAreStatic) {
            invalidCalls.push(
              `${location} (${keyNode.text}) uses non-static replacement fields`,
            );
          }

          translationCalls.push({
            key: keyNode.text,
            fallback: fallbackNode.text,
            expectedReplacements,
            suppliedReplacements: suppliedReplacements.sort(),
            file: path.relative(projectDirectory, file),
            line:
              sourceFile.getLineAndCharacterOfPosition(
                node.getStart(sourceFile),
              ).line + 1,
          });
        }
      }
      ts.forEachChild(node, inspect);
    };
    inspect(sourceFile);
  }

  const missingKeys = translationCalls.filter(({ key }) => !(key in template));
  const fallbackMismatches = translationCalls.filter(
    ({ key, fallback }) => key in template && template[key] !== fallback,
  );
  const replacementMismatches = translationCalls.filter(
    ({ expectedReplacements, suppliedReplacements }) =>
      !sameOrderedValues(expectedReplacements, suppliedReplacements),
  );
  const unusedKeys = Object.keys(template).filter(
    (key) => !translationCalls.some((call) => call.key === key),
  );
  if (
    invalidCalls.length ||
    missingKeys.length ||
    fallbackMismatches.length ||
    replacementMismatches.length ||
    unusedKeys.length
  ) {
    const location = ({ file, line, key }) => `${file}:${line} (${key})`;
    throw new Error(
      `Translation source mismatch: invalid=[${invalidCalls.join(", ")}], ` +
        `missing=[${missingKeys.map(location).join(", ")}], ` +
        `fallbacks=[${fallbackMismatches.map(location).join(", ")}], ` +
        `replacements=[${replacementMismatches.map(location).join(", ")}], ` +
        `unused=[${unusedKeys.join(", ")}]`,
    );
  }

  return translationCalls;
};

export const auditI18n = async (projectDirectory) => {
  const translationsDirectory = path.join(projectDirectory, "defaults", "i18n");
  const translationFiles = (await readdir(translationsDirectory))
    .filter((file) => file.endsWith(".json"))
    .sort();
  const translations = {};

  for (const file of translationFiles) {
    const language = path.basename(file, ".json");
    const value = JSON.parse(
      await readFile(path.join(translationsDirectory, file), "utf8"),
    );
    if (!isRecord(value)) {
      throw new Error(`defaults/i18n/${file} must contain a JSON object`);
    }
    translations[language] = value;
  }

  const template = translations.template;
  const languageMetadata = translations.language_metadata;
  const steamLanguageMap = translations.steam_language_map;
  if (!template) {
    throw new Error("defaults/i18n/template.json is required");
  }
  if (!languageMetadata) {
    throw new Error("defaults/i18n/language_metadata.json is required");
  }
  if (!steamLanguageMap) {
    throw new Error("defaults/i18n/steam_language_map.json is required");
  }

  const invalidTemplateKeys = Object.keys(template).filter(
    (key) => !/^[A-Z][A-Z0-9_]*$/.test(key),
  );
  const invalidTemplateValues = Object.entries(template)
    .filter(
      ([, value]) =>
        typeof value !== "string" || invalidPlaceholderSyntax(value),
    )
    .map(([key]) => key);
  if (invalidTemplateKeys.length || invalidTemplateValues.length) {
    throw new Error(
      `Template structure mismatch: keys=[${invalidTemplateKeys.join(", ")}], ` +
        `values=[${invalidTemplateValues.join(", ")}]`,
    );
  }

  const invalidMetadata = Object.entries(languageMetadata)
    .filter(
      ([language, metadata]) =>
        !/^[a-z]{2,3}(?:-[A-Z]{2})?$/.test(language) ||
        !isRecord(metadata) ||
        !sameOrderedValues(Object.keys(metadata), ["name"]) ||
        typeof metadata.name !== "string" ||
        metadata.name.trim() === "",
    )
    .map(([language]) => language);
  if (!("en" in languageMetadata) || invalidMetadata.length) {
    throw new Error(
      `Language metadata mismatch: missingEnglish=${!("en" in languageMetadata)}, ` +
        `invalid=[${invalidMetadata.join(", ")}]`,
    );
  }

  const invalidSteamMappings = Object.entries(steamLanguageMap)
    .filter(
      ([source, target]) =>
        source !== source.trim().toLowerCase().replaceAll("_", "-") ||
        typeof target !== "string" ||
        target === "" ||
        target !== target.trim().replaceAll("_", "-") ||
        !(target in languageMetadata),
    )
    .map(([source]) => source);
  if (invalidSteamMappings.length) {
    throw new Error(
      `Steam language map mismatch: invalid=[${invalidSteamMappings.join(", ")}]`,
    );
  }

  const sourceLanguages = Object.keys(translations).filter(
    (language) => !metadataFileNames.has(language),
  );
  const advertisedLanguages = Object.keys(languageMetadata).filter(
    (language) => language !== "en",
  );
  const advertisedWithoutStrings = advertisedLanguages.filter(
    (language) => !sourceLanguages.includes(language),
  );
  const stringsWithoutMetadata = sourceLanguages.filter(
    (language) => !(language in languageMetadata),
  );
  if (sourceLanguages.includes("en")) {
    stringsWithoutMetadata.push(
      "en (English strings belong in template.json, not en.json)",
    );
  }
  if (advertisedWithoutStrings.length || stringsWithoutMetadata.length) {
    throw new Error(
      `Language source mismatch: missing=[${advertisedWithoutStrings.join(", ")}], ` +
        `unadvertised=[${stringsWithoutMetadata.join(", ")}]`,
    );
  }

  const templateKeys = Object.keys(template);
  for (const language of sourceLanguages) {
    const strings = translations[language];
    const languageKeys = Object.keys(strings);
    const missing = templateKeys.filter((key) => !(key in strings));
    const extra = languageKeys.filter((key) => !(key in template));
    const invalidValues = Object.entries(strings)
      .filter(
        ([, value]) =>
          typeof value !== "string" || invalidPlaceholderSyntax(value),
      )
      .map(([key]) => key);
    const orderMatches = sameOrderedValues(templateKeys, languageKeys);
    const placeholderMismatches = templateKeys.filter(
      (key) =>
        typeof strings[key] === "string" &&
        !sameOrderedValues(
          placeholders(template[key]),
          placeholders(strings[key]),
        ),
    );
    if (
      missing.length ||
      extra.length ||
      invalidValues.length ||
      !orderMatches ||
      placeholderMismatches.length
    ) {
      throw new Error(
        `${language} translation mismatch: missing=[${missing.join(", ")}], ` +
          `extra=[${extra.join(", ")}], values=[${invalidValues.join(", ")}], ` +
          `order=${orderMatches}, placeholders=[${placeholderMismatches.join(", ")}]`,
      );
    }
  }

  const translationCalls = await validateTranslationCalls(
    projectDirectory,
    template,
  );
  return {
    translations,
    translationCalls,
    serializedTranslations: `${JSON.stringify(translations, null, 2)}\n`,
  };
};
