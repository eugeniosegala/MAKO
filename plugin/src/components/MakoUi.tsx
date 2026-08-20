import { PanelSectionRow, Spinner } from "@decky/ui";
import type { CSSProperties, ReactNode } from "react";

interface MakoSectionHeaderProps {
  children: ReactNode;
  description?: ReactNode;
  topMargin?: CSSProperties["marginTop"];
}

interface MakoReleaseIdentityProps {
  version: string;
  codename: string;
}

export const makoPanelDivider = "1px solid rgba(77, 170, 190, 0.2)";
export const makoDangerTextColor = "#d08aa0";
export const makoReleaseAccentColor = "#d58a39";

export const makoPanelStyle: CSSProperties = {
  overflow: "hidden",
  border: "1px solid rgba(77, 170, 190, 0.28)",
  borderRadius: "8px",
  background: "linear-gradient(135deg, rgba(7, 31, 49, 0.68), rgba(8, 55, 68, 0.38))",
  boxShadow: "inset 0 1px 0 rgba(255, 255, 255, 0.035), 0 2px 6px rgba(0, 0, 0, 0.18)"
};

export const makoPanelSectionHeaderStyle: CSSProperties = {
  padding: "12px 14px 9px",
  color: "#edf8fb",
  fontSize: "14px",
  fontWeight: 600,
  lineHeight: 1.25,
  letterSpacing: "0.15px"
};

export const makoPanelItemStyle: CSSProperties = {
  padding: "12px 14px",
  borderTop: makoPanelDivider
};

export function MakoReleaseIdentity({ version, codename }: MakoReleaseIdentityProps) {
  const codenameSlug = codename
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-|-$/g, "");

  return (
    <PanelSectionRow>
      <div
        aria-label={`Current release: MAKO Decky v${version}, ${codename}`}
        style={{
          width: "100%",
          boxSizing: "border-box",
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
          margin: "-2px 0 6px",
          padding: "2px 4px 9px",
          borderBottom: "1px solid rgba(77, 170, 190, 0.14)",
          color: "#a8bdc2",
          fontSize: "10px",
          fontWeight: 600,
          lineHeight: 1.2,
          letterSpacing: "0.55px",
          whiteSpace: "nowrap"
        }}
      >
        <span>v{version}</span>
        <span aria-hidden="true" style={{ padding: "0 6px", color: "#557f88" }}>-</span>
        <span style={{ color: makoReleaseAccentColor }}>{codenameSlug}</span>
      </div>
    </PanelSectionRow>
  );
}

export function MakoSectionHeader({
  children,
  description,
  topMargin = "32px"
}: MakoSectionHeaderProps) {
  return (
    <PanelSectionRow>
      <div
        style={{
          width: "100%",
          boxSizing: "border-box",
          marginTop: topMargin,
          marginBottom: "6px",
          color: "#edf8fb",
          fontSize: "14px",
          fontWeight: "600",
          lineHeight: "1.25",
          letterSpacing: "0.15px"
        }}
      >
        <div
          style={{
            paddingBottom: "8px",
            borderBottom: "1px solid rgba(77, 170, 190, 0.28)"
          }}
        >
          {children}
        </div>
        {description && (
          <div
            style={{
              marginTop: "8px",
              color: "#aebfc5",
              fontSize: "11px",
              fontWeight: "400",
              lineHeight: "1.35",
              letterSpacing: "normal"
            }}
          >
            {description}
          </div>
        )}
      </div>
    </PanelSectionRow>
  );
}

export function MakoCompactSpinner({ size = 18 }: { size?: number }) {
  return (
    <span
      aria-hidden="true"
      style={{
        width: `${size}px`,
        height: `${size}px`,
        display: "inline-flex",
        alignItems: "center",
        justifyContent: "center",
        flex: `0 0 ${size}px`,
        overflow: "hidden"
      }}
    >
      <Spinner
        width={size}
        height={size}
        style={{
          width: `${size}px`,
          height: `${size}px`,
          maxWidth: `${size}px`,
          maxHeight: `${size}px`,
          display: "block",
          flex: `0 0 ${size}px`
        }}
      />
    </span>
  );
}

export function makoDialogButtonStyle(
  isFocused: boolean,
  variant: "normal" | "danger" = "normal"
): CSSProperties {
  const danger = variant === "danger";
  return {
    color: danger ? "#fff0f5" : "#eefbfe",
    background: danger
      ? "linear-gradient(135deg, #3b1725 0%, #64253a 58%, #7d3048 100%)"
      : "linear-gradient(135deg, #071f31 0%, #0a4358 58%, #0b5967 100%)",
    border: danger
      ? "1px solid rgba(183, 82, 118, 0.62)"
      : "1px solid rgba(65, 158, 178, 0.62)",
    borderRadius: "4px",
    outline: isFocused ? "2px solid #d58a39" : "none",
    outlineOffset: "2px",
    boxShadow: isFocused
      ? "0 0 0 3px rgba(213, 138, 57, 0.2), 0 0 10px rgba(43, 142, 163, 0.28)"
      : "inset 0 1px 0 rgba(255, 255, 255, 0.08), 0 2px 5px rgba(0, 0, 0, 0.22)",
    transition: "background 120ms ease, box-shadow 120ms ease"
  };
}

export function MakoButtonTheme() {
  return (
    <style>{`
      .Mako_BrandButton button {
        color: #eefbfe !important;
        background: linear-gradient(135deg, #071f31 0%, #0a4358 58%, #0b5967 100%) !important;
        border: 1px solid rgba(65, 158, 178, 0.62) !important;
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.08), 0 2px 5px rgba(0, 0, 0, 0.22) !important;
        text-shadow: 0 1px 2px rgba(0, 13, 23, 0.72);
        transition: background 120ms ease, box-shadow 120ms ease, filter 120ms ease;
      }

      .Mako_BrandButton button:hover:not(:disabled) {
        background: linear-gradient(135deg, #092a40 0%, #0b5067 58%, #0d6875 100%) !important;
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.12), 0 0 9px rgba(39, 150, 171, 0.26) !important;
      }

      .Mako_BrandButton button:focus,
      .Mako_BrandButton button:focus-visible {
        outline: 2px solid #d58a39 !important;
        outline-offset: 2px !important;
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.12), 0 0 0 3px rgba(213, 138, 57, 0.2), 0 0 10px rgba(43, 142, 163, 0.28) !important;
      }

      .Mako_BrandButton button:disabled {
        filter: saturate(0.4) brightness(0.68);
      }

      .Mako_BrandButton--danger button {
        background: linear-gradient(135deg, #3b1725 0%, #64253a 58%, #7d3048 100%) !important;
        border-color: rgba(183, 82, 118, 0.62) !important;
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.07), 0 2px 5px rgba(0, 0, 0, 0.24) !important;
      }

      .Mako_BrandButton--danger button:hover:not(:disabled) {
        background: linear-gradient(135deg, #481b2c 0%, #732a43 58%, #913852 100%) !important;
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.1), 0 0 9px rgba(170, 57, 98, 0.24) !important;
      }
    `}</style>
  );
}
