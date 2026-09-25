import { useEffect, useLayoutEffect, useRef, useState } from "react";
import {
  Dropdown,
  Field,
  GamepadButton,
  PanelSectionRow,
  SliderField,
  ToggleField,
  type GamepadEvent,
} from "@decky/ui";
import { RiArrowDownSFill, RiArrowUpSFill } from "react-icons/ri";
import {
  EXTERNAL_VULKAN_LAYER_NONE,
  EXTERNAL_VULKAN_LAYER_VKBASALT,
  EXTERNAL_VULKAN_LAYER,
  VKBASALT_ANTIALIASING,
  VKBASALT_ANTIALIASING_FXAA,
  VKBASALT_ANTIALIASING_NONE,
  VKBASALT_ANTIALIASING_SMAA,
  VKBASALT_DLS_DENOISE,
  VKBASALT_SHARPENING,
  VKBASALT_SHARPENING_CAS,
  VKBASALT_SHARPENING_DLS,
  VKBASALT_SHARPENING_NONE,
  VKBASALT_SHARPNESS,
  VKBASALT_SHADER,
  VKBASALT_SHADER_BLEACH_BYPASS,
  VKBASALT_SHADER_CARTOON,
  VKBASALT_SHADER_CHROMATIC_ABERRATION,
  VKBASALT_SHADER_CLARITY,
  VKBASALT_SHADER_COLOURFULNESS,
  VKBASALT_SHADER_CURVES,
  VKBASALT_SHADER_DEBAND,
  VKBASALT_SHADER_DPX,
  VKBASALT_SHADER_FILM_GRAIN,
  VKBASALT_SHADER_HDR_LOOK,
  VKBASALT_SHADER_LEVELS_PLUS,
  VKBASALT_SHADER_MONOCHROME,
  VKBASALT_SHADER_NOIR,
  VKBASALT_SHADER_NONE,
  VKBASALT_SHADER_NOSTALGIA,
  VKBASALT_SHADER_SEPIA,
  VKBASALT_SHADER_TECHNICOLOR,
  VKBASALT_SHADER_TECHNICOLOR2,
  VKBASALT_SHADER_VIBRANCE,
  VKBASALT_SHADER_VIGNETTE,
  VKBASALT_STRENGTH_MAX,
  VKBASALT_STRENGTH_MIN,
} from "../../config/configSchema";
import t from "../../i18n/i18n";
import { findFocusScrollContainer } from "../../utils/focusScrollUtils";
import {
  MakoExperimentalSettingLabel,
  MakoFocusable,
  MakoInlineTip,
} from "../MakoUi";
import type { ConfigurationControlProps } from "./types";

interface ShadersConfigurationGroupProps extends ConfigurationControlProps {
  isDefaultProfile: boolean;
  profileName: string;
  vkBasaltConfigPath: string;
}

interface EffectOption {
  data: string;
  label: string;
}

const EFFECTS_PER_PAGE = 5;
const EFFECT_ROW_HEIGHT = 38;
const EFFECT_ROW_GAP = 2;

type EffectsAction = "summary" | "pager" | "clear";

