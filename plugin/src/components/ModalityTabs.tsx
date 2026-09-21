import { PanelSectionRow } from "@decky/ui";
import { useState, type CSSProperties } from "react";
import { FiFastForward, FiLayers, FiMaximize2 } from "react-icons/fi";
import type { IconType } from "react-icons";
import t from "../i18n/i18n";
import { MakoFocusable } from "./MakoUi";

export type ModalityId = "frame-generation" | "spatial" | "shaders";

interface ModalityTabsProps {
  activeModality: ModalityId;
  onModalityChange: (modality: ModalityId) => void;
}

interface ModalityOption {
  id: ModalityId;
  label: string;
  ribbonLabel: string;
  icon: IconType;
}

function modalityButtonStyle(
  active: boolean,
  revealed: boolean,
): CSSProperties {
  return {
    position: "relative",
    boxSizing: "border-box",
    flex: "1 1 0",
    minWidth: 0,
    height: "46px",
    margin: 0,
    padding: "0 4px",
    overflow: "hidden",
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    color: active ? "#d9f8ff" : revealed ? "#c4e8f1" : "#86aab4",
    background: active
      ? "linear-gradient(145deg, rgba(12, 78, 98, 0.98), rgba(8, 42, 64, 0.98) 62%, rgba(11, 58, 75, 0.98))"
      : revealed
        ? "linear-gradient(145deg, rgba(12, 55, 73, 0.94), rgba(7, 33, 51, 0.96))"
        : "linear-gradient(145deg, rgba(8, 38, 56, 0.9), rgba(5, 27, 43, 0.94))",
    border: active
      ? "1px solid rgba(98, 211, 230, 0.78)"
      : revealed
        ? "1px solid rgba(83, 171, 192, 0.62)"
        : "1px solid rgba(66, 128, 147, 0.34)",
    borderRadius: "8px",
    outline: revealed ? "1px solid rgba(112, 222, 237, 0.48)" : "none",
    outlineOffset: "1px",
    boxShadow: active
      ? "inset 0 1px 0 rgba(255, 255, 255, 0.12), inset 0 -12px 24px rgba(5, 20, 34, 0.34), 0 0 14px rgba(43, 171, 194, 0.24)"
      : revealed
        ? "inset 0 1px 0 rgba(255, 255, 255, 0.08), 0 0 9px rgba(42, 143, 163, 0.2)"
        : "inset 0 1px 0 rgba(255, 255, 255, 0.045), 0 2px 5px rgba(0, 0, 0, 0.2)",
    textShadow: "0 1px 2px rgba(0, 8, 18, 0.8)",
    transform: revealed ? "translateY(-1px)" : "translateY(0)",
    transition:
      "background 140ms ease, border-color 140ms ease, box-shadow 140ms ease, color 140ms ease, transform 140ms ease",
  };
}

