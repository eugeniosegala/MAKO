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

/** One compact warning, including in the controls-only view. */
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
  if (!missingDll && !ls1Failed && !lsfgFailed) return null;

  return (
    <PanelSectionRow>
      <div role="alert" style={{ marginBottom: "6px" }}>
        <MakoInlineTip tone="warning" alwaysVisible>
          <div style={{ display: "flex", alignItems: "center", gap: "8px" }}>
            <span style={{ fontWeight: 700, flex: 1 }}>
              {t("MODEL_WARNING_TITLE", "Lossless Scaling")}
            </span>
            <DialogButton
              className="Mako_DialogButton"
              aria-label={
                missingDll
                  ? t(
                      "LOSSLESS_WARNING_OPEN_STEAM",
                      "Open Lossless Scaling in Steam",
                    )
                  : t(
                      "MODEL_WARNING_CHECK_UPDATES",
                      "Check for MAKO Decky updates",
                    )
              }
              style={{
                ...makoDialogButtonStyle(actionFocused),
                minWidth: 0,
                minHeight: "22px",
                height: "22px",
                width: "auto",
                padding: "2px 8px",
                fontSize: "10px",
                lineHeight: "16px",
              }}
              onGamepadFocus={() => setActionFocused(true)}
              onGamepadBlur={() => setActionFocused(false)}
              onClick={() =>
                Navigation.NavigateToExternalWeb(
                  missingDll
                    ? "https://store.steampowered.com/app/993090/Lossless_Scaling/"
                    : "https://github.com/eugeniosegala/MAKO/releases/latest",
                )
              }
            >
              {missingDll
                ? t("LOSSLESS_WARNING_INSTALL_ACTION", "Install")
                : t("MODEL_WARNING_UPDATES_ACTION", "Updates")}
            </DialogButton>
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
                  "Not found: Frame Generation and LS1 unavailable. MAKO Scaler and Shaders available.",
                )}
              </li>
            )}
            {!missingDll && ls1Failed && (
              <li>
                {ls1RuntimeFallback
                  ? t(
                      "MODEL_WARNING_LS1_ACTIVE",
                      "LS1 unavailable; MAKO Scaler active. LS1 selection kept.",
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
                  "Frame Generation unavailable for this model/precision.",
                )}
              </li>
            )}
          </ul>
        </MakoInlineTip>
      </div>
    </PanelSectionRow>
  );
}
