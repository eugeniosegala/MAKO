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
