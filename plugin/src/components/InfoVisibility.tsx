import {
  DialogButton,
  GamepadButton,
  gamepadDialogClasses,
  type GamepadEvent,
} from "@decky/ui";
import {
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
  type FocusEventHandler,
  type ReactNode,
} from "react";
import { usePersistentCollapseState } from "../hooks/usePersistentCollapseState";
import t from "../i18n/i18n";
import { MakoFocusable, makoDialogButtonStyle } from "./MakoUi";
import { InfoHiddenContext } from "./MakoInfo";

// Use Decky's resolved class, never a hard-coded Steam CSS module name.
const infoSelector = `[data-mako-info="true"], .${gamepadDialogClasses.FieldDescription}`;
const ribbonSelector = '[data-mako-info-toggle="true"]';

function adjacentControl(
  panel: Element,
  source: HTMLElement,
): HTMLElement | null {
  const view = source.ownerDocument.defaultView;
  const controls = Array.from(
    panel.querySelectorAll<HTMLElement>(
      "button, input, select, textarea, a[href], [tabindex]",
    ),
  ).filter((control) => {
    if (
      control.tabIndex < 0 ||
      control.contains(source) ||
      control.matches(":disabled") ||
      control.closest(
        `${infoSelector}, ${ribbonSelector}, [hidden], [inert], [aria-disabled="true"], .disabled`,
      )
    )
      return false;

    for (
      let element: HTMLElement | null = control;
      element;
      element = element.parentElement
    ) {
      const style = view?.getComputedStyle(element);
      if (
        style?.display === "none" ||
        style?.visibility === "hidden" ||
        style?.visibility === "collapse"
      )
        return false;
      if (element === panel) break;
    }
    return true;
  });

  // DOM order follows the panel's column navigation. Prefer continuing down
  // the settings; at the end, stay near the previous surviving control.
  return (
    controls.find(
      (control) =>
        source.compareDocumentPosition(control) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ) ??
    controls[controls.length - 1] ??
    null
  );
}

function scrollContainer(element: HTMLElement): HTMLElement | null {
  const view = element.ownerDocument.defaultView;
  for (
    let parent = element.parentElement;
    parent;
    parent = parent.parentElement
  ) {
    if (
      parent.scrollHeight > parent.clientHeight &&
      /auto|scroll|overlay/.test(view?.getComputedStyle(parent).overflowY ?? "")
    ) {
      return parent;
    }
  }
  return element.ownerDocument.scrollingElement as HTMLElement | null;
}

