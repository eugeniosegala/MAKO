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

const panelNavigation = vi.hoisted(() => ({
  scroll: undefined as
    ((node: { Element?: HTMLElement | null }) => boolean) | undefined,
  owner: undefined as boolean | "force" | undefined,
}));

vi.mock("@decky/ui", () => ({
  GamepadButton: { BUMPER_RIGHT: 6, BUMPER_LEFT: 5 },
  gamepadDialogClasses: { FieldDescription: "Steam_FieldDescription" },
  Focusable: ({
    onButtonDown,
    onGamepadFocus,
    onGamepadBlur,
    fnScrollIntoViewHandler,
    scrollIntoViewWhenChildFocused,
    "flow-children": _flow,
    children,
    ...props
  }: React.HTMLAttributes<HTMLDivElement> & {
    onButtonDown?: (event: GamepadEvent) => void;
    onGamepadFocus?: (event: GamepadEvent) => void;
    onGamepadBlur?: (event: GamepadEvent) => void;
    fnScrollIntoViewHandler?: (node: {
      Element?: HTMLElement | null;
    }) => boolean;
    scrollIntoViewWhenChildFocused?: boolean | "force";
    "flow-children"?: string;
  }) => {
    const ref = useRef<HTMLDivElement>(null);
    if (props.className?.includes("Mako_InfoVisibility")) {
      panelNavigation.scroll = fnScrollIntoViewHandler;
      panelNavigation.owner = scrollIntoViewWhenChildFocused;
    }
    useEffect(() => {
      const element = ref.current!;
      const handle = (event: Event) => onButtonDown?.(event as GamepadEvent);
      const focus = (event: Event) => onGamepadFocus?.(event as GamepadEvent);
      const blur = (event: Event) => onGamepadBlur?.(event as GamepadEvent);
      element.addEventListener("vgp_onbuttondown", handle);
      element.addEventListener("vgp_onfocus", focus);
      element.addEventListener("vgp_onblur", blur);
      return () => {
        element.removeEventListener("vgp_onbuttondown", handle);
        element.removeEventListener("vgp_onfocus", focus);
        element.removeEventListener("vgp_onblur", blur);
      };
    }, [onButtonDown, onGamepadFocus, onGamepadBlur]);
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
import { EffectsChecklist } from "../../src/components/settings/shaders/EffectsChecklist";
import { MakoInfo } from "../../src/components/MakoInfo";
import { ContentNotices } from "../../src/components/ContentNotices";
import { UsageInstructions } from "../../src/components/UsageInstructions";
import { StatusDisplay } from "../../src/components/StatusDisplay";
import { RuntimeStatusCard } from "../../src/components/RuntimeStatusCard";
import { EMPTY_RUNTIME_SCALING_UI_STATE } from "../../src/utils/runtimeScalingUtils";
import {
  ModelWarning,
  type ModelWarningProps,
} from "../../src/components/ModelWarning";
import {
  MakoExperimentalSettingLabel,
  MakoInlineTip,
  MakoReleaseIdentity,
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
  HTMLElement.prototype.scrollTo = vi.fn(function (
    this: HTMLElement,
    options: ScrollToOptions | number,
  ) {
    if (typeof options === "object")
      this.scrollTop = options.top ?? this.scrollTop;
  }) as typeof HTMLElement.prototype.scrollTo;
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
      <MakoReleaseIdentity version="3.2.1" codename="The Captain" />
      <RuntimeStatusCard
        runtimeState={{
          ...EMPTY_RUNTIME_SCALING_UI_STATE,
          hasContext: true,
          frameGenerationActive: true,
          frameGenerationMode: "fixed",
          frameGenerationMultiplier: 2,
        }}
      />
      <MakoSectionHeader description="Section tutorial">
        Settings
      </MakoSectionHeader>
      <label>
        Flow Scale
        <input defaultValue="90" />
      </label>
      <label>
        <MakoExperimentalSettingLabel
          label="Enable HDR (Restart)"
          badgeLabel="Experimental"
        />
        <input type="checkbox" defaultChecked />
      </label>
      <div className="Steam_FieldDescription">Setting description</div>
      <div className="Mako_OptionDescription">MAKO description</div>
      <MakoInlineTip>Helpful tip</MakoInlineTip>
      <MakoInlineTip tone="warning">Setting warning</MakoInlineTip>
      <MakoSettingRelationship>Setting relationship</MakoSettingRelationship>
    </InfoVisibility>,
  );
  const input = screen.getByRole("textbox") as HTMLInputElement;
  const hdrToggle = screen.getByRole("checkbox") as HTMLInputElement;
  fireEvent.change(input, { target: { value: "75" } });
  input.focus();
  const descriptions = [
    "Section tutorial",
    "Setting description",
    "MAKO description",
    "Helpful tip",
    "Setting warning",
    "Setting relationship",
    "Experimental",
  ];
  expect(
    descriptions.every((text) => isDisplayed(screen.getByText(text))),
  ).toBe(true);
  expect(
    screen.getByText("Helpful tip").closest(".Mako_OptionMessage"),
  ).toBeTruthy();
  expect(
    screen.getByText("Setting relationship").closest(".Mako_OptionMessage"),
  ).toBeTruthy();
  expect(pressButton(input, 5).defaultPrevented).toBe(false);
  expect(pressButton(input).defaultPrevented).toBe(true);
  expect(
    descriptions.every((text) => {
      const element = screen.queryByText(text);
      return !element || !isDisplayed(element);
    }),
  ).toBe(true);
  expect(isDisplayed(screen.getByText("Settings"))).toBe(true);
  expect(isDisplayed(screen.getByText("v3.2.1"))).toBe(true);
  expect(isDisplayed(screen.getByText("the-captain"))).toBe(true);
  expect(isDisplayed(screen.getByText("Live Status"))).toBe(true);
  expect(isDisplayed(screen.getByText("MAKO is active"))).toBe(true);
  expect(isDisplayed(screen.getByText("2×"))).toBe(true);
  expect(isDisplayed(input)).toBe(true);
  expect(input.value).toBe("75");
  expect(isDisplayed(screen.getByText("Enable HDR"))).toBe(true);
  expect(isDisplayed(screen.getByText("(Restart)"))).toBe(true);
  expect(isDisplayed(hdrToggle)).toBe(true);
  expect(hdrToggle.checked).toBe(true);
  expect(document.activeElement).toBe(input);
  pressButton(input, 6, true);
  expect(
    screen
      .getByRole("button", { name: "Show info" })
      .getAttribute("aria-pressed"),
  ).toBe("true");
  pressButton(input);
  expect(isDisplayed(screen.getByText("MAKO is active"))).toBe(true);
  expect(isDisplayed(screen.getByText("2×"))).toBe(true);
  expect(
    descriptions.every((text) => isDisplayed(screen.getByText(text))),
  ).toBe(true);
  expect(input.value).toBe("75");
  expect(hdrToggle.checked).toBe(true);
});

test("renders option descriptions two pixels smaller in both Steam UI modes", () => {
  const { container } = render(
    <InfoVisibility>
      <div className="Steam_FieldDescription">Setting description</div>
      <div className="Mako_OptionDescription">MAKO description</div>
      <MakoInlineTip>Option message</MakoInlineTip>
      <MakoInlineTip alwaysVisible>Top warning</MakoInlineTip>
    </InfoVisibility>,
  );

  const styles = [...container.querySelectorAll("style")]
    .map((style) => style.textContent)
    .join("\n");
  expect(styles).toContain(
    ".Mako_InfoVisibility .Steam_FieldDescription,\n        .Mako_InfoVisibility .Mako_OptionDescription,\n        .Mako_InfoVisibility .Mako_OptionMessage {\n          font-size: 10px !important;\n          line-height: 14px !important;",
  );
  expect(styles).toContain(
    ".DesktopUI .Mako_InfoVisibility .Steam_FieldDescription,\n        .DesktopUI .Mako_InfoVisibility .Mako_OptionDescription,\n        .DesktopUI .Mako_InfoVisibility .Mako_OptionMessage {\n          font-size: 11px !important;\n          line-height: 16px !important;",
  );
  expect(
    screen.getByText("Option message").closest(".Mako_OptionMessage"),
  ).toBeTruthy();
  expect(
    screen.getByText("Top warning").closest(".Mako_OptionMessage"),
  ).toBeNull();
});

test("clicking the ribbon persists the choice across reopening without changing section preferences", () => {
  localStorage.setItem("mako-welcome-tips-collapsed", "true");
  const { unmount } = render(
    <InfoVisibility>
      <button>
        <MakoExperimentalSettingLabel
          label="Enable Scaling (Restart)"
          badgeLabel="Experimental"
        />
      </button>
    </InfoVisibility>,
  );
  expect(screen.getByText("Experimental")).toBeTruthy();
  fireEvent.click(screen.getByRole("button", { name: "Hide info" }));
  expect(screen.queryByText("Experimental")).toBeNull();
  expect(localStorage.getItem("mako-info-hidden")).toBe("true");
  expect(localStorage.getItem("mako-welcome-tips-collapsed")).toBe("true");
  unmount();
  render(
    <InfoVisibility>
      <MakoReleaseIdentity version="3.2.1" codename="The Captain" />
      <RuntimeStatusCard runtimeState={EMPTY_RUNTIME_SCALING_UI_STATE} />
      <button>
        <MakoExperimentalSettingLabel
          label="Enable Scaling (Restart)"
          badgeLabel="Experimental"
        />
      </button>
    </InfoVisibility>,
  );
  expect(isDisplayed(screen.getByText("v3.2.1"))).toBe(true);
  expect(isDisplayed(screen.getByText("the-captain"))).toBe(true);
  expect(isDisplayed(screen.getByText("Waiting for MAKO"))).toBe(true);
  expect(isDisplayed(screen.getByText(/Live metrics unavailable/))).toBe(true);
  expect(screen.queryByText("Experimental")).toBeNull();
  fireEvent.click(screen.getByRole("button", { name: "Show info" }));
  expect(isDisplayed(screen.getByText("Experimental"))).toBe(true);
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
    behavior: "instant",
  });
  expect(vi.mocked(second.scrollIntoView).mock.contexts).toEqual([second]);
  screen.getByRole("button", { name: "First option" }).focus();
  expect(animationFrames.size).toBe(1);
  unmount();
  expect(animationFrames.size).toBe(0);
});

