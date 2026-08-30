import assert from "node:assert/strict";
import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { auditI18n } from "../scripts/i18n-contract.mjs";

const pluginDirectory = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "..",
);

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

test("accepts canonical regional language codes and Steam aliases", async () => {
  await withFixture(
    (sources) => {
      sources["pt-BR.json"] = {
        HELLO: "Olá {name}",
        PLAIN: "Simples",
      };
      sources["language_metadata.json"]["pt-BR"] = {
        name: "Português (Brasil)",
      };
      sources["steam_language_map.json"].brazilian = "pt-BR";
    },
    async (projectDirectory) => {
      const result = await auditI18n(projectDirectory);
      assert.equal(result.translations.steam_language_map.brazilian, "pt-BR");
    },
  );
});

test("rejects Steam aliases for unadvertised languages", async () => {
  await withFixture(
    (sources) => {
      sources["steam_language_map.json"].german = "de";
    },
    async (projectDirectory) => {
      await assert.rejects(auditI18n(projectDirectory), /invalid=\[german\]/);
    },
  );
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

test("uses localized restart markers only for process-start controls", async () => {
  const { translations } = await auditI18n(pluginDirectory);
  const restartMarkers = {
    template: "(Restart)",
    es: "(Reiniciar)",
    ja: "（再起動）",
    ko: "(재시작)",
    "pt-BR": "(Reiniciar)",
    "pt-PT": "(Reiniciar)",
    uk: "(перезапуск)",
    zh: "（重启）",
  };
  const processStartKeys = [
    "SCALING_ENABLED",
    "CONFIG_ULTRA_PERFORMANCE",
    "CONFIG_DISABLE_STEAMDECK_MODE",
    "CONFIG_ENABLE_ZINK",
    "CONFIG_FORCE_ALSA_AUDIO",
    "CONFIG_ENABLE_MANGOHUD",
    "CONFIG_ENABLE_VKBASALT",
    "CONFIG_DLL_PATH",
    "CONFIG_GAMESCOPE_WSI_COMPATIBILITY",
    "CONFIG_ALLOW_FP16",
    "CONFIG_GPU",
  ];
  const labelsWithoutRestartMarker = [
    "SCALING_METHOD",
    "SCALING_FACTOR",
    "SCALING_SHARPNESS",
    "CONFIG_FLOW_SCALE",
    "CONFIG_BASE_FPS_CAP",
    "CONFIG_FRAME_GENERATION_REFRESH_GUARD",
    "CONFIG_PERFORMANCE_MODE",
    "CONFIG_DISABLE_MAKO_NEXT_LAUNCH",
    "CONFIG_DISABLE_HDR_EXPOSURE",
    "FRAME_GENERATION_ENABLED",
    "MULTIPLIER_TITLE",
    "ADAPTIVE_TITLE",
    "ADAPTIVE_TARGET_FPS",
    "ADAPTIVE_MAX_MULTIPLIER",
  ];

  for (const [language, marker] of Object.entries(restartMarkers)) {
    for (const key of processStartKeys) {
      assert.ok(
        translations[language][key].includes(marker),
        `${language}.${key} is missing ${marker}`,
      );
    }
    for (const key of labelsWithoutRestartMarker) {
      assert.ok(
        !translations[language][key].includes(marker),
        `${language}.${key} incorrectly includes ${marker}`,
      );
    }
  }
});

test("keeps MAKO Scaler as an untranslated product name", async () => {
  const { translations } = await auditI18n(pluginDirectory);
  for (const [language, catalog] of Object.entries(translations)) {
    if (language === "language_metadata" || language === "steam_language_map") {
      continue;
    }
    assert.equal(
      catalog.SCALING_METHOD_MAKO,
      "MAKO Scaler",
      `${language}.SCALING_METHOD_MAKO changed the product name`,
    );
  }
});
