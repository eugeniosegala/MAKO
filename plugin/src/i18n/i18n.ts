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

const normalizeLanguage = (language: string): string => {
  const normalized = language.trim().toLowerCase();
  return steamLanguageMap[normalized] ?? normalized;
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

let cachedLang: string | undefined;

export const getCurrentLanguage = (): string => {
  if (cachedLang) return cachedLang;

  const lang = normalizeLanguage(window.LocalizationManager.m_rgLocalesToUse[0]);
  cachedLang = lang;
  return lang;
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