test("effects navigation keeps rows visible without recentering the whole panel", () => {
  render(
    <InfoVisibility>
      <EffectsChecklist
        options={[
          { data: "clarity", label: "Clarity" },
          { data: "vibrance", label: "Vibrance" },
        ]}
        initialSelection={[]}
        onChange={vi.fn(async () => undefined)}
      />
      <button>After effects</button>
    </InfoVisibility>,
  );
  fireEvent.click(screen.getByRole("button", { name: /^Choose effects/ }));
  for (const effect of screen.getAllByRole("checkbox")) {
    vi.mocked(effect.scrollIntoView).mockClear();
    effect.focus();
    flushAnimationFrames();
    expect(effect.scrollIntoView).toHaveBeenCalledExactlyOnceWith({
      block: "nearest",
      inline: "nearest",
      behavior: "instant",
    });
    expect(vi.mocked(effect.scrollIntoView).mock.contexts).toEqual([effect]);
  }
  const next = screen.getByRole("button", { name: "After effects" });
  vi.mocked(next.scrollIntoView).mockClear();
  next.focus();
  flushAnimationFrames();
  expect(next.scrollIntoView).toHaveBeenCalledExactlyOnceWith({
    block: "center",
    inline: "nearest",
    behavior: "instant",
  });
});