/** Keep help visibility local to the panel, independent of game profiles. */
export function InfoVisibility({ children }: { children: ReactNode }) {
  const [hidden, setHidden] = usePersistentCollapseState(
    "mako-info-hidden",
    false,
    "MAKO Decky information",
  );
  const [focused, setFocused] = useState(false);
  const ribbon = useRef<HTMLDivElement>(null);
  const scrollFrame = useRef<number>();
  const focusAnchor = useRef<{
    target: HTMLElement;
    top: number;
    scroller: HTMLElement | null;
  }>();
  const label = hidden
    ? t("CONTENT_SHOW_INFO", "Show info")
    : t("CONTENT_HIDE_INFO", "Hide info");

  const cancelScroll = () => {
    if (scrollFrame.current !== undefined) {
      cancelAnimationFrame(scrollFrame.current);
      scrollFrame.current = undefined;
    }
  };

  useEffect(() => cancelScroll, []);

  useLayoutEffect(() => {
    const anchor = focusAnchor.current;
    if (!anchor) return;
    const target = anchor.target.isConnected
      ? anchor.target
      : ribbon.current?.querySelector("button");
    // Refocusing a surviving control alone emits no new focus event. Restore
    // its screen position explicitly, after descriptions have changed height.
    target?.focus({ preventScroll: true });
    if (target === anchor.target && anchor.scroller) {
      anchor.scroller.scrollTop +=
        target.getBoundingClientRect().top - anchor.top;
    }
    focusAnchor.current = undefined;
  }, [hidden]);

  const onFocusCapture: FocusEventHandler<HTMLDivElement> = (event) => {
    cancelScroll();
    const target = event.target;
    if (
      focusAnchor.current ||
      !event.currentTarget.contains(target) ||
      target.closest(ribbonSelector)
    )
      return;

    // Keep normal navigation centred, but never let an older request scroll
    // away from a newer control or from the position restored by an R1 toggle.
    scrollFrame.current = requestAnimationFrame(() => {
      scrollFrame.current = undefined;
      if (target.isConnected && target.ownerDocument.activeElement === target) {
        target.scrollIntoView({
          block: "center",
          inline: "nearest",
          behavior: "auto",
        });
      }
    });
  };

  const toggle = (source?: HTMLElement) => {
    cancelScroll();
    const panel = ribbon.current?.closest(".Mako_InfoVisibility");
    const activeElement = ribbon.current?.ownerDocument
      .activeElement as HTMLElement | null;
    let target = source ?? activeElement;
    const sourceTop = target?.getBoundingClientRect().top;
    if (
      target &&
      panel?.contains(target) &&
      !hidden &&
      target.closest(infoSelector)
    ) {
      target = adjacentControl(panel, target);
    }
    if (!target || !panel?.contains(target)) {
      target = ribbon.current?.querySelector("button") ?? null;
    }
    if (target) {
      focusAnchor.current = {
        target,
        top: sourceTop ?? target.getBoundingClientRect().top,
        scroller: target.closest(ribbonSelector)
          ? null
          : scrollContainer(target),
      };
      target.focus({ preventScroll: true });
    }
    setHidden((current) => !current);
  };

  const onButtonDown = (event: GamepadEvent) => {
    if (event.detail.button !== GamepadButton.BUMPER_RIGHT) return;
    event.preventDefault();
    event.stopPropagation();
    if (!event.detail.is_repeat) toggle(event.target as HTMLElement);
  };

  return (
    <MakoFocusable
      className={
        hidden ? "Mako_InfoVisibility Mako_InfoHidden" : "Mako_InfoVisibility"
      }
      flow-children="column"
      onButtonDown={onButtonDown}
      onFocusCapture={onFocusCapture}
    >
      <style>{`
        .Mako_InfoVisibility .${gamepadDialogClasses.FieldDescription} {
          font-size: 10px !important;
          line-height: 14px !important;
        }
        .Mako_InfoVisibility.DesktopUI .${gamepadDialogClasses.FieldDescription},
        .DesktopUI .Mako_InfoVisibility .${gamepadDialogClasses.FieldDescription} {
          font-size: 11px !important;
          line-height: 16px !important;
        }
        .Mako_InfoHidden .${gamepadDialogClasses.FieldDescription} {
          display: none !important;
        }
        .Mako_InfoHidden [data-mako-update-notice="true"] {
          background: none !important;
          border: none !important;
          padding: 0 !important;
        }
      `}</style>
      <InfoHiddenContext.Provider value={hidden}>
        {children}
      </InfoHiddenContext.Provider>
      <div
        ref={ribbon}
        data-mako-info-toggle="true"
        style={{
          position: "sticky",
          bottom: "8px",
          zIndex: 5,
          display: "flex",
          justifyContent: "flex-end",
          margin: "8px 12px",
          pointerEvents: "none",
        }}
      >
        <DialogButton
          aria-label={label}
          aria-pressed={hidden}
          onClick={() => toggle()}
          onGamepadFocus={() => setFocused(true)}
          onGamepadBlur={() => setFocused(false)}
          style={{
            ...makoDialogButtonStyle(focused),
            display: "flex",
            alignItems: "center",
            gap: "6px",
            width: "auto",
            minWidth: 0,
            height: "28px",
            padding: "4px 9px",
            borderRadius: "6px",
            fontSize: "11px",
            lineHeight: 1.2,
            pointerEvents: "auto",
          }}
        >
          <span
            aria-hidden="true"
            style={{
              border: "1px solid rgba(200, 230, 240, 0.6)",
              borderRadius: "3px",
              padding: "1px 4px",
              fontSize: "10px",
              fontWeight: 700,
            }}
          >
            R1
          </span>
          {label}
        </DialogButton>
      </div>
    </MakoFocusable>
  );
}
