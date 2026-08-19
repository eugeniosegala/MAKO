// Generated from defaults/i18n by the normal frontend build.
import * as languages from "./languages.json";

type LanguageEntry = {
  name: string;
  strings?: Record<string, string>;
};

const steamLanguageMap: Record<string, string> =
  languages.steam_language_map as Record<string, string>;
const languageMetadata = languages.language_metadata as unknown as Record<string, LanguageEntry>;
const translationSets = languages as unknown as Record<string, Record<string, string>>;

export const normalizeLanguage = (language?: string): string => {
  const normalized = (language || "en").trim().toLowerCase().replace(/_/g, "-");
  const mapped = steamLanguageMap[normalized] ?? normalized;

  // Steam has used both language names (such as `schinese`) and standard
  // locale identifiers (such as `zh-CN`) here. Translation files are keyed by
  // their base language so either representation resolves consistently.
  return mapped.split("-", 1)[0] || "en";
};

function getLangs(): Record<string, LanguageEntry> {
  const langs = Object.fromEntries(
    Object.entries(languageMetadata).map(([language, metadata]) => [language, { ...metadata }])
  ) as Record<string, LanguageEntry>;

  for (const [language, metadata] of Object.entries(langs)) {
    const strings = translationSets[language];
    if (strings && metadata.name) {
      metadata.strings = strings;
    }
  }

  return langs;
}

export const LANGS = getLangs();

export const getCurrentLanguage = (): string => {
  return normalizeLanguage(window.LocalizationManager?.m_rgLocalesToUse?.[0]);
};

export const getLanguageName = (lang?: string): string => {
  const targetLang = normalizeLanguage(lang || getCurrentLanguage());
  return LANGS[targetLang]?.name || targetLang;
};

/**
 * Translate a key to the current language
 *
 * @param key - Translation key
 * @param originalString - Original text (fallback)
 * @param replacements - Named values for placeholders such as {profile}
 * @returns Translated string or original text if translation not found
 *
 * @example
 * t('CONTENT_FPS_MULTIPLIER', 'FPS Multiplier')
 */
const t = (
  key: string,
  originalString: string,
  replacements: Record<string, string | number> = {}
): string => {
  const lang = getCurrentLanguage();
  const translated = lang === "en"
    ? originalString
    : LANGS[lang]?.strings?.[key] ?? originalString;

  return Object.entries(replacements).reduce(
    (text, [name, value]) => text.split(`{${name}}`).join(String(value)),
    translated
  );
};

export default t;
