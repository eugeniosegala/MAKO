import type {
  RuntimeApplicationPhase,
  RuntimeContextState,
  RuntimeScalingMethod,
  RuntimeScalingPipeline,
  RuntimeWineCompatibilityFix,
  RuntimeStatusResult,
} from "../api/makoApi";
import type { FrameGenerationModelChoice } from "../config/frameGenerationModelChoice";
import t from "../i18n/i18n";

export function scalingInactiveReason(
  status: RuntimeStatusResult,
  profileName: string,
): string | null {
  if (!status.success) return null;
  const context = status.contexts.find(
    (candidate) =>
      !candidate.spatial_scaling.activation_supported &&
      candidate.requested.scaling_enabled &&
      candidate.requested.scaling_method !== "native" &&
      (!profileName ||
        candidate.requested.name === profileName ||
        candidate.applied.name === profileName),
  );
  return context
    ? context.spatial_scaling.inactive_reason ||
        "gamescope-wsi-surface-unproven"
    : null;
}

export interface RuntimeScalingUiState {
  hasContext: boolean;
  phase: RuntimeApplicationPhase;
  frameGenerationActive: boolean;
  frameGenerationEnabled: boolean;
  frameGenerationModel: FrameGenerationModelChoice | null;
  frameGenerationMode: "off" | "fixed" | "adaptive";
  frameGenerationAdaptiveStyle: "fractional" | "steady" | null;
  frameGenerationTargetFps: number | null;
  frameGenerationMultiplier: number | null;
  frameGenerationPending: boolean;
  wineCompatibilityFix: RuntimeWineCompatibilityFix | null;
  gpuHeadroomState: string;
  gpuHeadroomPercent: number;
  scalingActive: boolean;
  scalingEnabled: boolean;
  scalingActivationSupported: boolean | null;
  scalingPending: boolean;
  inactiveReason: string | null;
  constraintReason: string | null;
  requestedFactor: number;
  nonSupersamplingFactorCeiling: number | null;
  sourceWidth: number;
  sourceHeight: number;
  presentationWidth: number;
  presentationHeight: number;
  gamescopeTargetWidth: number;
  gamescopeTargetHeight: number;
  requestedMethod: RuntimeScalingMethod;
  activeMethod: RuntimeScalingMethod;
  effectiveFactor: number;
  pipeline: RuntimeScalingPipeline;
  supersamplingActive: boolean;
  fallbackReason: string | null;
}

export const EMPTY_RUNTIME_SCALING_UI_STATE: RuntimeScalingUiState = {
  hasContext: false,
  phase: "inactive",
  frameGenerationActive: false,
  frameGenerationEnabled: false,
  wineCompatibilityFix: null,
  frameGenerationModel: null,
  frameGenerationMode: "off",
  frameGenerationAdaptiveStyle: null,
  frameGenerationTargetFps: null,
  frameGenerationMultiplier: null,
  frameGenerationPending: false,
  gpuHeadroomState: "off",
  gpuHeadroomPercent: 0,
  scalingActive: false,
  scalingEnabled: false,
  scalingActivationSupported: null,
  scalingPending: false,
  inactiveReason: null,
  constraintReason: null,
  requestedFactor: 1,
  nonSupersamplingFactorCeiling: null,
  sourceWidth: 0,
  sourceHeight: 0,
  presentationWidth: 0,
  presentationHeight: 0,
  gamescopeTargetWidth: 0,
  gamescopeTargetHeight: 0,
  requestedMethod: "native",
  activeMethod: "native",
  effectiveFactor: 1,
  pipeline: "inactive",
  supersamplingActive: false,
  fallbackReason: null,
};

function newestContext(
  contexts: RuntimeContextState[],
  predicate: (context: RuntimeContextState) => boolean,
): RuntimeContextState | undefined {
  return contexts.find(predicate);
}

