import t from "../i18n/i18n";
import { MAKO_INSTALL_COMPLETION_DURATION_MS } from "../config/uiTiming";

interface MakoInstallCountdownProps {
  durationMs: number;
}

export function MakoInstallCountdown({
  durationMs,
}: MakoInstallCountdownProps) {
  return (
    <svg
      aria-hidden="true"
      width="24"
      height="24"
      viewBox="0 0 24 24"
      style={{ display: "block", transform: "rotate(-90deg)" }}
    >
      <circle
        cx="12"
        cy="12"
        r="9"
        fill="none"
        stroke="rgba(208, 138, 160, 0.24)"
        strokeWidth="2.5"
      />
      <circle
        cx="12"
        cy="12"
        r="9"
        pathLength="1"
        fill="none"
        stroke="#d08aa0"
        strokeWidth="2.5"
        strokeLinecap="round"
        strokeDasharray="1"
        strokeDashoffset="0"
      >
        <animate
          attributeName="stroke-dashoffset"
          from="0"
          to="1"
          dur={`${durationMs}ms`}
          fill="freeze"
        />
      </circle>
    </svg>
  );
}

export function MakoInstallCompletion() {
  return (
    <div style={{ display: "flex", alignItems: "center", gap: "10px" }}>
      <MakoInstallCountdown durationMs={MAKO_INSTALL_COMPLETION_DURATION_MS} />
      <div
        style={{
          display: "flex",
          flexDirection: "column",
          alignItems: "flex-start",
          gap: "2px",
        }}
      >
        <div style={{ fontWeight: 600 }}>
          {t("TOAST_INSTALL_COMPLETE", "Installation Complete")}
        </div>
        <div style={{ fontSize: "12px", opacity: 0.82 }}>
          {t(
            "TOAST_INSTALL_COMPLETE_DESC",
            "Restarting your device is recommended.",
          )}
        </div>
      </div>
    </div>
  );
}
