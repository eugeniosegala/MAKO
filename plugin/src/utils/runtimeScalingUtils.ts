import type { RuntimeStatusResult } from "../api/makoApi";

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
  inactiveReason: string | null;
  nonSupersamplingFactorCeiling: number | null;
}

export function runtimeScalingUiState(
  status: RuntimeStatusResult,
  profileName: string,
): RuntimeScalingUiState {
  if (!status.success) {
    return { inactiveReason: null, nonSupersamplingFactorCeiling: null };
  }
  const contexts = status.contexts.filter(
    (candidate) =>
      !profileName ||
      candidate.requested.name === profileName ||
      candidate.applied.name === profileName,
  );
  const ceilings = contexts
    .map(
      (candidate) =>
        candidate.spatial_scaling.non_supersampling_factor_ceiling,
    )
    .filter((value): value is number => value !== null && value >= 1);
  return {
    inactiveReason: scalingInactiveReason(status, profileName),
    nonSupersamplingFactorCeiling:
      ceilings.length > 0 ? Math.min(...ceilings) : null,
  };
}