export function runtimeScalingUiState(
  status: RuntimeStatusResult,
  profileName: string,
): RuntimeScalingUiState {
  if (!status.success) {
    return { ...EMPTY_RUNTIME_SCALING_UI_STATE };
  }
  const contexts = status.contexts.filter(
    (candidate) =>
      !profileName ||
      candidate.requested.name === profileName ||
      candidate.applied.name === profileName,
  );
  const ceilings = contexts
    .map(
      (candidate) => candidate.spatial_scaling.non_supersampling_factor_ceiling,
    )
    .filter((value): value is number => value !== null && value >= 1);
  const frameContext = newestContext(
    contexts,
    (candidate) => candidate.role === "frame-generation",
  );
  const scalingRequestedContext = newestContext(
    contexts,
    (candidate) => candidate.requested.scaling_enabled,
  );
  const spatialContext =
    // Bridge display-size presentation can have a private spatial graph with
    // Scaling off. Use the applied feature flag, including restart-pending
    // edits, when selecting its user-visible owner.
    newestContext(
      contexts,
      (candidate) =>
        candidate.applied.scaling_enabled && candidate.spatial_scaling.active,
    ) ??
    scalingRequestedContext ??
    newestContext(contexts, (candidate) => candidate.applied.scaling_enabled);
  const appliedFrameProfile = frameContext?.applied;
  const frameGenerationEnabled = Boolean(
    appliedFrameProfile?.frame_generation_enabled,
  );
  const frameGenerationMode = !frameGenerationEnabled
    ? "off"
    : appliedFrameProfile?.adaptive
      ? "adaptive"
      : "fixed";
  return {
    hasContext: contexts.length > 0,
    phase: contexts.length > 0 ? status.phase : "inactive",
    frameGenerationActive: Boolean(frameContext?.frame_generation_active),
    frameGenerationEnabled,
    wineCompatibilityFix:
      frameContext?.frame_generation_unavailable?.wine_compatibility_fix ??
      null,
    gpuHeadroomState: frameContext?.gpu_headroom_state ?? "unavailable",
    gpuHeadroomPercent: appliedFrameProfile?.gpu_headroom_percent ?? 0,
    frameGenerationModel: appliedFrameProfile
      ? appliedFrameProfile.frame_generation_model === "mako-fg"
        ? "mako-fg"
        : appliedFrameProfile.effective_performance_mode
          ? "lsfg-performance"
          : "lsfg-quality"
      : null,
    frameGenerationMode,
    frameGenerationAdaptiveStyle: appliedFrameProfile?.adaptive
      ? appliedFrameProfile.adaptive_auto_base_fps_cap
        ? "steady"
        : "fractional"
      : null,
    frameGenerationTargetFps: appliedFrameProfile?.adaptive
      ? appliedFrameProfile.target_fps
      : null,
    frameGenerationMultiplier: frameGenerationEnabled
      ? appliedFrameProfile?.adaptive
        ? appliedFrameProfile.adaptive_max_multiplier
        : (appliedFrameProfile?.multiplier ?? null)
      : null,
    frameGenerationPending: Boolean(
      frameContext &&
      (frameContext.pending.frame_generation_private ||
        frameContext.pending.process_restart),
    ),
    scalingActive: Boolean(
      spatialContext?.applied.scaling_enabled &&
      spatialContext.spatial_scaling.active,
    ),
    scalingEnabled: Boolean(scalingRequestedContext?.requested.scaling_enabled),
    scalingActivationSupported: spatialContext
      ? spatialContext.spatial_scaling.activation_supported
      : null,
    scalingPending: Boolean(
      spatialContext &&
      (spatialContext.pending.spatial_private ||
        spatialContext.pending.swapchain_recreation ||
        spatialContext.pending.process_restart),
    ),
    inactiveReason:
      spatialContext?.spatial_scaling.inactive_reason ??
      scalingInactiveReason(status, profileName),
    constraintReason: spatialContext?.spatial_scaling.constraint_reason ?? null,
    requestedFactor:
      spatialContext?.requested.scaling_factor ??
      scalingRequestedContext?.requested.scaling_factor ??
      1,
    nonSupersamplingFactorCeiling:
      ceilings.length > 0 ? Math.min(...ceilings) : null,
    sourceWidth: spatialContext?.spatial_scaling.source_width ?? 0,
    sourceHeight: spatialContext?.spatial_scaling.source_height ?? 0,
    presentationWidth: spatialContext?.spatial_scaling.presentation_width ?? 0,
    presentationHeight:
      spatialContext?.spatial_scaling.presentation_height ?? 0,
    gamescopeTargetWidth:
      spatialContext?.spatial_scaling.gamescope_target_width ?? 0,
    gamescopeTargetHeight:
      spatialContext?.spatial_scaling.gamescope_target_height ?? 0,
    requestedMethod:
      spatialContext?.spatial_scaling.requested_method ?? "native",
    activeMethod: spatialContext?.spatial_scaling.active_method ?? "native",
    effectiveFactor: spatialContext?.spatial_scaling.effective_factor ?? 1,
    pipeline: spatialContext?.spatial_scaling.pipeline ?? "inactive",
    supersamplingActive: Boolean(
      spatialContext?.spatial_scaling.supersampling_active,
    ),
    fallbackReason: spatialContext?.spatial_scaling.fallback_reason ?? null,
  };
}

/** Explain why Wine's offscreen copy keeps Frame Generation off for a game. */
export function wineCompatibilityFixMessage(
  fix: RuntimeScalingUiState["wineCompatibilityFix"],
): string | null {
  switch (fix) {
    case "restart-required":
      return t(
        "LIVE_STATUS_FG_WINE_RESTART",
        "Off: this game needs the Proton Offscreen Window Fix. MAKO applied it; restart the game to use Frame Generation.",
      );
    case "ineffective":
      return t(
        "LIVE_STATUS_FG_WINE_INEFFECTIVE",
        "Unavailable: the Proton Offscreen Window Fix doesn't work with this Proton version. Use Proton 10 or newer.",
      );
    case "manual":
      return t(
        "LIVE_STATUS_FG_WINE_MANUAL",
        "Unavailable: this game needs the Proton Offscreen Window Fix. Add WINE_DISABLE_VK_CHILD_WINDOW_RENDERING_HACK=1 to its launch options.",
      );
    case "disabled":
      return t(
        "LIVE_STATUS_FG_WINE_DISABLED",
        "Off: this game needs the Proton Offscreen Window Fix. Turn it on in Tweaks to apply it.",
      );
    case "not-eligible":
    case "unneeded":
      return t(
        "LIVE_STATUS_FG_WINE_UNAVAILABLE",
        "Unavailable: Frame Generation doesn't work with this game on this Proton version. Use Proton 10 or newer.",
      );
    default:
      return null;
  }
}
