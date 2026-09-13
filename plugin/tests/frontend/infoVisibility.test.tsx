import React, { useEffect, useRef } from "react";
import { createPortal } from "react-dom";
import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import { afterEach, beforeEach, expect, test, vi } from "vitest";
import { Navigation, type GamepadEvent } from "@decky/ui";

vi.mock("@decky/ui", () => ({
  GamepadButton: { BUMPER_RIGHT: 6, BUMPER_LEFT: 5 },
  gamepadDialogClasses: { FieldDescription: "Steam_FieldDescription" },
  Focusable: ({
    onButtonDown,
    "flow-children": _flow,
    children,
    ...props
  }: React.HTMLAttributes<HTMLDivElement> & {
    onButtonDown?: (event: GamepadEvent) => void;
    "flow-children"?: string;
  }) => {
    const ref = useRef<HTMLDivElement>(null);
    useEffect(() => {
      const element = ref.current!;
      const handle = (event: Event) => onButtonDown?.(event as GamepadEvent);
      element.addEventListener("vgp_onbuttondown", handle);
      return () => element.removeEventListener("vgp_onbuttondown", handle);
    }, [onButtonDown]);
    return (
      <div ref={ref} {...props}>
        {children}
      </div>
    );
  },
  DialogButton: ({
    onGamepadFocus,
    onGamepadBlur,
    ...props
  }: React.ButtonHTMLAttributes<HTMLButtonElement> & {
    onGamepadFocus?: () => void;
    onGamepadBlur?: () => void;
  }) => <button {...props} onFocus={onGamepadFocus} onBlur={onGamepadBlur} />,
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div data-testid="navigation-row">{children}</div>
  ),
  ButtonItem: ({
    children,
    onClick,
  }: React.ButtonHTMLAttributes<HTMLButtonElement>) => (
    <button onClick={onClick}>{children}</button>
  ),
  Navigation: { NavigateToExternalWeb: vi.fn() },
}));
vi.mock("../../src/api/makoApi", () => ({
  DEFAULT_STEAM_LAUNCH_OPTION: "mako-run %command%",
  getLaunchOption: vi.fn(async () => ({ launch_option: "mako-run %command%" })),
}));
vi.mock("../../src/components/SmartClipboardButton", () => ({
  SmartClipboardButton: () => <button>Copy Launch Option</button>,
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { InfoVisibility } from "../../src/components/InfoVisibility";
import { ContentNotices } from "../../src/components/ContentNotices";
import { UsageInstructions } from "../../src/components/UsageInstructions";
import { StatusDisplay } from "../../src/components/StatusDisplay";
import {
  ModelWarning,
  type ModelWarningProps,
} from "../../src/components/ModelWarning";
import {
  MakoInlineTip,
  MakoSectionHeader,
  MakoSettingRelationship,
} from "../../src/components/MakoUi";

const animationFrames = new Map<number, FrameRequestCallback>();
let frameId = 0;

beforeEach(() => {
  window.SP_REACT = React;
  localStorage.clear();
  animationFrames.clear();
  vi.stubGlobal("requestAnimationFrame", (callback: FrameRequestCallback) => {
    animationFrames.set(++frameId, callback);
    return frameId;
  });
  vi.stubGlobal("cancelAnimationFrame", (id: number) =>
    animationFrames.delete(id),
  );
  HTMLElement.prototype.scrollIntoView = vi.fn();
});
afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

function flushAnimationFrames() {
  act(() => {
    const callbacks = [...animationFrames.values()];
    animationFrames.clear();
    callbacks.forEach((callback) => callback(0));
  });
}

function pressButton(target: Element, button = 6, repeat = false) {
  const event = new CustomEvent("vgp_onbuttondown", {
    bubbles: true,
    cancelable: true,
    detail: { button, is_repeat: repeat, source: 0 },
  });
  fireEvent(target, event);
  return event;
}

function isDisplayed(element: Element): boolean {
  return (
    getComputedStyle(element).display !== "none" &&
    (!element.parentElement || isDisplayed(element.parentElement))
  );
}

test("R1 hides information without changing controls, repeats, or other buttons", () => {
  render(
    <InfoVisibility>
      <MakoSectionHeader description="Section tutorial">
        Settings
      </MakoSectionHeader>
      <label>
        Flow Scale
        <input defaultValue="90" />
      </label>
      <div className="Steam_FieldDescription">Setting description</div>
      <MakoInlineTip>Helpful tip</MakoInlineTip>
      <MakoInlineTip tone="warning">Setting warning</MakoInlineTip>
      <MakoSettingRelationship>Setting relationship</MakoSettingRelationship>
    </InfoVisibility>,
  );
  const input = screen.getByRole("textbox") as HTMLInputElement;
  fireEvent.change(input, { target: { value: "75" } });
  input.focus();
  const descriptions = [
    "Section tutorial",
    "Setting description",
    "Helpful tip",
    "Setting warning",
    "Setting relationship",
  ];
  expect(
    descriptions.every((text) => isDisplayed(screen.getByText(text))),
  ).toBe(true);
  expect(pressButton(input, 5).defaultPrevented).toBe(false);
  expect(pressButton(input).defaultPrevented).toBe(true);
  expect(
    descriptions.every((text) => {
      const element = screen.queryByText(text);
      return !element || !isDisplayed(element);
    }),
  ).toBe(true);
  expect(isDisplayed(screen.getByText("Settings"))).toBe(true);
  expect(isDisplayed(input)).toBe(true);
  expect(input.value).toBe("75");
  expect(document.activeElement).toBe(input);
  pressButton(input, 6, true);
  expect(
    screen
      .getByRole("button", { name: "Show info" })
      .getAttribute("aria-pressed"),
  ).toBe("true");
  pressButton(input);
  expect(
    descriptions.every((text) => isDisplayed(screen.getByText(text))),
  ).toBe(true);
  expect(input.value).toBe("75");
});

test("clicking the ribbon persists the choice across reopening without changing section preferences", () => {
  localStorage.setItem("mako-welcome-tips-collapsed", "true");
  const { unmount } = render(
    <InfoVisibility>
      <button>Option</button>
    </InfoVisibility>,
  );
  fireEvent.click(screen.getByRole("button", { name: "Hide info" }));
  expect(localStorage.getItem("mako-info-hidden")).toBe("true");
  expect(localStorage.getItem("mako-welcome-tips-collapsed")).toBe("true");
  unmount();
  render(
    <InfoVisibility>
      <button>Option</button>
    </InfoVisibility>,
  );
  fireEvent.click(screen.getByRole("button", { name: "Show info" }));
  expect(localStorage.getItem("mako-info-hidden")).toBe("false");
});

test("R1 refocuses the selected control and anchors it after both hide and show reflow", () => {
  render(
    <div data-testid="scroller" style={{ overflowY: "auto" }}>
      <InfoVisibility>
        <MakoInlineTip>Long tutorial above the control</MakoInlineTip>
        <input aria-label="Flow Scale" defaultValue="90" />
      </InfoVisibility>
    </div>,
  );
  const scroller = screen.getByTestId("scroller");
  Object.defineProperties(scroller, {
    scrollHeight: { value: 1400 },
    clientHeight: { value: 400 },
  });
  scroller.scrollTop = 600;
  const control = screen.getByRole("textbox");
  vi.spyOn(control, "getBoundingClientRect").mockImplementation(() => {
    const offset = control.closest(".Mako_InfoHidden") ? 250 : 700;
    return new DOMRect(0, offset - scroller.scrollTop, 100, 32);
  });
  control.focus();
  const focus = vi.spyOn(control, "focus");
  expect(animationFrames.size).toBe(1);
  pressButton(control);
  expect(document.activeElement).toBe(control);
  expect(focus).toHaveBeenLastCalledWith({ preventScroll: true });
  expect(scroller.scrollTop).toBe(150);
  expect(control.getBoundingClientRect().top).toBe(100);
  expect(animationFrames.size).toBe(0);
  pressButton(control);
  expect(scroller.scrollTop).toBe(600);
  expect(control.getBoundingClientRect().top).toBe(100);
  expect(document.activeElement).toBe(control);
  flushAnimationFrames();
  expect(control.scrollIntoView).not.toHaveBeenCalled();
});

test("normal navigation scrolls only the latest focus and cancels work on unmount", () => {
  const { unmount } = render(
    <InfoVisibility>
      <button>First option</button>
      <button>Second option</button>
    </InfoVisibility>,
  );
  screen.getByRole("button", { name: "First option" }).focus();
  const second = screen.getByRole("button", { name: "Second option" });
  second.focus();
  flushAnimationFrames();
  expect(second.scrollIntoView).toHaveBeenCalledExactlyOnceWith({
    block: "center",
    inline: "nearest",
    behavior: "auto",
  });
  expect(vi.mocked(second.scrollIntoView).mock.contexts).toEqual([second]);
  screen.getByRole("button", { name: "First option" }).focus();
  expect(animationFrames.size).toBe(1);
  unmount();
  expect(animationFrames.size).toBe(0);
});

test("a ribbon toggle keeps focus there without scrolling to the panel bottom", () => {
  render(
    <InfoVisibility>
      <button>Option</button>
    </InfoVisibility>,
  );
  const ribbon = screen.getByRole("button", { name: "Hide info" });
  ribbon.focus();
  fireEvent.click(ribbon);
  flushAnimationFrames();
  expect(document.activeElement).toBe(ribbon);
  expect(ribbon.scrollIntoView).not.toHaveBeenCalled();
});

test("unmounts hidden information and its navigation rows; preserves actions and focus", async () => {
  render(
    <InfoVisibility>
      <ContentNotices
        developmentBuildInfo={{
          generatedAt: "2026-09-13T10:00:00Z",
          plugin: {
            commit: "abc1234",
            dirty: false,
            frontendDeployed: true,
            backendDeployed: true,
          },
          engine: null,
        }}
        showWelcome
        engineUpdateRequired
        isInstalling={false}
        isInstallCompletionVisible={false}
        isUninstalling={false}
        onInstall={vi.fn(async () => undefined)}
        modelStatus={{
          lsfg: { compatible: false, reason: "lsfg-unavailable" },
        }}
      />
      <UsageInstructions />
      <StatusDisplay
        dllDetected
        dllDetectionStatus="DLL found"
        isInstalled
        installationStatus="Installed"
      />
    </InfoVisibility>,
  );
  await waitFor(() =>
    expect(screen.getByText("mako-run %command%")).toBeTruthy(),
  );
  const welcomeButton = screen.getByRole("button", { name: "Hide tips" });
  const welcomeRow = welcomeButton.closest('[data-testid="navigation-row"]')!;
  const developmentButton = screen.getByRole("button", { name: "Details" });
  const warningButton = screen.getByRole("button", {
    name: "Check for MAKO Decky updates",
  });
  fireEvent.click(developmentButton);
  fireEvent.click(welcomeButton);
  welcomeButton.focus();
  pressButton(welcomeButton);
  expect(document.activeElement).toBe(
    screen.getByRole("button", { name: "Show info" }),
  );
  for (const text of [
    "Hello from the MAKO Team!",
    "mako-run %command%",
    "DLL found",
    "Installed",
    "MAKO Renderer update required",
  ]) {
    expect(screen.queryByText(text)).toBeNull();
  }
  // CSS-hidden buttons still exist in Steam's navigation graph. Check actual
  // removal, including the row that can otherwise become an empty focus stop.
  for (const element of [welcomeButton, welcomeRow, developmentButton]) {
    expect(element.isConnected).toBe(false);
  }
  expect(warningButton.isConnected).toBe(true);
  expect(screen.getAllByTestId("navigation-row")).toHaveLength(3);
  expect(
    screen
      .getAllByRole("button", { hidden: true })
      .map((button) => button.textContent),
  ).toEqual([
    "Check for MAKO Decky updates",
    "Update MAKO Renderer",
    "Copy Launch Option",
    "R1Show info",
  ]);
  expect(isDisplayed(screen.getByRole("alert"))).toBe(true);
  expect(
    screen.getByRole("button", { name: "Copy Launch Option" }),
  ).toBeTruthy();
  expect(
    screen.getByRole("button", { name: "Update MAKO Renderer" }),
  ).toBeTruthy();
  fireEvent.click(screen.getByRole("button", { name: "Show info" }));
  expect(screen.getByRole("button", { name: "Show tips" })).toBeTruthy();
  expect(screen.getByRole("button", { name: "Hide" })).toBeTruthy();
  expect(screen.getByRole("alert")).toBeTruthy();
});

test("a new model failure appears while info is hidden, without restoring welcome controls", () => {
  localStorage.setItem("mako-info-hidden", "true");
  const panel = (failed: boolean) => (
    <InfoVisibility>
      <ContentNotices
        developmentBuildInfo={null}
        showWelcome
        engineUpdateRequired={false}
        isInstalling={false}
        isInstallCompletionVisible={false}
        isUninstalling={false}
        onInstall={vi.fn(async () => undefined)}
        modelStatus={{
          lsfg: {
            compatible: !failed,
            reason: failed ? "lsfg-unavailable" : null,
          },
        }}
      />
      <button>Option</button>
    </InfoVisibility>
  );
  const { rerender } = render(panel(false));
  expect(screen.queryByRole("alert")).toBeNull();
  rerender(panel(true));
  expect(screen.getAllByTestId("navigation-row")).toHaveLength(1);
  expect(screen.getAllByRole("button", { hidden: true })).toHaveLength(3);
  expect(isDisplayed(screen.getByRole("alert"))).toBe(true);
  expect(screen.queryByRole("button", { name: "Hide tips" })).toBeNull();
  pressButton(screen.getByRole("button", { name: "Option" }));
  expect(screen.getByRole("button", { name: "Hide tips" })).toBeTruthy();
  expect(
    screen.getByRole("button", { name: "Check for MAKO Decky updates" }),
  ).toBeTruthy();
});

test.each<{ name: string; status: ModelWarningProps; message: string }>([
  {
    name: "LSFG",
    status: { lsfg: { compatible: false, reason: "lsfg-unavailable" } },
    message: "An LSFG model check failed.",
  },
  {
    name: "LS1",
    status: { ls1: { compatible: false, reason: "ls1-unavailable" } },
    message: "LS1 failed its availability check.",
  },
  {
    name: "missing DLL",
    status: { lsfg: { compatible: false, reason: "dll-unavailable" } },
    message: "Lossless.dll could not be found.",
  },
  {
    name: "runtime fallback",
    status: { ls1RuntimeFallback: true },
    message: "LS1 is unavailable for this game.",
  },
])(
  "$name warning stays readable when reopening with info hidden",
  ({ status, message }) => {
    localStorage.setItem("mako-info-hidden", "true");
    render(
      <InfoVisibility>
        <ModelWarning {...status} />
        <MakoInlineTip tone="warning">Optional advice</MakoInlineTip>
      </InfoVisibility>,
    );
    const warning = screen.getByRole("alert");
    expect(warning.textContent).toContain(message);
    expect(isDisplayed(warning.querySelector('[role="note"]')!)).toBe(true);
    expect(screen.queryByText("Optional advice")).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "Show info" }));
    fireEvent.click(screen.getByRole("button", { name: "Hide info" }));
    expect(screen.getByRole("alert")).toBe(warning);
    expect(isDisplayed(warning)).toBe(true);
  },
);

