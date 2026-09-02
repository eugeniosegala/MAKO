import type {
  RuntimeApplicationPhase,
  RuntimeContextState,
  RuntimeScalingMethod,
  RuntimeScalingPipeline,
  RuntimeStatusResult,
} from "../api/makoApi";

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
  frameGenerationMode: "off" | "fixed" | "adaptive";
  frameGenerationAdaptiveStyle: "fractional" | "steady" | null;
  frameGenerationTargetFps: number | null;
  frameGenerationMultiplier: number | null;
  frameGenerationPending: boolean;
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
  frameGenerationMode: "off",
  frameGenerationAdaptiveStyle: null,
  frameGenerationTargetFps: null,
  frameGenerationMultiplier: null,
  frameGenerationPending: false,
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
  const spatialContext =
    newestContext(contexts, (candidate) => candidate.spatial_scaling.active) ??
    newestContext(
      contexts,
      (candidate) => candidate.role === "spatial-scaling",
    ) ??
    newestContext(contexts, (candidate) => candidate.requested.scaling_enabled);
  const appliedFrameProfile = frameContext?.applied;
  const scalingRequestedContext = newestContext(
    contexts,
    (candidate) => candidate.requested.scaling_enabled,
  );
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
    scalingActive: Boolean(spatialContext?.spatial_scaling.active),
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
    constraintReason:
      spatialContext?.spatial_scaling.constraint_reason ?? null,
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
