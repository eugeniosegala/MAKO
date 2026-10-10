import { afterEach, describe, expect, it } from "vitest";
import t, {
  getCurrentLanguage,
  getLanguageName,
  normalizeLanguage,
} from "../../src/i18n/i18n";

const setSteamLanguage = (language?: string) => {
  if (language === undefined) {
    Reflect.deleteProperty(window, "LocalizationManager");
  } else {
    Object.defineProperty(window, "LocalizationManager", {
      configurable: true,
      value: { m_rgLocalesToUse: [language] },
      writable: true,
    });
  }
};

afterEach(() => setSteamLanguage());

describe("i18n runtime", () => {
  it("normalizes Steam names and locale identifiers", () => {
    expect(normalizeLanguage()).toBe("en");
    expect(normalizeLanguage("Koreana")).toBe("ko");
    expect(normalizeLanguage("schinese")).toBe("zh");
    expect(normalizeLanguage("ja_JP")).toBe("ja");
    expect(normalizeLanguage(" spanish ")).toBe("es");
    expect(normalizeLanguage("brazilian")).toBe("pt-BR");
    expect(normalizeLanguage("pt_BR")).toBe("pt-BR");
    expect(normalizeLanguage("portuguese")).toBe("pt-PT");
    expect(normalizeLanguage("pt-PT")).toBe("pt-PT");
    expect(normalizeLanguage("ukrainian")).toBe("uk");
    expect(normalizeLanguage("uk-UA")).toBe("uk");
    expect(normalizeLanguage(" German ")).toBe("de");
    expect(normalizeLanguage("de_DE")).toBe("de");
    expect(normalizeLanguage("de-AT")).toBe("de");
    expect(normalizeLanguage("de-CH")).toBe("de");
    expect(normalizeLanguage(" Russian ")).toBe("ru");
    expect(normalizeLanguage("ru_RU")).toBe("ru");
    expect(normalizeLanguage("ru-BY")).toBe("ru");
  });

  it("reports localized language names with a normalized fallback", () => {
    expect(getLanguageName("koreana")).toBe("한국어");
    expect(getLanguageName("spanish")).toBe("Español");
    expect(getLanguageName("brazilian")).toBe("Português (Brasil)");
    expect(getLanguageName("ukrainian")).toBe("Українська");
    expect(getLanguageName("german")).toBe("Deutsch");
    expect(getLanguageName("russian")).toBe("Русский");
  });

  it("uses the selected dictionary and replaces named placeholders", () => {
    setSteamLanguage("japanese");
    expect(getCurrentLanguage()).toBe("ja");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation")).toBe(
      "フレーム生成",
    );
    expect(
      t("FLATPAK_RUNTIME_VERSION", "Runtime {version}", {
        version: "24.08",
      }),
    ).toBe("ランタイム 24.08");
  });

  it("uses translated regional dictionaries without collapsing their locale", () => {
    setSteamLanguage("brazilian");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation")).toBe(
      "Geração de quadros",
    );

    setSteamLanguage("portuguese");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation")).toBe(
      "Geração de fotogramas",
    );

    setSteamLanguage("spanish");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation")).toBe(
      "Generación de cuadros",
    );

    setSteamLanguage("ukrainian");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation")).toBe(
      "Генерація кадрів",
    );
  });

  it.each(["german", "de_DE", "de-AT", "de-CH"])(
    "uses German translations and placeholders for %s",
    (locale) => {
      setSteamLanguage(locale);
      expect(getCurrentLanguage()).toBe("de");
      expect(t("SCALING_ENABLED", "Enable Scaling (Restart)")).toBe(
        "Skalierung aktivieren (Neustart)",
      );
      expect(
        t("PROFILE_SAVE_RUNNING", "Save profile for {game}", {
          game: "Portal 2",
        }),
      ).toBe("Profil für Portal 2 speichern");
      expect(
        t(
          "LIVE_STATUS_SCALING_MEMORY_CONSTRAINED",
          "Requested {requested}×; limited to {effective}× by this GPU's memory safety limit.",
          { requested: 2, effective: 1.5 },
        ),
      ).toBe(
        "Angefordert: 2×; durch die Speichersicherheitsgrenze dieser GPU auf 1.5× begrenzt.",
      );
      expect(t("NOT_A_REAL_KEY", "Safe fallback")).toBe("Safe fallback");
    },
  );

  it.each(["russian", "ru", "ru_RU", "ru-BY"])(
    "uses Russian translations and placeholders for %s",
    (locale) => {
      setSteamLanguage(locale);
      expect(getCurrentLanguage()).toBe("ru");
      expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation")).toBe(
        "Генерация кадров",
      );
      expect(t("CONFIG_DISABLE_HDR_EXPOSURE", "Disable HDR (Restart)")).toBe(
        "Отключить HDR (требуется перезапуск)",
      );
      expect(
        t(
          "FLATPAK_STATUS_EXTENSION_READY",
          "Prepared — MAKO {version} extension installed",
          {
            version: "26.08",
          },
        ),
      ).toBe("Подготовлено — расширение MAKO 26.08 установлено");
      expect(
        t(
          "LIVE_STATUS_SCALING_MEMORY_CONSTRAINED",
          "Requested {requested}×; limited to {effective}× by this GPU's memory safety limit.",
          { requested: 2, effective: 1.5 },
        ),
      ).toBe(
        "Запрошено 2×; ограничено до 1.5× из соображений безопасности памяти GPU.",
      );
      expect(t("NOT_A_REAL_KEY", "Safe fallback")).toBe("Safe fallback");
    },
  );

  it("uses the caller fallback for English, unknown languages, and unknown keys", () => {
    setSteamLanguage("english");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation")).toBe(
      "Frame Generation",
    );

    setSteamLanguage("finnish");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation")).toBe(
      "Frame Generation",
    );
    expect(t("NOT_A_REAL_KEY", "Safe fallback")).toBe("Safe fallback");
  });
});
