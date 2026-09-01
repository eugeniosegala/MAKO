import { PanelSectionRow } from "@decky/ui";
import type { RuntimeScalingUiState } from "../utils/runtimeScalingUtils";
import t from "../i18n/i18n";
import {
  MakoSectionHeader,
  MakoSectionTail,
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

function StatusDetail({
  label,
  value,
}: {
  label: string;
  value: React.ReactNode;
}) {
  return (
    <div
      style={{
        display: "grid",
        gridTemplateColumns: "minmax(0, 1fr) auto",
        gap: "5px",
        alignItems: "baseline",
        marginTop: "2px",
      }}
    >
      <span style={{ color: "#839da5" }}>{label}</span>
      <span
        style={{
          color: "#d7e7eb",
          textAlign: "right",
          overflowWrap: "anywhere",
        }}
      >
        {value}
      </span>
    </div>
  );
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

function StatusNotices({ children }: { children: React.ReactNode }) {
  return (
    <div
      data-mako-live-status-notices="true"
      style={{
        display: "grid",
        gap: "4px",
        marginTop: "8px",
        color: "#f7d9b4",
      }}
    >
      {children}
    </div>
  );
}

export function RuntimeStatusCard({
  runtimeState,
}: {
  runtimeState: RuntimeScalingUiState;
}) {
  return (
    <>
      <MakoSectionHeader>
        {t("LIVE_STATUS_TITLE", "Live Status")}
      </MakoSectionHeader>
      <PanelSectionRow>
        <MakoSectionTail>
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
                    {runtimeState.frameGenerationActive ? (
                      <>
                        <StatusDetail
                          label={t("LIVE_STATUS_MODE", "Mode")}
                          value={
                            runtimeState.frameGenerationMode === "adaptive"
                              ? t("ADAPTIVE_VALUE", "Adaptive")
                              : t("LIVE_STATUS_FIXED_VALUE", "Fixed")
                          }
                        />
                        {runtimeState.frameGenerationMode === "adaptive" ? (
                          <>
                            <StatusDetail
                              label={t("LIVE_STATUS_ADAPTIVE_STYLE", "Style")}
                              value={
                                runtimeState.frameGenerationAdaptiveStyle ===
                                "steady"
                                  ? t("LIVE_STATUS_STEADY_VALUE", "Steady")
                                  : t(
                                      "LIVE_STATUS_FRACTIONAL_VALUE",
                                      "Fractional",
                                    )
                              }
                            />
                            <StatusDetail
                              label={t("LIVE_STATUS_TARGET", "Target")}
                              value={`${runtimeState.frameGenerationTargetFps ?? "—"} FPS`}
                            />
                            <StatusDetail
                              label={t(
                                "LIVE_STATUS_MAX_MULTIPLIER",
                                "Max factor",
                              )}
                              value={`${runtimeState.frameGenerationMultiplier ?? "—"}×`}
                            />
                          </>
                        ) : (
                          <StatusDetail
                            label={t("LIVE_STATUS_MULTIPLIER", "Factor")}
                            value={`${runtimeState.frameGenerationMultiplier ?? "—"}×`}
                          />
                        )}
                      </>
                    ) : runtimeState.frameGenerationEnabled ? (
                      t(
                        "LIVE_STATUS_FG_INACTIVE",
                        "On in settings, but not currently generating frames.",
                      )
                    ) : (
                      t("LIVE_STATUS_OFF", "Off")
                    )}
                    {runtimeState.frameGenerationPending && (
                      <StatusNotices>
                        {t(
                          "LIVE_STATUS_PENDING",
                          "A saved change is still applying or needs a restart.",
                        )}
                      </StatusNotices>
                    )}
                  </StatusRow>
                  <StatusRow
                    label={t("FEATURE_UPSCALING_TAB", "Upscaling")}
                    active={runtimeState.scalingActive}
                    separated
                  >
                    {runtimeState.scalingActive ? (
                      <>
                        <StatusDetail
                          label={t("LIVE_STATUS_MODEL", "Model")}
                          value={methodLabel(runtimeState.activeMethod)}
                        />
                        <StatusDetail
                          label={t("LIVE_STATUS_ORIGINAL_RESOLUTION", "Input")}
                          value={resolution(
                            runtimeState.sourceWidth,
                            runtimeState.sourceHeight,
                          )}
                        />
                        <StatusDetail
                          label={t("LIVE_STATUS_SCALED_RESOLUTION", "Output")}
                          value={
                            runtimeState.supersamplingActive
                              ? t(
                                  "LIVE_STATUS_SUPERSAMPLED_OUTPUT",
                                  "{presentation} → {target}",
                                  {
                                    presentation: resolution(
                                      runtimeState.presentationWidth,
                                      runtimeState.presentationHeight,
                                    ),
                                    target: resolution(
                                      runtimeState.gamescopeTargetWidth,
                                      runtimeState.gamescopeTargetHeight,
                                    ),
                                  },
                                )
                              : resolution(
                                  runtimeState.presentationWidth,
                                  runtimeState.presentationHeight,
                                )
                          }
                        />
                        <StatusDetail
                          label={t("LIVE_STATUS_MULTIPLIER", "Factor")}
                          value={`${runtimeState.effectiveFactor.toFixed(2)}×`}
                        />
                      </>
                    ) : runtimeState.scalingEnabled ? (
                      runtimeState.scalingActivationSupported === false ? (
                        t(
                          "LIVE_STATUS_SCALING_UNAVAILABLE",
                          "Unavailable for this running surface.",
                        )
                      ) : (
                        t(
                          "LIVE_STATUS_SCALING_INACTIVE",
                          "On in settings, but the game image is not being upscaled.",
                        )
                      )
                    ) : (
                      t("LIVE_STATUS_OFF", "Off")
                    )}
                    {(runtimeState.supersamplingActive ||
                      runtimeState.fallbackReason ||
                      runtimeState.scalingPending) && (
                      <StatusNotices>
                        {runtimeState.supersamplingActive && (
                          <div>
                            {t(
                              "LIVE_STATUS_SUPERSAMPLING",
                              "Quality Supersampling is on for a sharper final image.",
                            )}
                          </div>
                        )}
                        {runtimeState.fallbackReason && (
                          <div>
                            {t(
                              "LIVE_STATUS_SCALING_FALLBACK",
                              "You selected {requested}; MAKO is using {active} instead.",
                              {
                                requested: methodLabel(
                                  runtimeState.requestedMethod,
                                ),
                                active: methodLabel(runtimeState.activeMethod),
                              },
                            )}
                          </div>
                        )}
                        {runtimeState.scalingPending && (
                          <div>
                            {t(
                              "LIVE_STATUS_PENDING",
                              "A saved change is still applying or needs a restart.",
                            )}
                          </div>
                        )}
                      </StatusNotices>
                    )}
                  </StatusRow>
                </div>
              </>
            )}
          </div>
        </MakoSectionTail>
      </PanelSectionRow>
    </>
  );
}