test.each([
  { scale: 1, collapseFirst: false },
  { scale: 1, collapseFirst: true },
  { scale: 1.5, collapseFirst: false },
  { scale: 1.5, collapseFirst: true },
])(
  "effects collapse keeps Customize visible (scale $scale, collapse first $collapseFirst)",
  ({ scale, collapseFirst }) => {
    vi.useFakeTimers({ toFake: ["setTimeout", "clearTimeout"] });
    try {
      render(
        <div data-testid="scroller" style={{ overflowY: "auto" }}>
          <InfoVisibility>
            <button>Shaders tab</button>
            <EffectsChecklist
              options={[{ data: "clarity", label: "Clarity" }]}
              initialSelection={["clarity"]}
              onChange={vi.fn(async () => undefined)}
            />
            <button>Customize shaders</button>
          </InfoVisibility>
        </div>,
      );
      const scroller = screen.getByTestId("scroller");
      Object.defineProperties(scroller, {
        clientHeight: { value: 600 },
        offsetHeight: { value: 600 },
        scrollHeight: { value: 3000 },
      });
      scroller.getBoundingClientRect = () =>
        new DOMRect(0, 0, 300, 600 * scale);
      const tab = screen.getByRole("button", { name: "Shaders tab" });
      tab.getBoundingClientRect = () =>
        new DOMRect(0, (300 - scroller.scrollTop) * scale, 300, 40 * scale);
      tab.focus();
      flushAnimationFrames();
      fireEvent.click(screen.getByRole("button", { name: /^Choose effects/ }));
      const row = screen.getByRole("checkbox");
      fireEvent(row, new CustomEvent("vgp_onfocus", { bubbles: true }));
      flushAnimationFrames();
      scroller.scrollTop = 1000;
      const customize = screen.getByRole("button", {
        name: "Customize shaders",
      });
      customize.getBoundingClientRect = () => {
        const offset = screen.queryByTestId("mako-effects-list") ? 1440 : 1140;
        return new DOMRect(
          0,
          (offset - scroller.scrollTop) * scale,
          300,
          40 * scale,
        );
      };
      // Model native centering separately from Steam's pending animation. Only
      // Steam's scrollTo wrapper cancels the old destination.
      customize.scrollIntoView = vi.fn(() => {
        scroller.scrollTop +=
          customize.getBoundingClientRect().top / scale - 280;
      });
      const staleAnimation = setTimeout(() => {
        scroller.scrollTop = 200;
      }, 100);
      scroller.scrollTo = vi.fn((options: ScrollToOptions | number) => {
        if (typeof options === "object") {
          if (options.behavior === "instant") clearTimeout(staleAnimation);
          scroller.scrollTop = options.top ?? scroller.scrollTop;
        }
      }) as typeof scroller.scrollTo;
      const detail = {
        blurredNode: { Element: row },
        focusedNode: { Element: customize },
      };
      // Steam runs its scroll hook before the bubbling blur/focus notifications.
      act(() => panelNavigation.scroll?.({ Element: customize }));
      fireEvent(row, new CustomEvent("vgp_onblur", { bubbles: true, detail }));
      fireEvent(
        customize,
        new CustomEvent("vgp_onfocus", { bubbles: true, detail }),
      );
      if (collapseFirst) act(() => vi.advanceTimersByTime(0));
      flushAnimationFrames();
      if (!collapseFirst) act(() => vi.advanceTimersByTime(0));
      flushAnimationFrames();
      act(() => vi.advanceTimersByTime(100));

      expect(screen.queryByTestId("mako-effects-list")).toBeNull();
      expect(customize.getBoundingClientRect().top).toBeCloseTo(280 * scale);
      // Virtual focus does not need to refocus the browser's older tab target.
      expect(document.activeElement).toBe(tab);
      expect(panelNavigation.owner).toBe("force");
    } finally {
      vi.useRealTimers();
    }
  },
);

test("effects collapse follows virtual focus and cancels pending work on reentry or unmount", () => {
  vi.useFakeTimers({ toFake: ["setTimeout", "clearTimeout"] });
  try {
    const { unmount } = render(
      <>
        <EffectsChecklist
          options={[
            { data: "clarity", label: "Clarity" },
            { data: "vibrance", label: "Vibrance" },
          ]}
          initialSelection={["clarity"]}
          onChange={vi.fn(async () => undefined)}
        />
        <button>Customize shaders</button>
      </>,
    );
    const summary = screen.getByRole("button", { name: /^Choose effects/ });
    fireEvent.click(summary);
    const [first, second] = screen.getAllByRole("checkbox");
    const customize = screen.getByRole("button", { name: "Customize shaders" });
    // DOM focus stays inside the list throughout these controller moves.
    first.focus();
    const leave = (target: HTMLElement) =>
      fireEvent(
        first,
        new CustomEvent("vgp_onblur", {
          bubbles: true,
          detail: { focusedNode: { Element: target } },
        }),
      );
    leave(second);
    act(() => vi.advanceTimersByTime(0));
    expect(screen.getByTestId("mako-effects-list")).toBeTruthy();
    leave(customize);
    fireEvent(second, new CustomEvent("vgp_onfocus", { bubbles: true }));
    act(() => vi.advanceTimersByTime(0));
    expect(screen.getByTestId("mako-effects-list")).toBeTruthy();
    leave(customize);
    act(() => vi.advanceTimersByTime(0));
    expect(screen.queryByTestId("mako-effects-list")).toBeNull();
    expect(document.activeElement).not.toBe(customize);

    fireEvent.click(summary);
    fireEvent(
      screen.getAllByRole("checkbox")[0],
      new CustomEvent("vgp_onblur", {
        bubbles: true,
        detail: { focusedNode: { Element: customize } },
      }),
    );
    expect(vi.getTimerCount()).toBe(1);
    unmount();
    expect(vi.getTimerCount()).toBe(0);
  } finally {
    vi.useRealTimers();
  }
});

