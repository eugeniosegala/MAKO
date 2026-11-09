import { useEffect, useRef, useState } from "react";
import { DialogButton, Dropdown, ModalRoot } from "@decky/ui";
import {
  getShaderParameters,
  setShaderParameters,
  type ShaderEffectParameters,
} from "../../../api/makoApi";
import t from "../../../i18n/i18n";
import { MakoFocusable } from "../../MakoUi";
import { ShaderParameterControls } from "./ShaderParameterControls";

const CONTROL_INSET = 12;
const MIN_SAVING_STATUS_MS = 500;

export function ShaderParametersModal({
  profileName: requestedProfile,
  closeModal,
  onReturn,
}: {
  profileName: string;
  closeModal?: () => void;
  onReturn?: () => void;
}) {
  // An open editor belongs to the profile that opened it, even if selection changes elsewhere.
  const [profileName] = useState(requestedProfile);
  const [effects, setEffects] = useState<ShaderEffectParameters[]>([]);
  const [selected, setSelected] = useState("");
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState("");
  const [preview, setPreview] = useState(false);
  const alive = useRef(true);
  const pending = useRef<Record<string, number | null>>({});
  const timer = useRef<ReturnType<typeof setTimeout>>();
  const savingTimer = useRef<ReturnType<typeof setTimeout>>();
  const savingStarted = useRef<number | null>(null);
  const flight = useRef<Promise<void> | null>(null);
  const closing = useRef(false);
  const closed = useRef(false);
  const editorFields = useRef<HTMLDivElement>(null);
  const feedback = useRef<HTMLDivElement>(null);

  const startSaving = () => {
    if (!alive.current) return;
    clearTimeout(savingTimer.current);
    savingStarted.current ??= performance.now();
    setSaving(true);
  };
  const finishSaving = () => {
    if (!alive.current || savingStarted.current === null) return;
    clearTimeout(savingTimer.current);
    const remaining =
      MIN_SAVING_STATUS_MS - (performance.now() - savingStarted.current);
    const finish = () => {
      savingStarted.current = null;
      if (alive.current) setSaving(false);
    };
    // Hold only the label; persistence and closing never wait for this timer.
    if (remaining > 0) savingTimer.current = setTimeout(finish, remaining);
    else finish();
  };

  const flush = (): Promise<void> => {
    clearTimeout(timer.current);
    if (flight.current) return flight.current.then(flush);
    if (!Object.keys(pending.current).length) {
      finishSaving();
      return Promise.resolve();
    }
    const changes = pending.current;
    pending.current = {};
    startSaving();
    const work = (async () => {
      try {
        const result = await setShaderParameters(profileName, changes);
        if (!result.success) throw new Error(result.error || result.message);
        if (alive.current) setError("");
      } catch (e) {
        // Keep unsaved values for an explicit retry; never claim they were applied.
        pending.current = { ...changes, ...pending.current };
        clearTimeout(savingTimer.current);
        savingStarted.current = null;
        if (alive.current) {
          setError(String(e));
          setSaving(false);
        }
        throw e;
      } finally {
        flight.current = null;
      }
    })();
    flight.current = work;
    // Edits can arrive while this request is being saved, including during close.
    return work.then(flush);
  };

  useEffect(() => {
    alive.current = true;
    getShaderParameters(profileName)
      .then((result) => {
        if (!alive.current) return;
        if (!result.success) throw new Error(result.error || result.message);
        setEffects(result.effects);
        setSelected(result.effects[0]?.id || "");
      })
      .catch((e) => {
        if (alive.current) setError(String(e));
      })
      .finally(() => {
        if (alive.current) setLoading(false);
      });
    return () => {
      alive.current = false;
      clearTimeout(timer.current);
      clearTimeout(savingTimer.current);
      void flush().catch((e) =>
        console.error("MAKO Decky: shader parameter save failed", e),
      );
    };
  }, [profileName]);

  const change = (values: Record<string, number | null>) => {
    pending.current = { ...pending.current, ...values };
    // Include the debounce interval: the displayed value is not saved yet.
    startSaving();
    setEffects((current) =>
      current.map((effect) => ({
        ...effect,
        parameters: effect.parameters.map((p) =>
          Object.prototype.hasOwnProperty.call(values, p.key)
            ? { ...p, value: values[p.key] ?? p.default }
            : p,
        ),
      })),
    );
    clearTimeout(timer.current);
    timer.current = setTimeout(() => {
      void flush().catch(() => {});
    }, 350);
  };
  const commitFocusedValue = () => {
    const active = editorFields.current?.ownerDocument.activeElement;
    if (active && editorFields.current?.contains(active) && "blur" in active)
      (active as HTMLElement).blur();
  };
  const finishClose = () => {
    if (closed.current) return;
    closed.current = true;
    closeModal?.();
    onReturn?.();
  };
  const close = (event?: {
    preventDefault(): void;
    stopPropagation(): void;
  }) => {
    // Consume B before reopening the side menu, so this press cannot close it.
    event?.preventDefault();
    event?.stopPropagation();
    if (closing.current) return;
    closing.current = true;
    commitFocusedValue();
    void flush()
      .then(finishClose)
      .catch(() => {
        closing.current = false;
      });
  };
  const revealControl = (target: EventTarget | null) => {
    // Steam's controller focus does not always move the browser's activeElement.
    // Use the event target, including when the modal lives in another window.
    const element = target as HTMLElement | null;
    if (element?.nodeType !== 1) return;
    const scroller = [editorFields.current, feedback.current].find((region) =>
      region?.contains(element),
    );
    if (!scroller) return;
    const bounds = scroller.getBoundingClientRect();
    const focused = element.getBoundingClientRect();
    // Steam can scale its UI. Convert viewport coordinates to scroll CSS pixels.
    const scale = scroller.offsetHeight
      ? bounds.height / scroller.offsetHeight
      : 1;
    if (!scale || !scroller.clientHeight) return;
    const top = (focused.top - bounds.top) / scale - scroller.clientTop;
    const bottom = (focused.bottom - bounds.top) / scale - scroller.clientTop;
    const inset = scroller === editorFields.current ? CONTROL_INSET : 4;
    let next = scroller.scrollTop;
    if (top < inset || bottom - top > scroller.clientHeight - inset * 2)
      next += top - inset;
    else if (bottom > scroller.clientHeight - inset)
      next += bottom - scroller.clientHeight + inset;
    // Use scrollTo even when already visible: Steam wraps it to cancel any
    // outstanding animation and clear its cached destination. scrollTop or
    // native scrollIntoView alone leave that animation running behind us.
    scroller.scrollTo({
      top: Math.max(
        0,
        Math.min(scroller.scrollHeight - scroller.clientHeight, next),
      ),
      behavior: "instant",
    });
  };
  const effect = effects.find((item) => item.id === selected);
  const savedStatus = t(
    "SHADER_EDITOR_LIVE",
    "Saved to this profile and applied live.",
  );
  let status = savedStatus;
  if (loading) status = t("SHADER_EDITOR_LOADING", "Reading shader options...");
  else if (saving) status = t("SHADER_EDITOR_SAVING", "Saving...");
  else if (error)
    status = Object.keys(pending.current).length
      ? t("SHADER_EDITOR_UNSAVED", "Changes not saved.")
      : "";
  return (
    <ModalRoot
      className={`mako-shader-editor${preview ? " mako-shader-editor-preview" : ""}`}
      modalClassName="mako-shader-editor-overlay"
      closeModal={close}
      onCancel={close}
      onEscKeypress={close}
      bDisableBackgroundDismiss
      bHideCloseIcon
    >
      <style>{`
      .FullModalOverlay:has(.mako-shader-editor) [class*="ModalOverlayBackground"] { background: transparent !important; backdrop-filter: none !important; }
      /* Keep Steam's top/bottom safe area; viewport-fixed children are clipped by it. */
      .mako-shader-editor-overlay { background: transparent !important; backdrop-filter: none !important; display: flex !important; flex-direction: row !important; justify-content: flex-start !important; align-items: stretch !important; padding: 12px !important; box-sizing: border-box !important; overflow: clip !important; }
      .mako-shader-editor { position: relative !important; inset: auto !important; transform: none !important; margin: 0 !important; width: min(360px, 100%) !important; max-width: 100% !important; min-width: 0 !important; height: 100% !important; max-height: 100% !important; min-height: 0 !important; padding: 16px !important; background: rgba(15, 23, 32, .97) !important; box-sizing: border-box !important; display: flex !important; flex-direction: column !important; overflow: clip !important; }
      /* ModalRoot inserts this wrapper between the panel and our content. */
      /* clip keeps fixed chrome from becoming a focus-scroll container. */
      .mako-shader-editor > .DialogContent_InnerWidth, .mako-shader-editor-content { display: flex; flex-direction: column; flex: 1; min-height: 0; min-width: 0; width: 100%; overflow: clip !important; }
      .mako-shader-editor-preview { height: auto !important; align-self: flex-start; }
      .mako-shader-editor-preview > .DialogContent_InnerWidth, .mako-shader-editor-preview .mako-shader-editor-content { flex: 0 1 auto; }
      .mako-shader-editor-header { flex-shrink: 0; min-width: 0; overflow-wrap: anywhere; }
      .mako-shader-editor-status { display: grid; min-height: 32px; margin-bottom: 6px; font-size: 12px; line-height: 16px; color: #aebac6; }
      /* Reserve the translated saved message's height while a shorter status is shown. */
      .mako-shader-editor-status::before { content: attr(data-saved-label); visibility: hidden; grid-area: 1 / 1; }
      .mako-shader-editor-status > span { grid-area: 1 / 1; }
      /* Steam buttons default to content-box sizing and a 160px minimum. */
      .mako-shader-editor button.DialogButton { box-sizing: border-box !important; min-width: 0 !important; max-width: 100%; white-space: normal; overflow-wrap: anywhere; }
      .mako-shader-editor-actions { display: flex; gap: 8px; min-width: 0; padding: 4px; }
      .mako-shader-editor-actions > button.DialogButton { flex: 1 1 0; width: 0; margin: 0; padding: 10px 8px; }
      .mako-shader-editor-controls { overflow-y: auto; overflow-x: hidden; flex: 1; min-height: 0; margin-top: 12px; padding: ${CONTROL_INSET}px; border: 1px solid rgba(153, 180, 207, .4); border-radius: 6px; background: rgba(9, 15, 22, .25); box-sizing: border-box; scrollbar-gutter: stable both-edges; scroll-padding-block: ${CONTROL_INSET}px; overscroll-behavior: contain; }
      .mako-shader-editor-feedback { flex-shrink: 0; max-height: 30vh; overflow-y: auto; overscroll-behavior: contain; }
    `}</style>
      <MakoFocusable
        className="mako-shader-editor-content"
        flow-children="column"
        scrollIntoViewWhenChildFocused="force"
        fnScrollIntoViewHandler={(focused) => {
          // Replace Steam's descendant/Field smooth scrolling with one owner.
          revealControl(focused.Element ?? null);
          return true;
        }}
        onCancel={close}
        onGamepadFocus={(event) => revealControl(event.target)}
        onFocusCapture={(event) => revealControl(event.target)}
      >
        <div className="mako-shader-editor-header">
          <div style={{ fontSize: 18, marginBottom: 6 }}>
            {t("SHADER_EDITOR_TITLE", "Customize shaders")}
          </div>
          <div style={{ fontSize: 12, color: "#aebac6", marginBottom: 6 }}>
            {profileName}
          </div>
          <div
            className="mako-shader-editor-status"
            data-saved-label={savedStatus}
            role="status"
            aria-live="polite"
            aria-atomic="true"
          >
            <span>{status}</span>
          </div>
          <MakoFocusable
            className="mako-shader-editor-actions"
            flow-children="row"
          >
            <DialogButton
              onClick={() => {
                commitFocusedValue();
                setPreview(!preview);
              }}
            >
              {preview
                ? t("SHADER_EDITOR_SHOW", "Show controls")
                : t("SHADER_EDITOR_HIDE", "Hide controls")}
            </DialogButton>
            <DialogButton onClick={close}>
              {t("SHADER_EDITOR_CLOSE", "Close")}
            </DialogButton>
          </MakoFocusable>
        </div>
        {!preview && (
          <div ref={editorFields} className="mako-shader-editor-controls">
            {!!effects.length && (
              <Dropdown
                selectedOption={selected}
                rgOptions={effects.map((item) => ({
                  data: item.id,
                  label: item.name,
                }))}
                onChange={(o) => {
                  commitFocusedValue();
                  setSelected(String(o.data));
                }}
              />
            )}
            {effect?.error && <div role="alert">{effect.error}</div>}
            {!loading &&
              !error &&
              !effect?.parameters.length &&
              !effect?.error && (
                <div>
                  {t(
                    "SHADER_EDITOR_EMPTY",
                    "No editable parameters in the selected shaders.",
                  )}
                </div>
              )}
            {effect && (
              <ShaderParameterControls
                key={effect.id}
                parameters={effect.parameters}
                onChange={change}
                onBeforeSelectColor={commitFocusedValue}
              />
            )}
            {!!effect?.parameters.some((p) => p.resettable !== false) && (
              <DialogButton
                style={{ marginTop: 16 }}
                onClick={() => {
                  commitFocusedValue();
                  change(
                    Object.fromEntries(
                      effect.parameters
                        .filter((p) => p.resettable !== false)
                        .map((p) => [p.key, null]),
                    ),
                  );
                }}
              >
                {t("SHADER_EDITOR_RESET", "Reset shader to defaults")}
              </DialogButton>
            )}
          </div>
        )}
        {error && (
          <div
            role="alert"
            ref={feedback}
            className="mako-shader-editor-feedback"
            style={{ color: "#ffbdad", marginTop: 8, overflowWrap: "anywhere" }}
          >
            {error}
            {!!Object.keys(pending.current).length && (
              <DialogButton
                onClick={() => {
                  void flush().catch(() => {});
                }}
              >
                {t("SHADER_EDITOR_RETRY", "Retry save")}
              </DialogButton>
            )}
            {!!Object.keys(pending.current).length && (
              <DialogButton
                disabled={saving}
                onClick={() => {
                  if (saving || closed.current) return;
                  closing.current = true;
                  clearTimeout(timer.current);
                  pending.current = {};
                  finishClose();
                }}
              >
                {t("SHADER_EDITOR_DISCARD", "Discard unsaved changes")}
              </DialogButton>
            )}
          </div>
        )}
      </MakoFocusable>
    </ModalRoot>
  );
}
