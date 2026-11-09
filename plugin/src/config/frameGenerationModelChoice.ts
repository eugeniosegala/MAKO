import {
  FRAME_GENERATION_MODEL,
  PERFORMANCE_MODE,
  type ConfigurationData,
} from "./configSchema";
import t from "../i18n/i18n";

/**
 * Model picker entries. LSFG Quality and LSFG Performance share the
 * restart-bound `lossless-scaling` model and differ only by the live
 * `performance_mode` field.
 */
export type FrameGenerationModelChoice =
  | "mako-fg"
  | "lsfg-quality"
  | "lsfg-performance";

type ModelChoiceConfig = Pick<
  ConfigurationData,
  "frame_generation_model" | "performance_mode" | "ultra_performance"
>;

export function frameGenerationModelChoice(
  config: ModelChoiceConfig,
): FrameGenerationModelChoice {
  if (config.frame_generation_model === "mako-fg") return "mako-fg";
  return config.ultra_performance || config.performance_mode
    ? "lsfg-performance"
    : "lsfg-quality";
}

/** Ultra Performance always runs LSFG Performance, so it hides LSFG Quality. */
export function frameGenerationModelChoices(
  config: ModelChoiceConfig,
): FrameGenerationModelChoice[] {
  return config.ultra_performance
    ? ["mako-fg", "lsfg-performance"]
    : ["mako-fg", "lsfg-quality", "lsfg-performance"];
}

/** MAKO FG keeps the saved LSFG variant; an LSFG entry writes both fields atomically. */
export function frameGenerationModelChoiceChanges(
  choice: FrameGenerationModelChoice,
): Partial<ConfigurationData> {
  if (choice === "mako-fg") return { [FRAME_GENERATION_MODEL]: "mako-fg" };
  return {
    [FRAME_GENERATION_MODEL]: "lossless-scaling",
    [PERFORMANCE_MODE]: choice === "lsfg-performance",
  };
}

/** Compact alias for the live status, where a full LSFG name would wrap. */
export function frameGenerationModelShortLabel(
  choice: FrameGenerationModelChoice,
): string {
  switch (choice) {
    case "lsfg-quality":
      return t("LIVE_STATUS_MODEL_LSFG_QUALITY", "LSFG Q");
    case "lsfg-performance":
      return t("LIVE_STATUS_MODEL_LSFG_PERFORMANCE", "LSFG P");
    default:
      return "MAKO FG";
  }
}

export function frameGenerationModelLabel(
  choice: FrameGenerationModelChoice,
): string {
  switch (choice) {
    case "lsfg-quality":
      return t("CONFIG_FG_MODEL_LSFG_QUALITY", "LSFG Quality");
    case "lsfg-performance":
      return t("CONFIG_FG_MODEL_LSFG_PERFORMANCE", "LSFG Performance");
    default:
      return "MAKO FG";
  }
}