function EffectsChecklist({
  options,
  initialSelection,
  onChange,
}: {
  options: EffectOption[];
  initialSelection: string[];
  onChange: (value: string) => Promise<void>;
}) {
  const [selected, setSelected] = useState(initialSelection);
  const [expanded, setExpanded] = useState(false);
  const [page, setPage] = useState(0);
  const [focusedEffect, setFocusedEffect] = useState<string | null>(null);
  const [hoveredEffect, setHoveredEffect] = useState<string | null>(null);
  const [focusedAction, setFocusedAction] = useState<EffectsAction | null>(
    null,
  );
  const [hoveredAction, setHoveredAction] = useState<EffectsAction | null>(
    null,
  );
  const checklistRef = useRef<HTMLDivElement>(null);
  const focusAnchor = useRef<{
    target: HTMLElement;
    top: number;
    scroller: HTMLElement | null;
  }>();
  const pendingPageFocusRow = useRef<number>();
  const pendingSave = useRef(Promise.resolve());
  const selectionValue = initialSelection.join(":");
  const effects = options.filter(
    (option) => option.data !== VKBASALT_SHADER_NONE,
  );
  const pageCount = Math.max(1, Math.ceil(effects.length / EFFECTS_PER_PAGE));

  useEffect(() => {
    setSelected(initialSelection);
  }, [selectionValue]);

  useLayoutEffect(() => {
    if (expanded) return;
    const anchor = focusAnchor.current;
    if (!anchor) return;
    if (anchor.target.isConnected) {
      anchor.target.focus({ preventScroll: true });
      if (anchor.scroller) {
        anchor.scroller.scrollTop +=
          anchor.target.getBoundingClientRect().top - anchor.top;
      }
    }
    focusAnchor.current = undefined;
  }, [expanded]);

  useLayoutEffect(() => {
    const requestedRow = pendingPageFocusRow.current;
    if (requestedRow === undefined) return;
    const effectCount = Math.min(
      EFFECTS_PER_PAGE,
      Math.max(0, effects.length - page * EFFECTS_PER_PAGE),
    );
    if (effectCount === 0) {
      pendingPageFocusRow.current = undefined;
      return;
    }
    const targetRow = Math.min(requestedRow, effectCount - 1);
    const target = checklistRef.current?.querySelector<HTMLElement>(
      `[data-mako-effect-row="${targetRow}"]`,
    );
    target?.focus({ preventScroll: true });
    pendingPageFocusRow.current = undefined;
  }, [effects.length, page]);

  const updateSelection = (next: string[]) => {
    setSelected(next);
    const value = next.join(":") || VKBASALT_SHADER_NONE;
    pendingSave.current = pendingSave.current
      .catch(() => {})
      .then(() => onChange(value));
  };

  const closeAfterFocusLeaves = () => {
    if (!expanded) return;
    window.setTimeout(() => {
      const checklist = checklistRef.current;
      const activeElement = checklist?.ownerDocument
        .activeElement as HTMLElement | null;
      if (
        checklist &&
        !checklist.contains(activeElement) &&
        !checklist.querySelector(".mako-effects-focus-within")
      ) {
        if (activeElement && activeElement !== checklist.ownerDocument.body) {
          focusAnchor.current = {
            target: activeElement,
            top: activeElement.getBoundingClientRect().top,
            scroller: findFocusScrollContainer(activeElement),
          };
        }
        setExpanded(false);
        setFocusedEffect(null);
        setFocusedAction(null);
      }
    }, 0);
  };

  const actionStyle = (action: EffectsAction, disabled = false) => {
    const highlighted = focusedAction === action || hoveredAction === action;
    return {
      boxSizing: "border-box" as const,
      display: "grid",
      placeItems: "center",
      height: "34px",
      minHeight: "34px",
      padding: "0 9px",
      border: "1px solid rgba(77, 170, 190, 0.3)",
      borderRadius: "5px",
      background: highlighted
        ? "rgba(62, 130, 156, 0.62)"
        : "rgba(18, 48, 65, 0.46)",
      outline: highlighted ? "2px solid #83bff0" : "2px solid transparent",
      outlineOffset: "-2px",
      color: "#edf8fb",
      textAlign: "center" as const,
      lineHeight: 1,
      opacity: disabled ? 0.45 : 1,
    };
  };

  const actionFocusProps = (action: EffectsAction) => ({
    onFocus: () => setFocusedAction(action),
    onBlur: () => setFocusedAction(null),
    onGamepadFocus: () => setFocusedAction(action),
    onGamepadBlur: () => setFocusedAction(null),
    onMouseEnter: () => setHoveredAction(action),
    onMouseLeave: () => setHoveredAction(null),
  });

  const changePage = (direction: -1 | 1) => {
    setPage((current) =>
      Math.min(pageCount - 1, Math.max(0, current + direction)),
    );
  };

  const onPageButtonDown = (event: GamepadEvent) => {
    if (
      event.detail.button !== GamepadButton.DIR_LEFT &&
      event.detail.button !== GamepadButton.DIR_RIGHT
    ) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    changePage(event.detail.button === GamepadButton.DIR_LEFT ? -1 : 1);
  };

  const changePageFromEffect = (direction: -1 | 1, row: number) => {
    const nextPage = Math.min(pageCount - 1, Math.max(0, page + direction));
    if (nextPage === page) return;
    const nextPageEffectCount = Math.min(
      EFFECTS_PER_PAGE,
      Math.max(0, effects.length - nextPage * EFFECTS_PER_PAGE),
    );
    const targetRow = Math.min(row, Math.max(0, nextPageEffectCount - 1));
    const targetEffect = effects[nextPage * EFFECTS_PER_PAGE + targetRow];
    if (!targetEffect) return;
    if (targetRow !== row) {
      pendingPageFocusRow.current = targetRow;
    }
    setFocusedEffect(targetEffect.data);
    setHoveredEffect(null);
    setPage(nextPage);
  };

  const onEffectPageButtonDown = (event: GamepadEvent, row: number) => {
    if (
      event.detail.button !== GamepadButton.DIR_LEFT &&
      event.detail.button !== GamepadButton.DIR_RIGHT
    ) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    changePageFromEffect(
      event.detail.button === GamepadButton.DIR_LEFT ? -1 : 1,
      row,
    );
  };

  const summaryHighlighted =
    focusedAction === "summary" || hoveredAction === "summary";

  const collapseToSummary = () => {
    checklistRef.current
      ?.querySelector<HTMLElement>('[data-mako-effects-summary="true"]')
      ?.focus({ preventScroll: true });
    setExpanded(false);
    setFocusedEffect(null);
    setFocusedAction("summary");
  };

  const handleExpandedCancel = (event: CustomEvent) => {
    event.preventDefault();
    event.stopPropagation();
    collapseToSummary();
  };

  return (
    <div
      ref={checklistRef}
      data-testid="mako-effects-selector"
      style={{
        boxSizing: "border-box",
        width: "100%",
        marginTop: "8px",
        paddingBottom: expanded ? "8px" : "0px",
      }}
    >
      <MakoFocusable
        flow-children="column"
        focusWithinClassName="mako-effects-focus-within"
        onBlur={closeAfterFocusLeaves}
        onGamepadBlur={closeAfterFocusLeaves}
        {...(expanded ? { onCancel: handleExpandedCancel } : {})}
        style={{ width: "100%" }}
      >
        <MakoFocusable
          data-mako-effects-summary="true"
          role="button"
          tabIndex={0}
          aria-expanded={expanded}
          onClick={() => setExpanded(!expanded)}
          onActivate={() => setExpanded(!expanded)}
          {...actionFocusProps("summary")}
          style={{
            display: "flex",
            alignItems: "center",
            justifyContent: "space-between",
            boxSizing: "border-box",
            width: "100%",
            minHeight: "40px",
            padding: "7px 12px",
            border: summaryHighlighted
              ? "1px solid rgba(131, 191, 240, 0.8)"
              : "1px solid rgba(77, 170, 190, 0.32)",
            borderRadius: "7px",
            background: summaryHighlighted
              ? "rgba(62, 130, 156, 0.62)"
              : "rgba(18, 48, 65, 0.72)",
            outline: summaryHighlighted
              ? "2px solid #83bff0"
              : "2px solid transparent",
            outlineOffset: "-2px",
            color: "#edf8fb",
            fontSize: "14px",
          }}
        >
          <span
            style={{
              display: "flex",
              alignItems: "center",
              justifyContent: "space-between",
              width: "100%",
            }}
          >
            <span>
              {expanded
                ? t("CONFIG_VKBASALT_EFFECTS_DONE", "Done")
                : t(
                    "CONFIG_VKBASALT_EFFECTS_SELECTED",
                    "Choose effects ({value} selected)",
                    {
                      value: selected.length,
                    },
                  )}
            </span>
            {expanded ? (
              <RiArrowUpSFill aria-hidden="true" />
            ) : (
              <RiArrowDownSFill aria-hidden="true" />
            )}
          </span>
        </MakoFocusable>
        {expanded && (
          <div
            data-testid="mako-effects-list"
            style={{
              width: "100%",
              boxSizing: "border-box",
              marginTop: "6px",
              padding: "4px",
              border: "1px solid rgba(77, 170, 190, 0.22)",
              borderRadius: "7px",
              background: "rgba(7, 31, 49, 0.48)",
            }}
          >
            <MakoFocusable
              flow-children="column"
              style={{ display: "flex", flexDirection: "column", gap: "2px" }}
            >
              <div
                data-testid="mako-effects-page"
                style={{
                  display: "flex",
                  flexDirection: "column",
                  gap: `${EFFECT_ROW_GAP}px`,
                  height: `${
                    EFFECTS_PER_PAGE * EFFECT_ROW_HEIGHT +
                    (EFFECTS_PER_PAGE - 1) * EFFECT_ROW_GAP
                  }px`,
                }}
              >
                {effects
                  .slice(page * EFFECTS_PER_PAGE, (page + 1) * EFFECTS_PER_PAGE)
                  .map((option, row) => {
                    const order = selected.indexOf(option.data);
                    const enabled = order !== -1;
                    const highlighted =
                      focusedEffect === option.data ||
                      hoveredEffect === option.data;
                    const toggleEffect = () =>
                      updateSelection(
                        enabled
                          ? selected.filter((effect) => effect !== option.data)
                          : [...selected, option.data],
                      );
                    return (
                      <MakoFocusable
                        key={row}
                        role="checkbox"
                        tabIndex={0}
                        data-mako-effect-row={row}
                        aria-checked={enabled}
                        aria-label={
                          enabled
                            ? `${order + 1}. ${option.label}`
                            : option.label
                        }
                        onClick={toggleEffect}
                        onActivate={toggleEffect}
                        onButtonDown={(event) =>
                          onEffectPageButtonDown(event, row)
                        }
                        onKeyDown={(event) => {
                          if (
                            event.key !== "ArrowLeft" &&
                            event.key !== "ArrowRight"
                          ) {
                            return;
                          }
                          event.preventDefault();
                          event.stopPropagation();
                          changePageFromEffect(
                            event.key === "ArrowLeft" ? -1 : 1,
                            row,
                          );
                        }}
                        onFocus={() => setFocusedEffect(option.data)}
                        onBlur={() => setFocusedEffect(null)}
                        onGamepadFocus={() => setFocusedEffect(option.data)}
                        onGamepadBlur={() => setFocusedEffect(null)}
                        onMouseEnter={() => setHoveredEffect(option.data)}
                        onMouseLeave={() => setHoveredEffect(null)}
                        style={{
                          display: "flex",
                          alignItems: "center",
                          justifyContent: "space-between",
                          boxSizing: "border-box",
                          width: "100%",
                          height: `${EFFECT_ROW_HEIGHT}px`,
                          minHeight: `${EFFECT_ROW_HEIGHT}px`,
                          flex: `0 0 ${EFFECT_ROW_HEIGHT}px`,
                          padding: "6px 9px",
                          borderRadius: "5px",
                          background: highlighted
                            ? "rgba(62, 130, 156, 0.62)"
                            : enabled
                              ? "rgba(49, 108, 132, 0.38)"
                              : "transparent",
                          outline: highlighted
                            ? "2px solid #83bff0"
                            : "2px solid transparent",
                          outlineOffset: "-2px",
                          color: "#edf8fb",
                          fontSize: "13px",
                        }}
                      >
                        <span
                          style={{
                            minWidth: 0,
                            overflow: "hidden",
                            textOverflow: "ellipsis",
                            whiteSpace: "nowrap",
                          }}
                        >
                          {enabled
                            ? `${order + 1}. ${option.label}`
                            : option.label}
                        </span>
                        <span
                          aria-hidden="true"
                          style={{
                            display: "grid",
                            placeItems: "center",
                            width: "18px",
                            height: "18px",
                            flex: "0 0 18px",
                            marginLeft: "8px",
                            border: `1px solid ${enabled ? "#83bff0" : "rgba(198, 226, 238, 0.5)"}`,
                            borderRadius: "4px",
                            background: enabled ? "#3b8cad" : "transparent",
                            fontSize: "12px",
                            fontWeight: 700,
                          }}
                        >
                          {enabled ? "✓" : ""}
                        </span>
                      </MakoFocusable>
                    );
                  })}
              </div>
              <MakoFocusable
                role="button"
                tabIndex={0}
                aria-label={t("CONFIG_VKBASALT_SHADER", "Effects")}
                aria-description={`${page + 1} / ${pageCount}`}
                onActivate={() => changePage(1)}
                onButtonDown={onPageButtonDown}
                onKeyDown={(event) => {
                  if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") {
                    return;
                  }
                  event.preventDefault();
                  event.stopPropagation();
                  changePage(event.key === "ArrowLeft" ? -1 : 1);
                }}
                {...actionFocusProps("pager")}
                style={{
                  ...actionStyle("pager"),
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "space-between",
                  gap: "6px",
                  width: "100%",
                  marginTop: "5px",
                  padding: 0,
                }}
              >
                <span
                  aria-hidden="true"
                  data-testid="mako-effects-previous-page"
                  title={t(
                    "CONFIG_VKBASALT_EFFECTS_PREVIOUS_PAGE",
                    "Previous effects page",
                  )}
                  onClick={() => changePage(-1)}
                  style={{
                    display: "grid",
                    placeItems: "center",
                    alignSelf: "stretch",
                    width: "42px",
                    cursor: page === 0 ? "default" : "pointer",
                    opacity: page === 0 ? 0.45 : 1,
                  }}
                >
                  ‹
                </span>
                <span
                  aria-live="polite"
                  style={{ fontSize: "12px", opacity: 0.75 }}
                >
                  {page + 1} / {pageCount}
                </span>
                <span
                  aria-hidden="true"
                  data-testid="mako-effects-next-page"
                  title={t(
                    "CONFIG_VKBASALT_EFFECTS_NEXT_PAGE",
                    "Next effects page",
                  )}
                  onClick={() => changePage(1)}
                  style={{
                    display: "grid",
                    placeItems: "center",
                    alignSelf: "stretch",
                    width: "42px",
                    cursor: page === pageCount - 1 ? "default" : "pointer",
                    opacity: page === pageCount - 1 ? 0.45 : 1,
                  }}
                >
                  ›
                </span>
              </MakoFocusable>
              <MakoFocusable
                role="button"
                tabIndex={0}
                aria-disabled={selected.length === 0}
                onClick={() => selected.length > 0 && updateSelection([])}
                onActivate={() => selected.length > 0 && updateSelection([])}
                {...actionFocusProps("clear")}
                style={{
                  ...actionStyle("clear", selected.length === 0),
                  alignSelf: "stretch",
                  width: "100%",
                  marginTop: "8px",
                  fontSize: "12px",
                }}
              >
                {t("CONFIG_VKBASALT_EFFECTS_CLEAR", "Clear all")}
              </MakoFocusable>
            </MakoFocusable>
          </div>
        )}
      </MakoFocusable>
    </div>
  );
}

