import { PanelSectionRow } from "@decky/ui";
import { FiAlertCircle, FiCheckCircle } from "react-icons/fi";

interface StatusDisplayProps {
  dllDetected: boolean;
  dllDetectionStatus: string;
  isInstalled: boolean;
  installationStatus: string;
  topMargin?: string;
}

interface StatusRowProps {
  ready: boolean;
  text: string;
  separated?: boolean;
}

function StatusRow({ ready, text, separated = false }: StatusRowProps) {
  const accent = ready ? "#65b9c9" : "#c89558";
  const Icon = ready ? FiCheckCircle : FiAlertCircle;

  return (
    <div
      style={{
        minHeight: "38px",
        padding: "7px 10px",
        boxSizing: "border-box",
        display: "flex",
        alignItems: "center",
        gap: "9px",
        borderTop: separated ? "1px solid rgba(77, 170, 190, 0.16)" : "none",
        color: "#e6f2f5",
        fontSize: "13px",
        fontWeight: "500",
        lineHeight: "1.3"
      }}
    >
      <Icon
        aria-hidden="true"
        style={{
          width: "16px",
          height: "16px",
          flex: "0 0 16px",
          color: accent
        }}
      />
      <span>{text}</span>
    </div>
  );
}

export function StatusDisplay({
  dllDetected,
  dllDetectionStatus,
  isInstalled,
  installationStatus,
  topMargin = "0"
}: StatusDisplayProps) {
  return (
    <PanelSectionRow>
      <div
        style={{
          marginTop: topMargin,
          marginBottom: "0",
          width: "100%",
          boxSizing: "border-box",
          overflow: "hidden",
          background: "linear-gradient(135deg, rgba(7, 31, 49, 0.72), rgba(8, 55, 68, 0.46))",
          border: "1px solid rgba(77, 170, 190, 0.28)",
          borderRadius: "6px",
          boxShadow: "inset 0 1px 0 rgba(255, 255, 255, 0.035), 0 2px 5px rgba(0, 0, 0, 0.16)"
        }}
      >
        <StatusRow ready={dllDetected} text={dllDetectionStatus} />
        <StatusRow ready={isInstalled} text={installationStatus} separated />
      </div>
    </PanelSectionRow>
  );
}
