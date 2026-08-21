// Generated from defaults/i18n by the normal frontend build.
import languageBundle from "./languages.json";

type LanguageEntry = {
  name: string;
  strings?: Record<string, string>;
};

const steamLanguageMap: Record<string, string> =
  languageBundle.steam_language_map as Record<string, string>;
const languageMetadata = languageBundle.language_metadata as unknown as Record<string, LanguageEntry>;
const translationSets = languageBundle as unknown as Record<string, Record<string, string>>;
const canonicalLanguageCodes = new Map(
  Object.keys(languageMetadata).map((language) => [language.toLowerCase(), language]),
);

export const normalizeLanguage = (language?: string): string => {
  const normalized = (language || "en").trim().toLowerCase().replace(/_/g, "-");
  const mapped = steamLanguageMap[normalized] ?? normalized;

  // Steam has used both language names (such as `schinese`) and standard
  // locale identifiers (such as `pt-BR`) here. Prefer an exact advertised
  // locale before falling back to its base language so regional Portuguese
  // dictionaries remain distinct while ja-JP and zh-CN resolve normally.
  const exact = canonicalLanguageCodes.get(mapped.toLowerCase());
  if (exact) return exact;
  const base = mapped.split("-", 1)[0] || "en";
  return canonicalLanguageCodes.get(base) ?? base;
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