test.each(["Customize shaders", "Add custom shader", "Refresh"])(
  "leaving Clear all reveals %s after collapse cancels an unfinished scroll",
  (nextLabel) => {
    vi.useFakeTimers({ toFake: ["setTimeout", "clearTimeout"] });
    try {
      render(
        <div
          data-testid="scroller"
          style={{ overflowY: "auto", scrollBehavior: "smooth" }}
        >
          <InfoVisibility>
            <EffectsChecklist
              options={[{ data: "clarity", label: "Clarity" }]}
              initialSelection={["clarity"]}
              onChange={vi.fn(async () => undefined)}
            />
            <button>{nextLabel}</button>
            <button>Following setting</button>
          </InfoVisibility>
        </div>,
      );
      const scroller = screen.getByTestId("scroller");
      Object.defineProperties(scroller, {
        clientHeight: { value: 240 },
        offsetHeight: { value: 240 },
        scrollHeight: {
          get: () => (screen.queryByTestId("mako-effects-list") ? 2000 : 1700),
        },
      });
      scroller.getBoundingClientRect = () => new DOMRect(0, 0, 300, 240);
      fireEvent.click(screen.getByRole("button", { name: /^Choose effects/ }));
      const clear = screen.getByRole("button", { name: "Clear all" });
      const next = screen.getByRole("button", { name: nextLabel });
      const following = screen.getByRole("button", {
        name: "Following setting",
      });
      let pendingScroll: ReturnType<typeof setTimeout> | undefined;
      scroller.scrollTo = vi.fn((options: ScrollToOptions | number) => {
        if (typeof options === "object") {
          clearTimeout(pendingScroll);
          scroller.scrollTop = options.top ?? scroller.scrollTop;
        }
      }) as typeof scroller.scrollTo;
      for (const [control, offset] of [
        [clear, 1280],
        [next, 1450],
        [following, 1650],
      ] as const) {
        control.getBoundingClientRect = () =>
          new DOMRect(
            0,
            offset -
              (screen.queryByTestId("mako-effects-list") ? 0 : 300) -
              scroller.scrollTop,
            280,
            40,
          );
        control.scrollIntoView = vi.fn(
          (options?: boolean | ScrollIntoViewOptions) => {
            const destination =
              scroller.scrollTop + control.getBoundingClientRect().top - 100;
            const reveal = () => {
              scroller.scrollTop = destination;
            };
            // Native `auto` can inherit a smooth scroll. A subsequent scrollTo
            // during collapse cancels it before the button has reached view.
            if (typeof options === "object" && options.behavior === "instant")
              reveal();
            else pendingScroll = setTimeout(reveal, 50);
          },
        );
      }
      fireEvent(clear, new CustomEvent("vgp_onfocus", { bubbles: true }));
      flushAnimationFrames();
      act(() => vi.advanceTimersByTime(50));
      const move = (from: HTMLElement, to: HTMLElement) => {
        const detail = {
          blurredNode: { Element: from },
          focusedNode: { Element: to },
        };
        act(() => panelNavigation.scroll?.({ Element: to }));
        fireEvent(
          from,
          new CustomEvent("vgp_onblur", { bubbles: true, detail }),
        );
        fireEvent(
          to,
          new CustomEvent("vgp_onfocus", { bubbles: true, detail }),
        );
      };
      move(clear, next);
      flushAnimationFrames();
      act(() => vi.advanceTimersByTime(0));
      flushAnimationFrames();
      act(() => vi.advanceTimersByTime(100));
      expect(screen.queryByTestId("mako-effects-list")).toBeNull();
      expect(next.getBoundingClientRect().top).toBeGreaterThanOrEqual(0);
      expect(next.getBoundingClientRect().bottom).toBeLessThanOrEqual(240);
      move(next, following);
      flushAnimationFrames();
      act(() => vi.advanceTimersByTime(100));
      expect(following.getBoundingClientRect().top).toBeGreaterThanOrEqual(0);
      expect(following.getBoundingClientRect().bottom).toBeLessThanOrEqual(240);
    } finally {
      vi.useRealTimers();
    }
  },
);