export function ShadersConfigurationGroup({
  config,
  isDefaultProfile,
  profileName,
  vkBasaltConfigPath,
  onConfigChange,
}: ShadersConfigurationGroupProps) {
  const vkBasaltEnabled =
    config.external_vulkan_layer === EXTERNAL_VULKAN_LAYER_VKBASALT;
  const sharpeningEnabled =
    config.vkbasalt_sharpening !== VKBASALT_SHARPENING_NONE;
  const displayedConfigPath =
    vkBasaltConfigPath ||
    (isDefaultProfile
      ? "~/.config/vkBasalt/vkBasalt.conf"
      : "~/.config/mako-render/vkbasalt/<profile>.conf");
  const selectedEffects =
    config.vkbasalt_shader === VKBASALT_SHADER_NONE
      ? []
      : config.vkbasalt_shader.split(":");
  const sharpeningOptions = [
    {
      data: VKBASALT_SHARPENING_NONE,
      label: t("CONFIG_VKBASALT_EFFECT_NONE", "Off"),
    },
    {
      data: VKBASALT_SHARPENING_CAS,
      label: t("CONFIG_VKBASALT_SHARPENING_CAS", "CAS"),
    },
    {
      data: VKBASALT_SHARPENING_DLS,
      label: t("CONFIG_VKBASALT_SHARPENING_DLS", "DLS"),
    },
  ];
  const antialiasingOptions = [
    {
      data: VKBASALT_ANTIALIASING_NONE,
      label: t("CONFIG_VKBASALT_EFFECT_NONE", "Off"),
    },
    {
      data: VKBASALT_ANTIALIASING_FXAA,
      label: t("CONFIG_VKBASALT_ANTIALIASING_FXAA", "FXAA"),
    },
    {
      data: VKBASALT_ANTIALIASING_SMAA,
      label: t("CONFIG_VKBASALT_ANTIALIASING_SMAA", "SMAA"),
    },
  ];
  const shaderOptions = [
    {
      data: VKBASALT_SHADER_NONE,
      label: t("CONFIG_VKBASALT_EFFECT_NONE", "Off"),
    },
    {
      data: VKBASALT_SHADER_HDR_LOOK,
      label: t("CONFIG_VKBASALT_SHADER_HDR_LOOK", "HDR Look (SDR)"),
    },
    {
      data: VKBASALT_SHADER_CLARITY,
      label: t("CONFIG_VKBASALT_SHADER_CLARITY", "Clarity"),
    },
    {
      data: VKBASALT_SHADER_LEVELS_PLUS,
      label: t("CONFIG_VKBASALT_SHADER_LEVELS_PLUS", "Levels Plus"),
    },
    {
      data: VKBASALT_SHADER_VIBRANCE,
      label: t("CONFIG_VKBASALT_SHADER_VIBRANCE", "Vibrance"),
    },
    {
      data: VKBASALT_SHADER_COLOURFULNESS,
      label: t("CONFIG_VKBASALT_SHADER_COLOURFULNESS", "Colourfulness"),
    },
    {
      data: VKBASALT_SHADER_CURVES,
      label: t("CONFIG_VKBASALT_SHADER_CURVES", "Curves"),
    },
    {
      data: VKBASALT_SHADER_DEBAND,
      label: t("CONFIG_VKBASALT_SHADER_DEBAND", "Deband"),
    },
    {
      data: VKBASALT_SHADER_TECHNICOLOR2,
      label: t("CONFIG_VKBASALT_SHADER_TECHNICOLOR2", "Technicolor 2"),
    },
    {
      data: VKBASALT_SHADER_DPX,
      label: t("CONFIG_VKBASALT_SHADER_DPX", "DPX / Cineon"),
    },
    {
      data: VKBASALT_SHADER_BLEACH_BYPASS,
      label: t("CONFIG_VKBASALT_SHADER_BLEACH_BYPASS", "Bleach Bypass"),
    },
    {
      data: VKBASALT_SHADER_NOIR,
      label: t("CONFIG_VKBASALT_SHADER_NOIR", "Noir"),
    },
    {
      data: VKBASALT_SHADER_TECHNICOLOR,
      label: t("CONFIG_VKBASALT_SHADER_TECHNICOLOR", "Technicolor"),
    },
    {
      data: VKBASALT_SHADER_MONOCHROME,
      label: t("CONFIG_VKBASALT_SHADER_MONOCHROME", "Monochrome"),
    },
    {
      data: VKBASALT_SHADER_SEPIA,
      label: t("CONFIG_VKBASALT_SHADER_SEPIA", "Sepia"),
    },
    {
      data: VKBASALT_SHADER_FILM_GRAIN,
      label: t("CONFIG_VKBASALT_SHADER_FILM_GRAIN", "Film Grain"),
    },
    {
      data: VKBASALT_SHADER_VIGNETTE,
      label: t("CONFIG_VKBASALT_SHADER_VIGNETTE", "Vignette"),
    },
    {
      data: VKBASALT_SHADER_CARTOON,
      label: t("CONFIG_VKBASALT_SHADER_CARTOON", "Cartoon"),
    },
    {
      data: VKBASALT_SHADER_NOSTALGIA,
      label: t("CONFIG_VKBASALT_SHADER_NOSTALGIA", "Nostalgia"),
    },
    {
      data: VKBASALT_SHADER_CHROMATIC_ABERRATION,
      label: t(
        "CONFIG_VKBASALT_SHADER_CHROMATIC_ABERRATION",
        "Chromatic Aberration",
      ),
    },
  ];

  return (
    <>
      <PanelSectionRow>
        <ToggleField
          label={
            <MakoExperimentalSettingLabel
              label={t("CONFIG_ENABLE_VKBASALT", "Enable Shaders (Restart)")}
              badgeLabel={t("EXPERIMENTAL_LABEL", "Experimental")}
            />
          }
          description={t(
            "CONFIG_ENABLE_VKBASALT_DESC",
            "Enable before starting the game. Applies MAKO's bundled sharpening, anti-aliasing, and shader effects. No separate installation is needed. If effects are not visible, switch between Windowed and Fullscreen.",
          )}
          bottomSeparator={vkBasaltEnabled ? undefined : "none"}
          checked={vkBasaltEnabled}
          onChange={(value) =>
            onConfigChange(
              EXTERNAL_VULKAN_LAYER,
              value
                ? EXTERNAL_VULKAN_LAYER_VKBASALT
                : EXTERNAL_VULKAN_LAYER_NONE,
            )
          }
        />
      </PanelSectionRow>

      {vkBasaltEnabled && (
        <>
          <PanelSectionRow>
            <Field
              label={t("CONFIG_VKBASALT_SHADER", "Effects")}
              description={t(
                "CONFIG_VKBASALT_SHADER_DESC",
                "Check any effects to combine them. They run in selection order; uncheck and recheck to move one to the end. HDR Look is an SDR visual effect, not HDR output.",
              )}
              childrenLayout="below"
              childrenContainerWidth="max"
            >
              <EffectsChecklist
                key={profileName}
                options={shaderOptions}
                initialSelection={selectedEffects}
                onChange={(value) => onConfigChange(VKBASALT_SHADER, value)}
              />
            </Field>
          </PanelSectionRow>

          <PanelSectionRow>
            <Field
              label={t("CONFIG_VKBASALT_SHARPENING", "Sharpening")}
              description={t(
                "CONFIG_VKBASALT_SHARPENING_DESC",
                "CAS is a crisp general-purpose sharpener. DLS can preserve noisy or grainy detail better when paired with denoise.",
              )}
              childrenLayout="below"
              childrenContainerWidth="max"
            >
              <Dropdown
                rgOptions={sharpeningOptions}
                selectedOption={config.vkbasalt_sharpening}
                onChange={(option) =>
                  onConfigChange(VKBASALT_SHARPENING, String(option.data))
                }
              />
            </Field>
          </PanelSectionRow>

          {sharpeningEnabled && (
            <PanelSectionRow>
              <SliderField
                label={t("CONFIG_VKBASALT_SHARPNESS", "Sharpness ({value}%)", {
                  value: Math.round(config.vkbasalt_sharpness * 100),
                })}
                description={t(
                  "CONFIG_VKBASALT_SHARPNESS_DESC",
                  "Higher values produce a stronger effect but can exaggerate grain and create halos around high-contrast edges.",
                )}
                value={config.vkbasalt_sharpness}
                min={VKBASALT_STRENGTH_MIN}
                max={VKBASALT_STRENGTH_MAX}
                step={0.01}
                onChange={(value) => onConfigChange(VKBASALT_SHARPNESS, value)}
              />
            </PanelSectionRow>
          )}

          {config.vkbasalt_sharpening === VKBASALT_SHARPENING_DLS && (
            <PanelSectionRow>
              <SliderField
                label={t(
                  "CONFIG_VKBASALT_DLS_DENOISE",
                  "DLS Denoise ({value}%)",
                  { value: Math.round(config.vkbasalt_dls_denoise * 100) },
                )}
                description={t(
                  "CONFIG_VKBASALT_DLS_DENOISE_DESC",
                  "Limits how strongly DLS sharpens film grain and fine noise.",
                )}
                value={config.vkbasalt_dls_denoise}
                min={VKBASALT_STRENGTH_MIN}
                max={VKBASALT_STRENGTH_MAX}
                step={0.01}
                onChange={(value) =>
                  onConfigChange(VKBASALT_DLS_DENOISE, value)
                }
              />
            </PanelSectionRow>
          )}

          <PanelSectionRow>
            <Field
              label={t("CONFIG_VKBASALT_ANTIALIASING", "Anti-aliasing")}
              description={t(
                "CONFIG_VKBASALT_ANTIALIASING_DESC",
                "Optionally smooth jagged edges before sharpening. FXAA is lighter and softer; SMAA is more selective and may cost more GPU time.",
              )}
              childrenLayout="below"
              childrenContainerWidth="max"
            >
              <Dropdown
                rgOptions={antialiasingOptions}
                selectedOption={config.vkbasalt_antialiasing}
                onChange={(option) =>
                  onConfigChange(VKBASALT_ANTIALIASING, String(option.data))
                }
              />
            </Field>
          </PanelSectionRow>

          <PanelSectionRow>
            <MakoInlineTip tone="info">
              {isDefaultProfile
                ? t(
                    "CONFIG_VKBASALT_ADVANCED_GLOBAL_NOTE",
                    "Advanced options can be edited in {path}. MAKO merges only the controls above and preserves every other setting. Manual advanced changes apply on the next launch. The Default profile uses this global file.",
                    { path: displayedConfigPath },
                  )
                : t(
                    "CONFIG_VKBASALT_ADVANCED_PROFILE_NOTE",
                    "Advanced options can be edited in {path}. MAKO merges only the controls above and preserves every other setting. Manual advanced changes apply on the next launch. This file belongs to the selected profile and is removed when that profile is deleted.",
                    { path: displayedConfigPath },
                  )}
            </MakoInlineTip>
          </PanelSectionRow>
        </>
      )}
    </>
  );
}
