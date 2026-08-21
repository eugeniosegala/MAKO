import assert from "node:assert/strict";
import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { auditI18n } from "../scripts/i18n-contract.mjs";

const validSources = {
  "template.json": {
    HELLO: "Hello {name}",
    PLAIN: "Plain",
  },
  "ja.json": {
    HELLO: "こんにちは {name}",
    PLAIN: "標準",
  },
  "language_metadata.json": {
    en: { name: "English" },
    ja: { name: "日本語" },
  },
  "steam_language_map.json": {
    english: "en",
    japanese: "ja",
  },
};

const validCallSites = [
  'import translate from "./i18n/i18n";',
  'translate("HELLO", "Hello {name}", { name: "MAKO" });',
  'translate("PLAIN", "Plain");',
  "",
].join("\n");

const withFixture = async (mutate, assertion, callSites = validCallSites) => {
  const projectDirectory = await mkdtemp(
    path.join(os.tmpdir(), "mako-i18n-contract-"),
  );
  try {
    const sources = structuredClone(validSources);
    mutate?.(sources);
    const translationsDirectory = path.join(
      projectDirectory,
      "defaults",
      "i18n",
    );
    const sourceDirectory = path.join(projectDirectory, "src");
    await mkdir(translationsDirectory, { recursive: true });
    await mkdir(sourceDirectory, { recursive: true });
    await Promise.all(
      Object.entries(sources).map(([file, value]) =>
        writeFile(
          path.join(translationsDirectory, file),
          `${JSON.stringify(value, null, 2)}\n`,
          "utf8",
        ),
      ),
    );
    await writeFile(path.join(sourceDirectory, "view.ts"), callSites, "utf8");
    await assertion(projectDirectory);
  } finally {
    await rm(projectDirectory, { recursive: true, force: true });
  }
};

test("accepts ordered string dictionaries and static call sites", async () => {
  await withFixture(undefined, async (projectDirectory) => {
    const result = await auditI18n(projectDirectory);
    assert.equal(result.translationCalls.length, 2);
    assert.deepEqual(Object.keys(result.translations.ja), ["HELLO", "PLAIN"]);
  });
});

test("rejects a translated dictionary with a different key order", async () => {
  await withFixture(
    (sources) => {
      sources["ja.json"] = {
        PLAIN: "標準",
        HELLO: "こんにちは {name}",
      };
    },
    async (projectDirectory) => {
      await assert.rejects(auditI18n(projectDirectory), /order=false/);
    },
  );
});

test("rejects non-string values and mismatched placeholders", async () => {
  await withFixture(
    (sources) => {
      sources["ja.json"].HELLO = 42;
    },
    async (projectDirectory) => {
      await assert.rejects(auditI18n(projectDirectory), /values=\[HELLO\]/);
    },
  );
  await withFixture(
    (sources) => {
      sources["ja.json"].HELLO = "こんにちは {profile}";
    },
    async (projectDirectory) => {
      await assert.rejects(
        auditI18n(projectDirectory),
        /placeholders=\[HELLO\]/,
      );
    },
  );
});

test("rejects metadata without a corresponding dictionary", async () => {
  await withFixture(
    (sources) => {
      sources["language_metadata.json"].ko = { name: "한국어" };
    },
    async (projectDirectory) => {
      await assert.rejects(auditI18n(projectDirectory), /missing=\[ko\]/);
    },
  );
});

test("rejects call-site fallback and replacement drift", async () => {
  await withFixture(
    undefined,
    async (projectDirectory) => {
      await assert.rejects(auditI18n(projectDirectory), /fallbacks=/);
    },
    validCallSites.replace("Hello {name}", "Hi {name}"),
  );
  await withFixture(
    undefined,
    async (projectDirectory) => {
      await assert.rejects(auditI18n(projectDirectory), /replacements=/);
    },
    validCallSites.replace('{ name: "MAKO" }', '{ profile: "MAKO" }'),
  );
});