test.each([240, 480])(
  "tab grid positions only on entry from above in a %i-pixel viewport",
  (viewportHeight) => {
    render(
      <div data-testid="scroller" style={{ overflowY: "auto" }}>
        <InfoVisibility>
          <button>Before tabs</button>
          <div role="tablist" data-mako-focus-scroll="group">
            <button>Top tab</button>
            <button>Bottom tab</button>
          </div>
          <button>After tabs</button>
        </InfoVisibility>
      </div>,
    );
    const scroller = screen.getByTestId("scroller");
    Object.defineProperties(scroller, {
      scrollHeight: { value: 1400 },
      clientHeight: { value: viewportHeight },
      clientTop: { value: 2 },
    });
    vi.spyOn(scroller, "getBoundingClientRect").mockReturnValue(
      new DOMRect(0, 40, 300, viewportHeight + 4),
    );
    const group = screen.getByRole("tablist");
    vi.spyOn(group, "getBoundingClientRect").mockImplementation(
      () => new DOMRect(0, 742 - scroller.scrollTop, 280, 100),
    );
    const before = screen.getByRole("button", { name: "Before tabs" });
    const after = screen.getByRole("button", { name: "After tabs" });
    const first = screen.getByRole("button", { name: "Top tab" });
    const second = screen.getByRole("button", { name: "Bottom tab" });
    // Initial/restored focus has no direction: reveal it without repositioning.
    first.focus();
    flushAnimationFrames();
    expect(scroller.scrollTop).toBe(0);
    expect(first.scrollIntoView).toHaveBeenCalledExactlyOnceWith({
      block: "nearest",
      inline: "nearest",
      behavior: "instant",
    });
    vi.mocked(first.scrollIntoView).mockClear();
    before.focus();
    first.focus();
    // A second direction before the next animation frame must retain entry placement.
    second.focus();
    flushAnimationFrames();
    expect(group.getBoundingClientRect().top + 50 - 42).toBeCloseTo(
      viewportHeight / 3,
    );
    const entryScrollTop = scroller.scrollTop;
    expect(group.scrollIntoView).not.toHaveBeenCalled();

    first.focus();
    flushAnimationFrames();
    expect(first.scrollIntoView).toHaveBeenCalledExactlyOnceWith({
      block: "nearest",
      inline: "nearest",
      behavior: "instant",
    });
    expect(vi.mocked(first.scrollIntoView).mock.contexts).toEqual([first]);
    expect(scroller.scrollTop).toBe(entryScrollTop);

    vi.mocked(first.scrollIntoView).mockClear();
    pressButton(first);
    flushAnimationFrames();
    expect(document.activeElement).toBe(first);
    expect(first.scrollIntoView).not.toHaveBeenCalled();
    expect(scroller.scrollTop).toBe(entryScrollTop);
    second.focus();
    flushAnimationFrames();
    expect(second.scrollIntoView).toHaveBeenCalledExactlyOnceWith({
      block: "nearest",
      inline: "nearest",
      behavior: "instant",
    });
    expect(scroller.scrollTop).toBe(entryScrollTop);

    vi.mocked(after.scrollIntoView).mockClear();
    after.focus();
    flushAnimationFrames();
    expect(after.scrollIntoView).toHaveBeenCalledExactlyOnceWith({
      block: "nearest",
      inline: "nearest",
      behavior: "instant",
    });
    // Returning from below must retain a still-visible grid's current position,
    // even if controller focus moves again before the animation frame.
    vi.mocked(first.scrollIntoView).mockClear();
    scroller.scrollTop = entryScrollTop + 20;
    second.focus();
    first.focus();
    flushAnimationFrames();
    expect(scroller.scrollTop).toBeCloseTo(entryScrollTop + 20);
    expect(first.scrollIntoView).toHaveBeenCalledExactlyOnceWith({
      block: "nearest",
      inline: "nearest",
      behavior: "instant",
    });

    vi.mocked(before.scrollIntoView).mockClear();
    before.focus();
    flushAnimationFrames();
    expect(before.scrollIntoView).toHaveBeenCalledExactlyOnceWith({
      block: "nearest",
      inline: "nearest",
      behavior: "instant",
    });
    first.focus();
    flushAnimationFrames();
    expect(scroller.scrollTop).toBeCloseTo(entryScrollTop);
  },
);

test("leaving a tab grid before its scroll runs cancels entry positioning", () => {
  render(
    <InfoVisibility>
      <button>Before tabs</button>
      <div role="tablist" data-mako-focus-scroll="group">
        <button>Tab</button>
      </div>
      <button>Other control</button>
    </InfoVisibility>,
  );
  screen.getByRole("button", { name: "Before tabs" }).focus();
  screen.getByRole("button", { name: "Tab" }).focus();
  const other = screen.getByRole("button", { name: "Other control" });
  other.focus();
  flushAnimationFrames();
  expect(other.scrollIntoView).toHaveBeenCalledExactlyOnceWith({
    block: "nearest",
    inline: "nearest",
    behavior: "instant",
  });
  expect(vi.mocked(other.scrollIntoView).mock.contexts).toEqual([other]);
});

test("tab entry recognizes controls rendered in a separate Steam window", () => {
  const frame = document.createElement("iframe");
  document.body.append(frame);
  const targetDocument = frame.contentDocument!;
  const { unmount } = render(
    createPortal(
      <div data-testid="scroller" style={{ overflowY: "auto" }}>
        <InfoVisibility>
          <button>Before tabs</button>
          <div role="tablist" data-mako-focus-scroll="group">
            <button>Tab</button>
          </div>
        </InfoVisibility>
      </div>,
      targetDocument.body,
    ),
  );
  try {
    const scroller = targetDocument.querySelector<HTMLElement>(
      '[data-testid="scroller"]',
    )!;
    Object.defineProperties(scroller, {
      scrollHeight: { value: 1400 },
      clientHeight: { value: 240 },
    });
    const group =
      targetDocument.querySelector<HTMLElement>('[role="tablist"]')!;
    vi.spyOn(group, "getBoundingClientRect").mockImplementation(
      () => new DOMRect(0, 700 - scroller.scrollTop, 280, 100),
    );
    const before = scroller.querySelector<HTMLButtonElement>("button")!;
    const tab = group.querySelector<HTMLButtonElement>("button")!;
    scroller.scrollTo = HTMLElement.prototype.scrollTo;
    tab.scrollIntoView = vi.fn();
    before.focus();
    tab.focus();
    flushAnimationFrames();
    expect(targetDocument.activeElement).toBe(tab);
    expect(group.getBoundingClientRect().top + 50).toBeCloseTo(80);
  } finally {
    unmount();
    frame.remove();
  }
});