export function ModalityTabs({
  activeModality,
  onModalityChange,
}: ModalityTabsProps) {
  const [revealedModality, setRevealedModality] = useState<ModalityId | null>(
    null,
  );
  const options: ModalityOption[] = [
    {
      id: "frame-generation",
      label: t("CONTENT_FPS_MULTIPLIER", "Frame Generation"),
      ribbonLabel: "FG",
      icon: FiFastForward,
    },
    {
      id: "spatial",
      label: t("CONTENT_SCALING", "Spatial Settings"),
      ribbonLabel: t("CONTENT_SCALING", "Spatial Settings"),
      icon: FiMaximize2,
    },
    {
      id: "shaders",
      label: t("CONTENT_SHADERS", "Shaders"),
      ribbonLabel: t("CONTENT_SHADERS", "Shaders"),
      icon: FiLayers,
    },
  ];

  const hideRibbon = (modality: ModalityId) => {
    setRevealedModality((current) => (current === modality ? null : current));
  };

  return (
    <PanelSectionRow>
      <div
        data-mako-modality-selector="true"
        style={{
          width: "100%",
          boxSizing: "border-box",
          margin: "14px 0 12px",
          padding: "7px",
          border: "1px solid rgba(70, 146, 168, 0.3)",
          borderRadius: "11px",
          background:
            "linear-gradient(155deg, rgba(5, 28, 44, 0.88), rgba(8, 49, 61, 0.48))",
          boxShadow:
            "inset 0 1px 0 rgba(255, 255, 255, 0.035), 0 4px 12px rgba(0, 9, 17, 0.2)",
        }}
      >
        <MakoFocusable
          role="tablist"
          aria-label={t("CONTENT_IMAGE_PROCESSING", "Image Processing")}
          flow-children="row"
          noFocusRing
          style={{
            width: "100%",
            display: "flex",
            alignItems: "stretch",
            gap: "7px",
          }}
        >
          {options.map((option) => {
            const active = activeModality === option.id;
            const revealed = revealedModality === option.id;
            const Icon = option.icon;
            return (
              <MakoFocusable
                key={option.id}
                role="tab"
                id={`mako-modality-tab-${option.id}`}
                aria-label={option.label}
                aria-selected={active}
                aria-controls={`mako-modality-panel-${option.id}`}
                data-modality={option.id}
                onClick={() => onModalityChange(option.id)}
                onActivate={() => onModalityChange(option.id)}
                onMouseEnter={() => setRevealedModality(option.id)}
                onMouseLeave={() => hideRibbon(option.id)}
                onFocus={() => setRevealedModality(option.id)}
                onBlur={() => hideRibbon(option.id)}
                onGamepadFocus={() => setRevealedModality(option.id)}
                onGamepadBlur={() => hideRibbon(option.id)}
                noFocusRing
                style={{
                  ...modalityButtonStyle(active, revealed),
                  flex: "1 1 0",
                  minWidth: 0,
                  width: "100%",
                }}
              >
                <span
                  style={{
                    position: "absolute",
                    width: "1px",
                    height: "1px",
                    padding: 0,
                    margin: "-1px",
                    overflow: "hidden",
                    clip: "rect(0, 0, 0, 0)",
                    whiteSpace: "nowrap",
                  }}
                >
                  {option.label}
                </span>
                <span
                  aria-hidden="true"
                  style={{
                    position: "absolute",
                    inset: "0 0 auto",
                    height: "2px",
                    opacity: active ? 1 : 0,
                    background:
                      "linear-gradient(90deg, transparent, #75e2ef 24%, #b9f6ff 50%, #75e2ef 76%, transparent)",
                    boxShadow: "0 0 8px rgba(83, 221, 235, 0.72)",
                    transition: "opacity 140ms ease",
                  }}
                />
                <Icon
                  aria-hidden="true"
                  size={20}
                  style={{
                    filter: active
                      ? "drop-shadow(0 0 5px rgba(91, 220, 235, 0.58))"
                      : "none",
                    transform: revealed
                      ? "translateY(-5px) scale(0.94)"
                      : "translateY(0) scale(1)",
                    transition: "transform 140ms ease, filter 140ms ease",
                  }}
                />
                <span
                  aria-hidden="true"
                  data-mako-modality-ribbon={option.id}
                  style={{
                    position: "absolute",
                    left: "4px",
                    right: "4px",
                    bottom: "3px",
                    minWidth: 0,
                    padding: "1px 3px",
                    overflow: "hidden",
                    border: "1px solid rgba(104, 201, 220, 0.28)",
                    borderRadius: "4px",
                    opacity: revealed ? 1 : 0,
                    color: "#e1f8fb",
                    background:
                      "linear-gradient(90deg, rgba(12, 74, 91, 0.88), rgba(18, 95, 106, 0.82), rgba(12, 74, 91, 0.88))",
                    boxShadow:
                      "inset 0 1px 0 rgba(255, 255, 255, 0.08), 0 1px 3px rgba(0, 0, 0, 0.24)",
                    fontSize: "7.5px",
                    fontWeight: 650,
                    lineHeight: 1.15,
                    letterSpacing: "0.1px",
                    textAlign: "center",
                    textOverflow: "ellipsis",
                    whiteSpace: "nowrap",
                    transform: revealed ? "translateY(0)" : "translateY(5px)",
                    transition: "opacity 130ms ease, transform 130ms ease",
                    pointerEvents: "none",
                  }}
                >
                  {option.ribbonLabel}
                </span>
              </MakoFocusable>
            );
          })}
        </MakoFocusable>
      </div>
    </PanelSectionRow>
  );
}
