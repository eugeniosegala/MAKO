import { Focusable, PanelSectionRow, Spinner } from "@decky/ui";
import type { ComponentProps, CSSProperties, ReactNode } from "react";
import { FiAlertTriangle, FiInfo, FiLink } from "react-icons/fi";

interface MakoSectionHeaderProps {
  children: ReactNode;
  description?: ReactNode;
  topMargin?: CSSProperties["marginTop"];
}

interface MakoReleaseIdentityProps {
  version: string;
  codename: string;
  bottomMargin?: CSSProperties["marginBottom"];
}

interface MakoExperimentalSettingLabelProps {
  label: string;
  badgeLabel: string;
}

type SteamFocusFlow =
  "column" | "column-reverse" | "row" | "row-reverse" | "grid" | "geometric";

type MakoFocusableProps = Omit<
  ComponentProps<typeof Focusable>,
  "flow-children"
> & {
  "flow-children"?: SteamFocusFlow;
};

/** Restrict Decky's open string type to directions supported by Steam. */
export function MakoFocusable(props: MakoFocusableProps) {
  return <Focusable {...props} />;
}

export const makoPanelDivider = "1px solid rgba(77, 170, 190, 0.2)";
export const makoAccentColor = "#83bff0";
export const makoSectionGap = "26px";

export const makoPanelStyle: CSSProperties = {
  overflow: "hidden",
  border: "1px solid rgba(77, 170, 190, 0.28)",
  borderRadius: "8px",
  background:
    "linear-gradient(135deg, rgba(7, 31, 49, 0.68), rgba(8, 55, 68, 0.38))",
  boxShadow:
    "inset 0 1px 0 rgba(255, 255, 255, 0.035), 0 2px 6px rgba(0, 0, 0, 0.18)",
};

export const makoPanelSectionHeaderStyle: CSSProperties = {
  padding: "12px 14px 9px",
  color: "#edf8fb",
  fontSize: "14px",
  fontWeight: 600,
  lineHeight: 1.25,
  letterSpacing: "0.15px",
};

export const makoPanelItemStyle: CSSProperties = {
  padding: "12px 14px",
  borderTop: makoPanelDivider,
};

const trailingParentheticalPattern = /(\s*)(\([^()]+\)|（[^（）]+）)\s*$/u;

/** Render a translated restart-bound label while keeping its qualifier visually secondary. */
export function MakoRestartLabel({ label }: { label: string }) {
  const match = trailingParentheticalPattern.exec(label);
  if (!match || match.index === undefined) return <span>{label}</span>;

  return (
    <span>
      {label.slice(0, match.index)}
      {match[1]}
      <span
        data-mako-restart-marker="true"
        style={{
          fontSize: "0.72em",
          fontWeight: 500,
          opacity: 0.72,
          verticalAlign: "0.08em",
          whiteSpace: "nowrap",
        }}
      >
        {match[2]}
      </span>
    </span>
  );
}

/** Mark an intentionally early-access control without turning the label into a warning. */
export function MakoExperimentalBadge({ label }: { label: string }) {
  return (
    <span
      data-mako-experimental-badge="true"
      style={{
        display: "inline-flex",
        alignItems: "center",
        padding: "1px 5px",
        border: "1px solid rgba(244, 162, 89, 0.5)",
        borderRadius: "999px",
        background: "rgba(104, 59, 19, 0.42)",
        color: "#f7d9b4",
        fontSize: "0.62em",
        fontWeight: 600,
        lineHeight: 1.35,
        letterSpacing: "0.15px",
        textTransform: "uppercase",
        whiteSpace: "nowrap",
      }}
    >
      {label}
    </span>
  );
}

/** Keep experimental setting labels on one shared compact spacing rhythm. */
export function MakoExperimentalSettingLabel({
  label,
  badgeLabel,
}: MakoExperimentalSettingLabelProps) {
  return (
    <span
      data-mako-experimental-setting-label="true"
      style={{
        display: "inline-flex",
        alignItems: "center",
        columnGap: "6px",
        rowGap: "6px",
        flexWrap: "wrap",
      }}
    >
      <MakoRestartLabel label={label} />
      <MakoExperimentalBadge label={badgeLabel} />
    </span>
  );
}

/** Use info for context and potential performance effects; reserve warning for known added runtime cost. */
export function MakoInlineTip({
  children,
  tone = "info",
}: {
  children: ReactNode;
  tone?: "info" | "warning";
}) {
  const isWarning = tone === "warning";
  const accentColor = isWarning ? "#f4a259" : makoAccentColor;
  const Icon = isWarning ? FiAlertTriangle : FiInfo;
  return (
    <div
      role="note"
      data-tone={tone}
      style={{
        display: "flex",
        alignItems: "flex-start",
        gap: "6px",
        marginTop: "7px",
        padding: "6px 8px",
        border: isWarning
          ? "1px solid rgba(244, 162, 89, 0.34)"
          : "1px solid rgba(91, 163, 209, 0.24)",
        borderLeft: isWarning
          ? "2px solid rgba(244, 162, 89, 0.86)"
          : "2px solid rgba(131, 191, 240, 0.78)",
        borderRadius: "5px",
        background: isWarning
          ? "linear-gradient(90deg, rgba(104, 59, 19, 0.5), rgba(69, 37, 12, 0.2))"
          : "linear-gradient(90deg, rgba(24, 67, 94, 0.42), rgba(8, 39, 56, 0.18))",
        color: isWarning ? "#f7d9b4" : "#c8dce8",
        fontSize: "10px",
        fontWeight: 450,
        lineHeight: 1.35,
        letterSpacing: "0.05px",
      }}
    >
      <Icon
        aria-hidden="true"
        size={11}
        style={{
          flex: "0 0 11px",
          marginTop: "1px",
          color: accentColor,
        }}
      />
      <span style={{ minWidth: 0 }}>{children}</span>
    </div>
  );
}