test("controller focus scrolls without browser focus and keeps rapid moves stable", () => {
  render(
    <div data-testid="scroller" style={{ overflowY: "auto" }}>
      <InfoVisibility>
        <div role="button">Before tabs</div>
        <div role="tablist" data-mako-focus-scroll="group">
          <div role="tab">Top tab</div>
          <div role="tab">Bottom tab</div>
        </div>
        <div role="button">After tabs</div>
      </InfoVisibility>
    </div>,
  );
  const scroller = screen.getByTestId("scroller");
  Object.defineProperties(scroller, {
    scrollHeight: { value: 1400 },
    clientHeight: { value: 240 },
  });
  const group = screen.getByRole("tablist");
  vi.spyOn(group, "getBoundingClientRect").mockImplementation(
    () => new DOMRect(0, 700 - scroller.scrollTop, 280, 100),
  );
  const before = screen.getByRole("button", { name: "Before tabs" });
  const after = screen.getByRole("button", { name: "After tabs" });
  const first = screen.getByRole("tab", { name: "Top tab" });
  const second = screen.getByRole("tab", { name: "Bottom tab" });
  const browserFocus = document.activeElement;
  const gamepad = (target: HTMLElement, type: "focus" | "blur") =>
    fireEvent(target, new CustomEvent(`vgp_on${type}`, { bubbles: true }));
  gamepad(before, "focus");
  gamepad(before, "blur");
  gamepad(first, "focus");
  gamepad(first, "blur");
  gamepad(second, "focus");
  // A duplicate browser notification and late blur cannot erase controller entry.
  fireEvent.focusIn(second, { relatedTarget: first });
  gamepad(first, "blur");
  fireEvent.focusOut(first, { relatedTarget: document.body });
  flushAnimationFrames();
  expect(document.activeElement).toBe(browserFocus);
  expect(group.getBoundingClientRect().top + 50).toBeCloseTo(80);
  const entryScrollTop = scroller.scrollTop;

  vi.mocked(after.scrollIntoView).mockClear();
  gamepad(second, "blur");
  gamepad(after, "focus");
  flushAnimationFrames();
  expect(after.scrollIntoView).toHaveBeenCalledExactlyOnceWith({
    block: "nearest",
    inline: "nearest",
    behavior: "instant",
  });
  vi.mocked(first.scrollIntoView).mockClear();
  scroller.scrollTop = entryScrollTop + 20;
  gamepad(after, "blur");
  gamepad(first, "focus");
  flushAnimationFrames();
  expect(first.scrollIntoView).toHaveBeenCalledExactlyOnceWith({
    block: "nearest",
    inline: "nearest",
    behavior: "instant",
  });
  expect(scroller.scrollTop).toBe(entryScrollTop + 20);

  vi.mocked(second.scrollIntoView).mockClear();
  gamepad(first, "blur");
  gamepad(second, "focus");
  gamepad(second, "blur");
  flushAnimationFrames();
  expect(second.scrollIntoView).not.toHaveBeenCalled();
});

test("a disappearing info control moves to the next visible, enabled control", () => {
  render(
    <InfoVisibility>
      <button>Previous option</button>
      <MakoInfo data-mako-info="true">
        <button>Help</button>
        <button>More help</button>
      </MakoInfo>
      <div className="Steam_FieldDescription">
        <button>Description action</button>
      </div>
      <button disabled>Disabled option</button>
      <fieldset disabled>
        <input aria-label="Disabled field" />
      </fieldset>
      <div aria-disabled="true">
        <button>Unavailable option</button>
      </div>
      <div hidden>
        <button>Hidden option</button>
      </div>
      <div {...{ inert: "" }}>
        <button>Inert option</button>
      </div>
      <div style={{ display: "none" }}>
        <button>Collapsed option</button>
      </div>
      <button style={{ visibility: "hidden" }}>Invisible option</button>
      <div tabIndex={-1}>Not a navigation stop</div>
      <div tabIndex={0} role="slider" aria-label="Flow Scale" />
      <button>Later option</button>
    </InfoVisibility>,
  );
  const help = screen.getByRole("button", { name: "Help" });
  help.focus();
  pressButton(help);
  const next = screen.getByRole("slider", { name: "Flow Scale" });
  expect(document.activeElement).toBe(next);
  expect(help.isConnected).toBe(false);
  flushAnimationFrames();
  expect(next.scrollIntoView).not.toHaveBeenCalled();
  pressButton(next);
  expect(document.activeElement).toBe(next);
});

test.each([true, false])(
  "disappearing final help uses a previous control when available (%s), otherwise the ribbon",
  (hasPrevious) => {
    render(
      <InfoVisibility>
        {hasPrevious && (
          <>
            <button>First option</button>
            <button>Last option</button>
          </>
        )}
        <MakoInfo data-mako-info="true">
          <button>Help</button>
        </MakoInfo>
        <button disabled>Unavailable option</button>
      </InfoVisibility>,
    );
    const help = screen.getByRole("button", { name: "Help" });
    help.focus();
    pressButton(help);
    expect(document.activeElement).toBe(
      screen.getByRole("button", {
        name: hasPrevious ? "Last option" : "Show info",
      }),
    );
  },
);

