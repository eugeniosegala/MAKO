import { useEffect, useLayoutEffect, useRef, useState } from "react";
import { GamepadButton, type GamepadEvent } from "@decky/ui";
import { RiArrowDownSFill, RiArrowUpSFill } from "react-icons/ri";
import { VKBASALT_SHADER_NONE } from "../../../config/configSchema";
import t from "../../../i18n/i18n";
import { findFocusScrollContainer } from "../../../utils/focusScrollUtils";
import { MakoFocusable } from "../../MakoUi";

interface EffectOption {
  data: string;
  label: string;
}

const EFFECTS_PER_PAGE = 5;
const EFFECT_ROW_HEIGHT = 38;
const EFFECT_ROW_GAP = 2;

type EffectsAction = "summary" | "pager" | "clear";

export function EffectsChecklist({
  options,
  initialSelection,
  onChange,
  disabled = false,
}: {
  options: EffectOption[];
  initialSelection: string[];
  onChange: (value: string) => Promise<void>;
  disabled?: boolean;
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
    setPage((current) => Math.min(current, pageCount - 1));
  }, [pageCount]);

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
    if (disabled) return;
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
                        aria-disabled={disabled}
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
                aria-disabled={disabled || selected.length === 0}
                onClick={() => selected.length > 0 && updateSelection([])}
                onActivate={() => selected.length > 0 && updateSelection([])}
                {...actionFocusProps("clear")}
                style={{
                  ...actionStyle("clear", disabled || selected.length === 0),
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