export function MakoSettingRelationship({ children }: { children: ReactNode }) {
  return (
    <div
      data-mako-setting-relationship="true"
      style={{
        display: "flex",
        alignItems: "flex-start",
        gap: "5px",
        marginTop: "5px",
        color: "#93adb7",
        fontSize: "9.5px",
        fontWeight: 450,
        lineHeight: 1.35,
        letterSpacing: "0.03px",
      }}
    >
      <FiLink
        aria-hidden="true"
        size={10}
        style={{
          flex: "0 0 10px",
          marginTop: "1px",
          color: "rgba(131, 191, 240, 0.78)",
        }}
      />
      <span style={{ minWidth: 0 }}>{children}</span>
    </div>
  );
}

export function MakoReleaseIdentity({
  version,
  codename,
  bottomMargin = "2px",
}: MakoReleaseIdentityProps) {
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
          margin: "-2px 0 0",
          marginBottom: bottomMargin,
          padding: "2px 4px 4px",
          opacity: 0.5,
          color: "#a8bdc2",
          fontSize: "10px",
          fontWeight: 600,
          lineHeight: 1.2,
          letterSpacing: "0.55px",
          whiteSpace: "nowrap",
        }}
      >
        <span>v{version}</span>
        <span aria-hidden="true" style={{ padding: "0 6px", color: "#557f88" }}>
          -
        </span>
        <span style={{ color: makoAccentColor }}>{codenameSlug}</span>
      </div>
    </PanelSectionRow>
  );
}

export function MakoSectionHeader({
  children,
  description,
  topMargin = makoSectionGap,
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
          letterSpacing: "0.15px",
        }}
      >
        <div
          style={{
            paddingBottom: "8px",
            borderBottom: "4px solid rgba(77, 170, 190, 0.48)",
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
              letterSpacing: "normal",
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
        overflow: "hidden",
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
          flex: `0 0 ${size}px`,
        }}
      />
    </span>
  );
}

export function makoDialogButtonStyle(
  isFocused: boolean,
  variant: "normal" | "danger" = "normal",
): CSSProperties {
  const danger = variant === "danger";
  const focusColor = danger ? "#e36a79" : "#52d5e8";
  return {
    color: danger ? "#fff0f5" : "#eefbfe",
    background: danger
      ? "linear-gradient(135deg, #3b1725 0%, #64253a 58%, #7d3048 100%)"
      : "linear-gradient(135deg, #071f31 0%, #0a4358 58%, #0b5967 100%)",
    border: danger
      ? "1px solid rgba(183, 82, 118, 0.62)"
      : "1px solid rgba(65, 158, 178, 0.62)",
    borderRadius: "4px",
    outline: isFocused ? `2px solid ${focusColor}` : "none",
    outlineOffset: "2px",
    boxShadow: isFocused
      ? danger
        ? "0 0 0 3px rgba(227, 106, 121, 0.2), 0 0 10px rgba(166, 48, 72, 0.34)"
        : "0 0 0 3px rgba(82, 213, 232, 0.2), 0 0 10px rgba(43, 142, 163, 0.32)"
      : "inset 0 1px 0 rgba(255, 255, 255, 0.08), 0 2px 5px rgba(0, 0, 0, 0.22)",
    transition: "background 120ms ease, box-shadow 120ms ease",
  };
}

export function MakoButtonTheme() {
  return (
    <style>{`
      .Mako_DialogButton:not(.disabled):not([disabled]):not([aria-disabled="true"]):hover,
      .Mako_DialogButton button:hover:not(:disabled) {
        background: linear-gradient(135deg, #092a40 0%, #0b5067 58%, #0d6875 100%) !important;
        border-color: rgba(79, 188, 209, 0.78) !important;
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.12), 0 0 9px rgba(39, 150, 171, 0.26) !important;
      }

      .Mako_DialogButton--danger:not(.disabled):not([disabled]):not([aria-disabled="true"]):hover,
      .Mako_DialogButton--danger button:hover:not(:disabled) {
        background: linear-gradient(135deg, #481b2c 0%, #732a43 58%, #913852 100%) !important;
        border-color: rgba(208, 102, 139, 0.78) !important;
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.1), 0 0 9px rgba(170, 57, 98, 0.24) !important;
      }

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
        outline: 2px solid #52d5e8 !important;
        outline-offset: 2px !important;
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.12), 0 0 0 3px rgba(82, 213, 232, 0.2), 0 0 10px rgba(43, 142, 163, 0.32) !important;
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

      .Mako_BrandButton--danger button:focus,
      .Mako_BrandButton--danger button:focus-visible {
        outline: 2px solid #e36a79 !important;
        box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.1), 0 0 0 3px rgba(227, 106, 121, 0.2), 0 0 10px rgba(166, 48, 72, 0.34) !important;
      }
    `}</style>
  );
}