test("the next control takes the disappearing help control's screen position after reflow", () => {
  render(
    <div data-testid="scroller" style={{ overflowY: "auto" }}>
      <InfoVisibility>
        <MakoInfo data-mako-info="true">
          <button>Help</button>
        </MakoInfo>
        <input aria-label="Next option" />
      </InfoVisibility>
    </div>,
  );
  const scroller = screen.getByTestId("scroller");
  Object.defineProperties(scroller, {
    scrollHeight: { value: 1400 },
    clientHeight: { value: 400 },
  });
  scroller.scrollTop = 600;
  const help = screen.getByRole("button", { name: "Help" });
  vi.spyOn(help, "getBoundingClientRect").mockReturnValue(
    new DOMRect(0, 100, 100, 32),
  );
  const next = screen.getByRole("textbox");
  vi.spyOn(next, "getBoundingClientRect").mockImplementation(
    () =>
      new DOMRect(
        0,
        (next.closest(".Mako_InfoHidden") ? 350 : 1200) - scroller.scrollTop,
        100,
        32,
      ),
  );
  help.focus();
  pressButton(help);
  expect(document.activeElement).toBe(next);
  expect(scroller.scrollTop).toBe(250);
  expect(next.getBoundingClientRect().top).toBe(100);
  flushAnimationFrames();
  expect(next.scrollIntoView).not.toHaveBeenCalled();
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

test.each(["welcome", "development"])(
  "unmounts hidden information while keeping installation status; advances focus from %s",
  async (source) => {
    render(
      <InfoVisibility>
        <ContentNotices
          installationVerified={true}
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
          isInstalled
          installationStatus="MAKO Renderer installed"
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
    const focusedInfo =
      source === "welcome" ? welcomeButton : developmentButton;
    focusedInfo.focus();
    pressButton(focusedInfo);
    expect(document.activeElement).toBe(
      screen.getByRole("button", { name: "Update MAKO Renderer" }),
    );
    for (const text of [
      "Hello from the MAKO Team!",
      "mako-run %command%",
      "MAKO Renderer update required",
    ]) {
      expect(screen.queryByText(text)).toBeNull();
    }
    for (const text of [
      "Lossless Scaling installed (optional)",
      "MAKO Renderer installed",
    ]) {
      expect(isDisplayed(screen.getByText(text))).toBe(true);
    }
    // CSS-hidden buttons still exist in Steam's navigation graph. Check actual
    // removal, including the row that can otherwise become an empty focus stop.
    for (const element of [welcomeButton, welcomeRow, developmentButton]) {
      expect(element.isConnected).toBe(false);
    }
    expect(warningButton.isConnected).toBe(true);
    expect(screen.getAllByTestId("navigation-row")).toHaveLength(4);
    expect(
      screen
        .getAllByRole("button", { hidden: true })
        .map((button) => button.textContent),
    ).toEqual([
      "Updates",
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
  },
);

test.each(["", " \n\t"])(
  "blank installation status keeps useful labels through refreshes and R1 toggles (%j)",
  (blank) => {
    const panel = (
      ready: boolean,
      installationStatus = blank,
      dllDetected = ready,
    ) => (
      <InfoVisibility>
        <StatusDisplay
          dllDetected={dllDetected}
          isInstalled={ready}
          installationStatus={installationStatus}
        />
      </InfoVisibility>
    );
    const expectVisible = (...labels: string[]) => {
      for (const label of labels) {
        expect(isDisplayed(screen.getByText(label))).toBe(true);
      }
    };
    const installed = [
      "MAKO Renderer installed",
      "Lossless Scaling installed (optional)",
    ];
    const losslessMissing = "Lossless Scaling not installed (optional)";
    const { rerender, unmount } = render(panel(true));
    expectVisible(...installed);
    const ribbon = screen.getByRole("button", { name: "Hide info" });
    ribbon.focus();
    pressButton(ribbon);
    expect(screen.getByRole("button", { name: "Show info" })).toBeTruthy();
    expectVisible(...installed);

    rerender(panel(true, "Updating MAKO Renderer..."));
    expectVisible("Updating MAKO Renderer...", installed[1]);
    expect(screen.queryByText(installed[0])).toBeNull();
    rerender(panel(true, "Installation failed: offline", false));
    expectVisible("Installation failed: offline", losslessMissing);
    expect(screen.queryByText(installed[0])).toBeNull();
    expect(screen.queryByText(installed[1])).toBeNull();

    rerender(panel(false));
    expectVisible("Checking MAKO Renderer…", losslessMissing);
    expect(screen.queryByText(installed[0])).toBeNull();
    rerender(panel(true, blank, false));
    expectVisible(installed[0], losslessMissing);
    rerender(panel(true));
    expectVisible(...installed);
    expect(localStorage.getItem("mako-info-hidden")).toBe("true");
    unmount();

    render(panel(true, blank, false));
    expect(screen.getByRole("button", { name: "Show info" })).toBeTruthy();
    expectVisible(installed[0], losslessMissing);
    fireEvent.click(screen.getByRole("button", { name: "Show info" }));
    expect(screen.getByRole("button", { name: "Hide info" })).toBeTruthy();
    expectVisible(installed[0], losslessMissing);
  },
);

test("a new model failure appears while info is hidden, without restoring welcome controls", () => {
  localStorage.setItem("mako-info-hidden", "true");
  const panel = (failed: boolean) => (
    <InfoVisibility>
      <ContentNotices
        installationVerified={true}
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
    message:
      "Lossless Scaling Frame Generation unavailable for this model/precision. MAKO FG selected automatically; a game restart applies the model change.",
  },
  {
    name: "LS1",
    status: { ls1: { compatible: false, reason: "ls1-unavailable" } },
    message: "LS1 unavailable for this model; MAKO Scaler fallback.",
  },
  {
    name: "runtime fallback",
    status: { ls1RuntimeFallback: true },
    message: "LS1 unavailable; MAKO Scaler active.",
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
    expect(screen.getByRole("listitem").textContent).toContain(message);
    expect(warning.textContent).not.toContain("Some Lossless Scaling features");
    expect(isDisplayed(warning.querySelector('[role="note"]')!)).toBe(true);
    expect(screen.queryByText("Optional advice")).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "Show info" }));
    fireEvent.click(screen.getByRole("button", { name: "Hide info" }));
    expect(screen.getByRole("alert")).toBe(warning);
    expect(isDisplayed(warning)).toBe(true);
  },
);

test("one model warning updates its bullets as failures, fallback, and recovery change", () => {
  const failed = { compatible: false, reason: "lsfg-unavailable" };
  const panel = (status: ModelWarningProps) => (
    <InfoVisibility>
      <ModelWarning {...status} />
    </InfoVisibility>
  );
  const { rerender } = render(
    panel({
      ls1: { compatible: false, reason: "ls1-unavailable" },
      lsfg: failed,
    }),
  );
  const warning = screen.getByRole("alert");
  expect(screen.getAllByRole("alert")).toHaveLength(1);
  expect(
    screen.getAllByRole("listitem").map((item) => item.textContent),
  ).toEqual([
    "LS1 unavailable for this model; MAKO Scaler fallback.",
    "Lossless Scaling Frame Generation unavailable for this model/precision. MAKO FG selected automatically; a game restart applies the model change.",
  ]);
  const update = screen.getByRole("button", {
    name: "Check for MAKO Decky updates",
  });
  update.focus();
  pressButton(update);
  expect(screen.getAllByRole("listitem")).toHaveLength(2);
  expect(document.activeElement).toBe(update);

  rerender(
    panel({
      ls1: { compatible: false, reason: "ls1-unavailable" },
      ls1RuntimeFallback: true,
      lsfg: failed,
    }),
  );
  expect(screen.getByRole("alert")).toBe(warning);
  expect(screen.getAllByRole("listitem")).toHaveLength(2);
  expect(warning.textContent).toContain(
    "LS1 unavailable; MAKO Scaler active.",
  );
  expect(warning.textContent).not.toContain(
    "LS1 unavailable for this model; MAKO Scaler fallback.",
  );
  expect(
    screen.getByRole("button", { name: "Check for MAKO Decky updates" }),
  ).toBe(update);

  rerender(panel({ ls1: { compatible: true, reason: null }, lsfg: failed }));
  expect(screen.getByRole("listitem").textContent).toContain(
    "Lossless Scaling Frame Generation unavailable for this model/precision. MAKO FG selected automatically; a game restart applies the model change.",
  );
  rerender(
    panel({
      ls1: { compatible: true, reason: null },
      lsfg: { compatible: null, reason: "inspector-unavailable" },
    }),
  );
  expect(screen.queryByRole("alert")).toBeNull();
});

test.each<{ name: string; status: ModelWarningProps }>([
  {
    name: "LS1 only",
    status: { ls1: { compatible: false, reason: "dll-unavailable" } },
  },
  {
    name: "LSFG only",
    status: { lsfg: { compatible: false, reason: "dll-unavailable" } },
  },
  {
    name: "both model families",
    status: {
      ls1: { compatible: false, reason: "dll-unavailable" },
      lsfg: { compatible: false, reason: "dll-unavailable" },
    },
  },
  {
    name: "an older model failure and runtime fallback",
    status: {
      ls1: { compatible: false, reason: "dll-unavailable" },
      lsfg: { compatible: false, reason: "lsfg-unavailable" },
      ls1RuntimeFallback: true,
    },
  },
])(
  "a missing DLL keeps independent-feature guidance visible for $name",
  ({ status }) => {
    const panel = (modelStatus: ModelWarningProps) => (
      <InfoVisibility>
        <ModelWarning {...modelStatus} />
      </InfoVisibility>
    );
    const { rerender } = render(panel(status));
    expect(screen.getAllByRole("alert")).toHaveLength(1);
    expect(screen.getAllByRole("listitem")).toHaveLength(1);
    expect(screen.getByRole("alert").textContent).toContain(
      "Shaders remain available.",
    );
    expect(screen.getByRole("alert").querySelector("button")).toBeNull();
    expect(screen.queryByRole("button", { name: "Use MAKO FG" })).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "Hide info" }));
    expect(screen.getByRole("alert").getAttribute("data-mako-info")).not.toBe(
      "true",
    );
    expect(screen.getByRole("alert").textContent).toContain(
      "Shaders remain available.",
    );
    rerender(
      panel({
        lsfg: { compatible: false, reason: "lsfg-unavailable" },
      }),
    );
    expect(screen.queryByRole("button", { name: "Use MAKO FG" })).toBeNull();
    rerender(panel(status));
    expect(screen.queryByRole("button", { name: "Use MAKO FG" })).toBeNull();
    rerender(panel({}));
    expect(screen.queryByRole("alert")).toBeNull();
    expect(screen.queryByTestId("navigation-row")).toBeNull();
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
    expect(screen.getByRole("alert").contains(update)).toBe(true);
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
