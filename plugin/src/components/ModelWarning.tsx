import { DialogButton, Navigation, PanelSectionRow } from "@decky/ui";
import { useState } from "react";
import type { ModelStatusResult } from "../api/makoApi";
import { MakoInlineTip, makoDialogButtonStyle } from "./MakoUi";
import t from "../i18n/i18n";

export interface ModelWarningProps {
  ls1?: ModelStatusResult | null;
  lsfg?: ModelStatusResult | null;
  ls1RuntimeFallback?: boolean;
  dllMissing?: boolean;
}

/** One compact failure warning or inspection notice, including with info hidden. */
export function ModelWarning({
  ls1,
  lsfg,
  ls1RuntimeFallback = false,
  dllMissing = false,
}: ModelWarningProps) {
  const [actionFocused, setActionFocused] = useState(false);
  const ls1Failed = ls1RuntimeFallback || ls1?.compatible === false;
  const lsfgFailed = lsfg?.compatible === false;
  const missingDll =
    dllMissing ||
    (ls1Failed && ls1?.reason === "dll-unavailable") ||
    (lsfgFailed && lsfg?.reason === "dll-unavailable");
  const inspectionUnavailable =
    (!ls1Failed && ls1?.compatible == null && ls1?.reason === "inspection-unavailable") ||
    (!lsfgFailed && lsfg?.compatible == null && lsfg?.reason === "inspection-unavailable");
  const failed = missingDll || ls1Failed || lsfgFailed;
  if (!failed && !inspectionUnavailable) return null;

  return (
    <PanelSectionRow>
      <div role={failed ? "alert" : "status"} style={{ marginBottom: "6px" }}>
        <MakoInlineTip tone={failed ? "warning" : "info"} alwaysVisible>
          <div style={{ fontWeight: 700 }}>
            {t("MODEL_WARNING_TITLE", "Lossless Scaling (optional)")}
          </div>
          <ul
            style={{
              margin: "4px 0 0",
              paddingLeft: "16px",
              display: "grid",
              gap: "3px",
            }}
          >
            {missingDll && (
              <li>
                {t(
                  "LOSSLESS_WARNING_REQUIRED",
                  "Lossless Scaling unavailable. Unavailable models switch to MAKO automatically. FG model changes need a game restart; Shaders remain available.",
                )}
              </li>
            )}
            {!missingDll && ls1Failed && (
              <li>
                {ls1RuntimeFallback
                  ? t(
                      "MODEL_WARNING_LS1_ACTIVE",
                      "LS1 unavailable; MAKO Scaler active.",
                    )
                  : t(
                      "MODEL_WARNING_LS1",
                      "LS1 unavailable for this model; MAKO Scaler fallback.",
                    )}
              </li>
            )}
            {!missingDll && lsfgFailed && (
              <li>
                {t(
                  "MODEL_WARNING_LSFG",
                  "Lossless Scaling Frame Generation unavailable for this model/precision. MAKO FG selected automatically; a game restart applies the model change.",
                )}
              </li>
            )}
            {!missingDll && inspectionUnavailable && (
              <li>
                {t(
                  "MODEL_WARNING_INSPECTION_UNAVAILABLE",
                  "Lossless Scaling compatibility could not be fully verified. Unverified selections are unchanged.",
                )}
              </li>
            )}
          </ul>
        </MakoInlineTip>
        {!missingDll && failed && (
          <DialogButton
            className="Mako_DialogButton"
            aria-label={t(
              "MODEL_WARNING_CHECK_UPDATES",
              "Check for MAKO Decky updates",
            )}
            style={{
              ...makoDialogButtonStyle(actionFocused),
              boxSizing: "border-box",
              minWidth: 0,
              minHeight: "32px",
              height: "auto",
              width: "100%",
              marginTop: "8px",
              padding: "6px 12px",
              fontSize: "12px",
              lineHeight: "18px",
              whiteSpace: "normal",
            }}
            onGamepadFocus={() => setActionFocused(true)}
            onGamepadBlur={() => setActionFocused(false)}
            onClick={() =>
              Navigation.NavigateToExternalWeb(
                "https://github.com/eugeniosegala/MAKO/releases/latest",
              )
            }
          >
            {t("MODEL_WARNING_UPDATES_ACTION", "Updates")}
          </DialogButton>
        )}
      </div>
    </PanelSectionRow>
  );
}
