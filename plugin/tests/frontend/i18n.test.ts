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
  });

  it("reports localized language names with a normalized fallback", () => {
    expect(getLanguageName("koreana")).toBe("한국어");
    expect(getLanguageName("spanish")).toBe("Español");
    expect(getLanguageName("brazilian")).toBe("Português (Brasil)");
  });

  it("uses the selected dictionary and replaces named placeholders", () => {
    setSteamLanguage("japanese");
    expect(getCurrentLanguage()).toBe("ja");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation Mode")).toBe(
      "フレーム生成モード",
    );
    expect(
      t("FLATPAK_RUNTIME_VERSION", "Runtime {version}", {
        version: "24.08",
      }),
    ).toBe("ランタイム 24.08");
  });

  it("uses translated regional dictionaries without collapsing their locale", () => {
    setSteamLanguage("brazilian");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation Mode")).toBe(
      "Modo de geração de quadros",
    );

    setSteamLanguage("portuguese");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation Mode")).toBe(
      "Modo de geração de fotogramas",
    );

    setSteamLanguage("spanish");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation Mode")).toBe(
      "Modo de generación de cuadros",
    );
  });

  it("uses the caller fallback for English, unknown languages, and unknown keys", () => {
    setSteamLanguage("english");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation Mode")).toBe(
      "Frame Generation Mode",
    );

    setSteamLanguage("german");
    expect(t("CONTENT_FPS_MULTIPLIER", "Frame Generation Mode")).toBe(
      "Frame Generation Mode",
    );
    expect(t("NOT_A_REAL_KEY", "Safe fallback")).toBe("Safe fallback");
  });
});