test("the model-warning update action keeps focus across R1 and still opens updates", () => {
  render(
    <InfoVisibility>
      <ModelWarning lsfg={{ compatible: false, reason: "lsfg-unavailable" }} />
    </InfoVisibility>,
  );
  const update = screen.getByRole("button", {
    name: "Check for MAKO Decky updates",
  });
  update.focus();
  for (let toggle = 0; toggle < 2; toggle++) {
    pressButton(update);
    expect(document.activeElement).toBe(update);
    expect(isDisplayed(update)).toBe(true);
    expect(screen.getByRole("alert").textContent).toContain(
      "verify Lossless Scaling and collect diagnostics",
    );
  }
  fireEvent.click(update);
  expect(Navigation.NavigateToExternalWeb).toHaveBeenCalledWith(
    "https://github.com/eugeniosegala/MAKO/releases/latest",
  );
});

test("does not hide dialog content or handle buttons outside the panel", () => {
  const parentHandler = vi.fn();
  document.addEventListener("vgp_onbuttondown", parentHandler);
  const { unmount } = render(
    <InfoVisibility>
      <button>Panel option</button>
      {createPortal(
        <div role="dialog">
          <div data-mako-info="true">Confirmation</div>
          <button>Confirm</button>
        </div>,
        document.body,
      )}
    </InfoVisibility>,
  );
  pressButton(screen.getByText("Panel option"));
  expect(parentHandler).not.toHaveBeenCalled();
  expect(isDisplayed(screen.getByText("Confirmation"))).toBe(true);
  expect(pressButton(screen.getByText("Confirm")).defaultPrevented).toBe(false);
  expect(parentHandler).toHaveBeenCalledTimes(1);
  expect(screen.getByRole("button", { name: "Show info" })).toBeTruthy();
  unmount();
  expect(pressButton(document.body).defaultPrevented).toBe(false);
  document.removeEventListener("vgp_onbuttondown", parentHandler);
});
