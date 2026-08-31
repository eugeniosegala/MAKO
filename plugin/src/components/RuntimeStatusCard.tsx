import { PanelSectionRow } from "@decky/ui";
import type { RuntimeScalingUiState } from "../utils/runtimeScalingUtils";
import t from "../i18n/i18n";
import {
  MakoSectionHeader,
  makoAccentColor,
  makoPanelDivider,
  makoPanelStyle,
} from "./MakoUi";

function methodLabel(method: string): string {
  switch (method) {
    case "mako":
      return t("SCALING_METHOD_MAKO", "MAKO Scaler");
    case "ls1":
      return t("SCALING_METHOD_LS1", "LS1 Quality");
    case "ls1-performance":
      return t("SCALING_METHOD_LS1_PERFORMANCE", "LS1 Performance");
    default:
      return t("SCALING_METHOD_NATIVE", "Native Resolution");
  }
}

function resolution(width: number, height: number): string {
  return width > 0 && height > 0 ? `${width} × ${height}` : "—";
}

function StatusRow({
  label,
  active,
  separated,
  children,
}: {
  label: string;
  active: boolean;
  separated?: boolean;
  children: React.ReactNode;
}) {
  return (
    <div
      style={{
        display: "grid",
        gridTemplateColumns: "8px minmax(0, 1fr)",
        columnGap: "6px",
        padding: "6px 8px",
        borderLeft: separated ? makoPanelDivider : undefined,
      }}
    >
      <span
        aria-hidden="true"
        style={{
          width: "7px",
          height: "7px",
          marginTop: "3px",
          borderRadius: "50%",
          background: active ? "#5fe3b1" : "#738891",
          boxShadow: active ? "0 0 7px rgba(95, 227, 177, 0.55)" : "none",
        }}
      />
      <div style={{ minWidth: 0 }}>
        <div style={{ color: "#edf8fb", fontSize: "10px", fontWeight: 650 }}>
          {label}
        </div>
        <div
          style={{
            marginTop: "2px",
            color: "#b6c9cf",
            fontSize: "9px",
            lineHeight: 1.3,
          }}
        >
          {children}
        </div>
      </div>
    </div>
  );
}

