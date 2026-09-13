import { MakoInfo } from "./MakoInfo";
import { ButtonItem, Navigation, PanelSectionRow } from "@decky/ui";
import type { ModelStatusResult } from "../api/makoApi";
import { MakoInlineTip } from "./MakoUi";
import t from "../i18n/i18n";

export interface ModelWarningProps {
  ls1?: ModelStatusResult | null;
  lsfg?: ModelStatusResult | null;
  ls1RuntimeFallback?: boolean;
}

/** Report confirmed model failures without blocking controls or changing profiles. */
export function ModelWarning({
  ls1,
  lsfg,
  ls1RuntimeFallback = false,
}: ModelWarningProps) {
  const ls1Failed = ls1RuntimeFallback || ls1?.compatible === false;
  const lsfgFailed = lsfg?.compatible === false;
  if (!ls1Failed && !lsfgFailed) return null;
  const missingDll =
    (ls1Failed && ls1?.reason === "dll-unavailable") ||
    (lsfgFailed && lsfg?.reason === "dll-unavailable");

  return (
    <MakoInfo as={PanelSectionRow}>
      <div role="alert" data-mako-info="true" style={{ marginBottom: "8px" }}>
        <MakoInlineTip tone="warning">
          <div style={{ fontWeight: 700, marginBottom: "6px" }}>
            {t("MODEL_WARNING_TITLE", "Lossless Scaling model warning")}
          </div>
          {missingDll ? (
            <div>
              {t(
                "MODEL_WARNING_DLL_MISSING",
                "Lossless.dll could not be found. Check its configured path or install Lossless Scaling through Steam.",
              )}
            </div>
          ) : (
            <>
              {ls1Failed && (
                <div>
                  {ls1RuntimeFallback
                    ? t(
                        "SCALING_LS1_ACTIVE_FALLBACK",
                        "LS1 is unavailable for this game. MAKO Scaler is active. Your LS1 selection is preserved.",
                      )
                    : t(
                        "MODEL_WARNING_LS1",
                        "LS1 failed its availability check. MAKO Scaler is used automatically if LS1 cannot load.",
                      )}
                </div>
              )}
              {lsfgFailed && (
                <div>
                  {t(
                    "MODEL_WARNING_LSFG",
                    "An LSFG model check failed. Frame Generation may be unavailable with the selected precision setting.",
                  )}
                </div>
              )}
              <div style={{ marginTop: "6px" }}>
                {t(
                  "MODEL_WARNING_UPDATE",
                  "Check for MAKO Decky updates, then apply any MAKO Renderer update and restart the game. If the problem persists, verify Lossless Scaling and collect diagnostics.",
                )}
              </div>
            </>
          )}
        </MakoInlineTip>
        {!missingDll && (
          <ButtonItem
            layout="below"
            onClick={() =>
              Navigation.NavigateToExternalWeb(
                "https://github.com/eugeniosegala/MAKO/releases/latest",
              )
            }
          >
            {t("MODEL_WARNING_CHECK_UPDATES", "Check for MAKO Decky updates")}
          </ButtonItem>
        )}
      </div>
    </MakoInfo>
  );
}
