import type { ConfigurationData } from "./configSchema";
import t from "../i18n/i18n";

/** How often MAKO FG solves motion; `mako_fg_mode` in the shared schema. */
export type MakoFgMode =
  | "quality"
  | "balanced"
  | "performance"
  | "extra-performance";

type MakoFgModeConfig = Pick<
  ConfigurationData,
  "mako_fg_mode" | "ultra_performance"
>;

const MODES: readonly MakoFgMode[] = [
  "quality",
  "balanced",
  "performance",
  "extra-performance",
];

/** Ultra Performance uses at least Performance, so it hides the lighter modes. */
export function makoFgModeChoices(config: MakoFgModeConfig): MakoFgMode[] {
  return config.ultra_performance ? MODES.slice(2) : [...MODES];
}

export function effectiveMakoFgMode(config: MakoFgModeConfig): MakoFgMode {
  const saved = MODES.includes(config.mako_fg_mode as MakoFgMode)
    ? (config.mako_fg_mode as MakoFgMode)
    : "balanced";
  return makoFgModeChoices(config).includes(saved) ? saved : "performance";
}

export function makoFgModeLabel(mode: MakoFgMode): string {
  switch (mode) {
    case "quality":
      return t("CONFIG_MAKO_FG_MODE_QUALITY", "Quality");
    case "performance":
      return t("CONFIG_MAKO_FG_MODE_PERFORMANCE", "Performance");
    case "extra-performance":
      return t("CONFIG_MAKO_FG_MODE_EXTRA_PERFORMANCE", "Extra Performance");
    default:
      return t("CONFIG_MAKO_FG_MODE_BALANCED", "Balanced");
  }
}