export function RuntimeStatusCard({
  runtimeState,
}: {
  runtimeState: RuntimeScalingUiState;
}) {
  const frameGenerationSummary = runtimeState.frameGenerationActive
    ? runtimeState.frameGenerationMode === "adaptive"
      ? t(
          "LIVE_STATUS_FG_ADAPTIVE",
          "Adaptive · {target} FPS · up to {multiplier}x",
          {
            target: runtimeState.frameGenerationTargetFps ?? "—",
            multiplier: runtimeState.frameGenerationMultiplier ?? "—",
          },
        )
      : t("LIVE_STATUS_FG_FIXED", "Fixed · {multiplier}x", {
          multiplier: runtimeState.frameGenerationMultiplier ?? "—",
        })
    : runtimeState.frameGenerationEnabled
      ? t(
          "LIVE_STATUS_FG_INACTIVE",
          "On in settings, but not currently generating frames.",
        )
      : t("LIVE_STATUS_OFF", "Off");
  const scalingSummary = runtimeState.scalingActive
    ? t(
        "LIVE_STATUS_SCALING_ACTIVE",
        "{method} · {source} → {presentation} · {factor}x",
        {
          method: methodLabel(runtimeState.activeMethod),
          source: resolution(
            runtimeState.sourceWidth,
            runtimeState.sourceHeight,
          ),
          presentation: resolution(
            runtimeState.presentationWidth,
            runtimeState.presentationHeight,
          ),
          factor: runtimeState.effectiveFactor.toFixed(2),
        },
      )
    : runtimeState.scalingEnabled
      ? t(
          "LIVE_STATUS_SCALING_INACTIVE",
          "On in settings, but the game image is not being upscaled.",
        )
      : t("LIVE_STATUS_OFF", "Off");

  return (
    <>
      <MakoSectionHeader topMargin="18px">
        {t("LIVE_STATUS_TITLE", "Live Status")}
      </MakoSectionHeader>
      <PanelSectionRow>
        <div
          aria-label={t("LIVE_STATUS_TITLE", "Live Status")}
          style={{ ...makoPanelStyle, width: "100%" }}
        >
          <div
            style={{
              padding: "4px 8px",
              display: "flex",
              alignItems: "baseline",
              justifyContent: "flex-end",
              gap: "8px",
            }}
          >
            <span
              style={{
                color: runtimeState.hasContext ? makoAccentColor : "#93a5ab",
                fontSize: "8.5px",
                fontWeight: 600,
                textTransform: "uppercase",
              }}
            >
              {runtimeState.hasContext
                ? t("LIVE_STATUS_CONNECTED", "MAKO is active")
                : t("LIVE_STATUS_WAITING", "Waiting for MAKO")}
            </span>
          </div>

          {!runtimeState.hasContext ? (
            <div
              style={{
                padding: "8px 10px",
                borderTop: makoPanelDivider,
                color: "#b6c9cf",
                fontSize: "10px",
                lineHeight: 1.4,
              }}
            >
              {t(
                "LIVE_STATUS_WAITING_DESC",
                "The running game is not using MAKO yet. Start playing to confirm Frame Generation and Upscaling.",
              )}
            </div>
          ) : (
            <>
              <div
                data-mako-live-status-grid="compact-two-column"
                style={{
                  display: "grid",
                  gridTemplateColumns: "repeat(2, minmax(0, 1fr))",
                  borderTop: makoPanelDivider,
                }}
              >
                <StatusRow
                  label={t("CONTENT_FPS_MULTIPLIER", "Frame Generation")}
                  active={runtimeState.frameGenerationActive}
                >
                  {frameGenerationSummary}
                  {runtimeState.frameGenerationPending && (
                    <div style={{ marginTop: "3px", color: "#f7d9b4" }}>
                      {t(
                        "LIVE_STATUS_PENDING",
                        "A saved change is still applying or needs a restart.",
                      )}
                    </div>
                  )}
                </StatusRow>
                <StatusRow
                  label={t("FEATURE_UPSCALING_TAB", "Upscaling")}
                  active={runtimeState.scalingActive}
                  separated
                >
                  {scalingSummary}
                  {runtimeState.scalingActive && (
                    <div style={{ marginTop: "3px" }}>
                      {runtimeState.pipeline === "pre-frame-generation"
                        ? t(
                            "LIVE_STATUS_PIPELINE_PRE",
                            "Upscaling runs before generated frames.",
                          )
                        : t(
                            "LIVE_STATUS_PIPELINE_POST",
                            "Upscaling runs after generated frames.",
                          )}
                    </div>
                  )}
                  {runtimeState.supersamplingActive && (
                    <div style={{ marginTop: "3px", color: "#f7d9b4" }}>
                      {t(
                        "LIVE_STATUS_SUPERSAMPLING",
                        "Quality Supersampling is on for a sharper final image.",
                      )}
                    </div>
                  )}
                  {runtimeState.fallbackReason && (
                    <div style={{ marginTop: "3px", color: "#f7d9b4" }}>
                      {t(
                        "LIVE_STATUS_SCALING_FALLBACK",
                        "You selected {requested}; MAKO is using {active} instead.",
                        {
                          requested: methodLabel(runtimeState.requestedMethod),
                          active: methodLabel(runtimeState.activeMethod),
                        },
                      )}
                    </div>
                  )}
                  {runtimeState.scalingPending && (
                    <div style={{ marginTop: "3px", color: "#f7d9b4" }}>
                      {t(
                        "LIVE_STATUS_PENDING",
                        "A saved change is still applying or needs a restart.",
                      )}
                    </div>
                  )}
                </StatusRow>
              </div>
            </>
          )}
        </div>
      </PanelSectionRow>
    </>
  );
}
