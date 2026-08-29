import { PanelSectionRow } from "@decky/ui";
import type {
  RuntimeContextState,
  RuntimeStatusResult,
} from "../api/makoApi";
import { MakoCompactSpinner, makoPanelStyle } from "./MakoUi";
import t from "../i18n/i18n";

function latestRole(
  contexts: RuntimeContextState[],
  role: RuntimeContextState["role"],
): RuntimeContextState | undefined {
  return contexts.find((context) => context.role === role);
}

function multiplier(profile: RuntimeContextState["requested"]): number {
  return profile.adaptive
    ? profile.adaptive_max_multiplier
    : profile.multiplier;
}

export function RuntimeStatusNotice({
  status,
}: {
  status: RuntimeStatusResult | null;
}) {
  if (!status?.success || status.phase === "inactive") return null;

  const frameGeneration = latestRole(status.contexts, "frame-generation");
  const scaling = latestRole(status.contexts, "spatial-scaling");
  const transient = ["debouncing", "preparing", "draining"].includes(
    status.phase,
  );
  const warning = [
    "failed",
    "swapchain-recreation",
    "process-restart",
  ].includes(status.phase);
  const title =
    status.phase === "active"
      ? t("RUNTIME_STATUS_ACTIVE", "Live settings applied")
      : status.phase === "process-restart"
        ? t("RUNTIME_STATUS_RESTART", "Restart the game to finish applying")
        : status.phase === "swapchain-recreation"
          ? t(
              "RUNTIME_STATUS_SWAPCHAIN",
              "Waiting for the game to recreate its swapchain",
            )
          : status.phase === "failed"
            ? t(
                "RUNTIME_STATUS_FAILED",
                "Live update failed; the previous settings remain active",
              )
            : t("RUNTIME_STATUS_APPLYING", "Applying live settings…");

  const frameGenerationChanged = Boolean(
    frameGeneration &&
      (multiplier(frameGeneration.requested) !==
        multiplier(frameGeneration.applied) ||
        frameGeneration.requested.effective_flow_scale !==
          frameGeneration.applied.effective_flow_scale ||
        frameGeneration.requested.effective_performance_mode !==
          frameGeneration.applied.effective_performance_mode),
  );
  const scalingChanged = Boolean(
    scaling &&
      (scaling.requested.scaling_method !== scaling.applied.scaling_method ||
        scaling.requested.scaling_factor !== scaling.applied.scaling_factor ||
        scaling.requested.scaling_sharpness !==
          scaling.applied.scaling_sharpness),
  );

  return (
    <PanelSectionRow>
      <div
        role="status"
        data-runtime-phase={status.phase}
        style={{
          ...makoPanelStyle,
          width: "100%",
          boxSizing: "border-box",
          padding: "9px 11px",
          borderColor: warning
            ? "rgba(244, 162, 89, 0.55)"
            : "rgba(91, 163, 209, 0.42)",
          color: warning ? "#f7d9b4" : "#d6ecff",
          fontSize: "11px",
          lineHeight: 1.35,
        }}
      >
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: "7px",
            fontWeight: 650,
          }}
        >
          {transient && <MakoCompactSpinner size={14} />}
          <span>{title}</span>
        </div>
        {frameGenerationChanged && frameGeneration && (
          <div style={{ marginTop: "5px", color: "#b7d3de" }}>
            {t(
              "RUNTIME_STATUS_FG_DETAIL",
              "Frame Generation: requested {requested}x · active {applied}x · capacity {capacity}",
              {
                requested: multiplier(frameGeneration.requested),
                applied: multiplier(frameGeneration.applied),
                capacity: frameGeneration.applied_generated_capacity,
              },
            )}
          </div>
        )}
        {scalingChanged && scaling && (
          <div style={{ marginTop: "3px", color: "#b7d3de" }}>
            {t(
              "RUNTIME_STATUS_SCALING_DETAIL",
              "Scaling: requested {requested} · active {applied}",
              {
                requested: scaling.requested.scaling_method,
                applied: scaling.applied.scaling_method,
              },
            )}
          </div>
        )}
        {status.phase === "failed" &&
          status.contexts.find((context) => context.error)?.error && (
            <div style={{ marginTop: "4px", color: "#d9b99a" }}>
              {status.contexts.find((context) => context.error)?.error}
            </div>
          )}
      </div>
    </PanelSectionRow>
  );
}
