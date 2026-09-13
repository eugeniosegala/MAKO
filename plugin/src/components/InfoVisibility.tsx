import {
  DialogButton,
  GamepadButton,
  gamepadDialogClasses,
  type GamepadEvent,
} from "@decky/ui";
import {
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

/** Keep help visibility local to the panel, independent of game profiles. */
export function InfoVisibility({
  children,
  onFocusCapture,
}: {
  children: ReactNode;
  onFocusCapture?: FocusEventHandler<HTMLDivElement>;
}) {
  const [hidden, setHidden] = usePersistentCollapseState(
    "mako-info-hidden",
    false,
    "MAKO Decky information",
  );
  const [focused, setFocused] = useState(false);
  const ribbon = useRef<HTMLDivElement>(null);
  const label = hidden
    ? t("CONTENT_SHOW_INFO", "Show info")
    : t("CONTENT_HIDE_INFO", "Hide info");

  const toggle = () => {
    // A welcome/model notice may contain the focused button. Move focus to
    // the ribbon before hiding it, so controller navigation has a live target.
    const activeElement = ribbon.current?.ownerDocument.activeElement;
    if (!hidden && activeElement?.closest(infoSelector)) {
      ribbon.current?.querySelector("button")?.focus({ preventScroll: true });
    }
    setHidden((current) => !current);
  };

  const onButtonDown = (event: GamepadEvent) => {
    if (event.detail.button !== GamepadButton.BUMPER_RIGHT) return;
    event.preventDefault();
    event.stopPropagation();
    if (!event.detail.is_repeat) toggle();
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
          onClick={toggle}
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
