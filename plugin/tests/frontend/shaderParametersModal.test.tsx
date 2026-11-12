import React from "react";
import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
  within,
} from "@testing-library/react";
import { afterEach, beforeEach, expect, test, vi } from "vitest";
const api = vi.hoisted(() => ({ get: vi.fn(), set: vi.fn() }));
const navigation = vi.hoisted(() => ({
  cancel: undefined as ((event: CustomEvent) => void) | undefined,
  escape: undefined as (() => void) | undefined,
  focus: undefined as ((event: { target: EventTarget }) => void) | undefined,
  scroll: undefined as
    ((node: { Element?: HTMLElement | null }) => boolean) | undefined,
  scrollOwner: undefined as boolean | "force" | undefined,
}));
vi.mock("../../src/api/makoApi", () => ({
  getShaderParameters: api.get,
  setShaderParameters: api.set,
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoFocusable: ({
    children,
    onCancel,
    onActivate,
    onGamepadFocus,
    onGamepadBlur,
    noFocusRing: _noFocusRing,
    fnScrollIntoViewHandler,
    scrollIntoViewWhenChildFocused,
    "flow-children": _flow,
    ...props
  }: React.HTMLAttributes<HTMLDivElement> & {
    onCancel?: (event: CustomEvent) => void;
    onActivate?: () => void;
    onGamepadFocus?: (event: { target: EventTarget }) => void;
    onGamepadBlur?: () => void;
    noFocusRing?: boolean;
    fnScrollIntoViewHandler?: (node: {
      Element?: HTMLElement | null;
    }) => boolean;
    scrollIntoViewWhenChildFocused?: boolean | "force";
    "flow-children"?: string;
  }) => {
    const ref = React.useRef<HTMLDivElement>(null);
    React.useEffect(() => {
      const element = ref.current!;
      const activate = () => onActivate?.();
      const focus = () => onGamepadFocus?.({ target: element });
      const blur = () => onGamepadBlur?.();
      element.addEventListener("vgp_onactivate", activate);
      element.addEventListener("vgp_onfocus", focus);
      element.addEventListener("vgp_onblur", blur);
      return () => {
        element.removeEventListener("vgp_onactivate", activate);
        element.removeEventListener("vgp_onfocus", focus);
        element.removeEventListener("vgp_onblur", blur);
      };
    }, [onActivate, onGamepadFocus, onGamepadBlur]);
    if (props.className === "mako-shader-editor-content") {
      navigation.cancel = onCancel;
      navigation.focus = onGamepadFocus;
      navigation.scroll = fnScrollIntoViewHandler;
      navigation.scrollOwner = scrollIntoViewWhenChildFocused;
    }
    return <div ref={ref} {...props}>{children}</div>;
  },
}));
vi.mock("@decky/ui", () => ({
  ModalRoot: ({
    children,
    onEscKeypress,
  }: {
    children: React.ReactNode;
    onEscKeypress: () => void;
  }) => {
    navigation.escape = onEscKeypress;
    return (
      <div role="dialog">
        <div className="DialogContent_InnerWidth">{children}</div>
      </div>
    );
  },
  DialogButton: ({
    children,
    onClick,
  }: {
    children: React.ReactNode;
    onClick: () => void;
  }) => <button onClick={onClick}>{children}</button>,
  Field: ({
    children,
    label,
  }: {
    children: React.ReactNode;
    label: string;
  }) => (
    <label>
      {label}
      {children}
    </label>
  ),
  Dropdown: ({
    rgOptions,
    selectedOption,
    onChange,
  }: {
    rgOptions: { label: string; data: string | number }[];
    selectedOption: string | number;
    onChange: (v: { data: string }) => void;
  }) => (
    <select
      value={selectedOption}
      onChange={(e) => onChange({ data: e.target.value })}
    >
      {rgOptions.map((o) => (
        <option key={o.data} value={o.data}>
          {o.label}
        </option>
      ))}
    </select>
  ),
  SliderField: ({
    label,
    value,
    min,
    max,
    step,
    validValues,
    minimumDpadGranularity,
    onChange,
  }: {
    label: string;
    value: number;
    min: number;
    max: number;
    step: number;
    validValues: string;
    minimumDpadGranularity: number;
    onChange: (v: number) => void;
  }) => (
    <input
      aria-label={label}
      type="range"
      value={value}
      min={min}
      max={max}
      step={step}
      data-valid-values={validValues}
      data-dpad-step={minimumDpadGranularity}
      onChange={(e) => onChange(Number(e.target.value))}
    />
  ),
  ToggleField: ({
    label,
    checked,
    onChange,
  }: {
    label: string;
    checked: boolean;
    onChange: (v: boolean) => void;
  }) => (
    <input
      aria-label={label}
      type="checkbox"
      checked={checked}
      onChange={(e) => onChange(e.target.checked)}
    />
  ),
  TextField: ({ label, ...props }: { label: string }) => (
    <input aria-label={label} {...props} />
  ),
}));
import { ShaderParametersModal } from "../../src/components/settings/shaders/ShaderParametersModal";
const effect = {
  id: "Tone",
  name: "Tone",
  path: "/tone.fx",
  error: null,
  parameters: [
    {
      key: "strength",
      label: "Strength",
      type: "float",
      value: 0.5,
      default: 0.25,
      min: 0,
      max: 1,
      step: 0.01,
      category: "Colour",
      description: "",
      items: [],
    },
    {
      key: "amount",
      label: "Amount",
      type: "float",
      value: 1,
      default: 0,
      min: null,
      max: null,
      step: null,
      category: "",
      description: "",
      items: [],
    },
  ],
};
beforeEach(() => {
  window.SP_REACT = React;
  HTMLElement.prototype.scrollIntoView = vi.fn();
  vi.useFakeTimers();
  api.get.mockResolvedValue({ success: true, effects: [effect] });
  api.set.mockResolvedValue({ success: true, effects: [effect] });
});
afterEach(() => {
  cleanup();
  vi.useRealTimers();
});
async function open(closeModal = vi.fn(), onReturn = vi.fn()) {
  await act(async () => {
    render(
      <ShaderParametersModal
        profileName="Game A"
        closeModal={closeModal}
        onReturn={onReturn}
      />,
    );
  });
  return closeModal;
}
test("the header reports discovery before showing a saved state", async () => {
  let complete!: (result: { success: boolean; effects: typeof effect[] }) => void;
  api.get.mockImplementationOnce(
    () => new Promise((resolve) => {
      complete = resolve;
    }),
  );
  await open();
  const status = screen.getByRole("status");
  expect(status.textContent).toBe("Reading shader options...");
  expect(status.closest(".mako-shader-editor-header")).toBeTruthy();
  await act(async () => {
    complete({ success: true, effects: [effect] });
  });
  expect(screen.getByRole("status")).toBe(status);
  expect(status.textContent).toBe("Saved to this profile and applied live.");
});

test("one stationary header status covers debounce, serialized saves and hidden controls", async () => {
  const completions: ((result: { success: boolean }) => void)[] = [];
  api.set.mockImplementation(
    () => new Promise((resolve) => {
      completions.push(resolve);
    }),
  );
  await open();
  const status = screen.getByRole("status");
  const slider = screen.getByRole("slider");
  fireEvent.change(slider, { target: { value: "0.6" } });
  // Waiting for debounce is already unsaved, even before the RPC starts.
  expect(status.textContent).toBe("Saving...");
  expect(api.set).not.toHaveBeenCalled();
  expect(screen.queryByText("Saved to this profile and applied live.")).toBeNull();
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  fireEvent.change(slider, { target: { value: "0.9" } });
  fireEvent.click(screen.getByText("Hide controls"));
  expect(screen.getByRole("status")).toBe(status);
  expect(status.textContent).toBe("Saving...");
  await act(async () => {
    completions[0]({ success: true });
  });
  expect(api.set).toHaveBeenCalledTimes(2);
  expect(api.set).toHaveBeenLastCalledWith("Game A", { strength: 0.9 });
  expect(status.textContent).toBe("Saving...");
  await act(async () => {
    completions[1]({ success: true });
    await vi.advanceTimersByTimeAsync(150);
  });
  expect(screen.getByRole("status")).toBe(status);
  expect(status.textContent).toBe("Saved to this profile and applied live.");
  fireEvent.click(screen.getByText("Show controls"));
  expect(status.closest(".mako-shader-editor-header")).toBeTruthy();
  expect(
    (screen.getByRole("textbox", { name: "Strength" }) as HTMLInputElement).value,
  ).toBe("0.9");
  // The queued edit's old debounce timer cannot produce another write or status.
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).toHaveBeenCalledTimes(2);
  api.set.mockResolvedValue({ success: true });
});

test("fast saves keep Saving visible for 500 ms including the debounce interval", async () => {
  await open();
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.6" } });
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).toHaveBeenCalledExactlyOnceWith("Game A", { strength: 0.6 });
  expect(screen.getByRole("status").textContent).toBe("Saving...");
  await act(async () => {
    await vi.advanceTimersByTimeAsync(149);
  });
  expect(screen.getByRole("status").textContent).toBe("Saving...");
  await act(async () => {
    await vi.advanceTimersByTimeAsync(1);
  });
  expect(screen.getByRole("status").textContent).toBe(
    "Saved to this profile and applied live.",
  );
});

test("an edit during the minimum display interval cancels the old completion timer", async () => {
  await open();
  const slider = screen.getByRole("slider");
  fireEvent.change(slider, { target: { value: "0.6" } });
  await act(async () => {
    await vi.advanceTimersByTimeAsync(400);
  });
  fireEvent.change(slider, { target: { value: "0.9" } });
  await act(async () => {
    await vi.advanceTimersByTimeAsync(100);
  });
  expect(screen.getByRole("status").textContent).toBe("Saving...");
  expect(api.set).toHaveBeenCalledTimes(1);
  await act(async () => {
    await vi.advanceTimersByTimeAsync(250);
  });
  expect(api.set).toHaveBeenLastCalledWith("Game A", { strength: 0.9 });
  expect(api.set).toHaveBeenCalledTimes(2);
  expect(screen.getByRole("status").textContent).toBe(
    "Saved to this profile and applied live.",
  );
});

test("slow saves finish their status immediately once the real write completes", async () => {
  let complete!: (result: { success: boolean }) => void;
  api.set.mockImplementationOnce(
    () => new Promise((resolve) => {
      complete = resolve;
    }),
  );
  await open();
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.6" } });
  await act(async () => {
    await vi.advanceTimersByTimeAsync(1000);
  });
  expect(screen.getByRole("status").textContent).toBe("Saving...");
  await act(async () => {
    complete({ success: true });
  });
  expect(screen.getByRole("status").textContent).toBe(
    "Saved to this profile and applied live.",
  );
});

test("the minimum status duration does not delay closing or repeat the saved edit", async () => {
  const close = await open();
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.6" } });
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(screen.getByRole("status").textContent).toBe("Saving...");
  await act(async () => {
    fireEvent.click(screen.getByText("Close"));
  });
  expect(close).toHaveBeenCalledTimes(1);
  expect(api.set).toHaveBeenCalledTimes(1);
});

test("unmount cancels the pending status timer", async () => {
  await open();
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.6" } });
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(screen.getByRole("status").textContent).toBe("Saving...");
  expect(vi.getTimerCount()).toBe(1);
  cleanup();
  expect(vi.getTimerCount()).toBe(0);
});

test("discovery errors never claim the selected shaders have no editable parameters", async () => {
  api.get.mockResolvedValue({
    success: false,
    effects: [],
    error: "GLIBCXX version not found",
  });
  await open();
  expect(screen.getByRole("alert").textContent).toContain(
    "GLIBCXX version not found",
  );
  expect(
    screen.queryByText("No editable parameters in the selected shaders."),
  ).toBeNull();
  expect(screen.queryByRole("slider")).toBeNull();
  expect(screen.queryByText("Saved to this profile and applied live.")).toBeNull();
  expect(screen.queryByText("Saving...")).toBeNull();
});
test("successful discovery without parameters retains the empty message", async () => {
  api.get.mockResolvedValue({ success: true, effects: [] });
  await open();
  expect(
    screen.getByText("No editable parameters in the selected shaders."),
  ).toBeTruthy();
  expect(screen.queryByRole("alert")).toBeNull();
});
test("reflects shader controls and coalesces slider movement into one profile-specific update", async () => {
  await open();
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.6" } });
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.7" } });
  expect(api.set).not.toHaveBeenCalled();
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).toHaveBeenCalledExactlyOnceWith("Game A", { strength: 0.7 });
});

test("small shader values stay readable instead of displaying as zero", async () => {
  api.get.mockResolvedValue({
    success: true,
    effects: [
      {
        ...effect,
        parameters: [
          { ...effect.parameters[0], value: 0.000001, step: 0.000001 },
        ],
      },
    ],
  });
  await open();
  expect(
    screen.getByRole("slider", { name: "Strength: 0.000001" }),
  ).toBeTruthy();
});
test("all numeric displays hide FP32 noise without saving on blur", async () => {
  api.get.mockResolvedValue({
    success: true,
    effects: [
      {
        ...effect,
        parameters: [{ ...effect.parameters[0], value: 3.4000001, max: 100 }],
      },
    ],
  });
  await open();
  expect(screen.getByRole("slider", { name: "Strength: 3.4" })).toBeTruthy();
  const input = screen.getByRole("textbox", { name: "Strength" });
  expect((input as HTMLInputElement).value).toBe("3.4");
  fireEvent.blur(input);
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).not.toHaveBeenCalled();
});

test("Gamma uses hundredths for sliders while exact fields keep finer edits", async () => {
  api.get.mockResolvedValue({
    success: true,
    effects: [
      {
        ...effect,
        parameters: [
          {
            ...effect.parameters[0],
            label: "Gamma",
            min: 0.5,
            max: 2,
            step: null,
            value: 1.05499995,
          },
        ],
      },
    ],
  });
  await open();
  const slider = screen.getByRole("slider", { name: "Gamma: 1.055" });
  expect(slider.getAttribute("step")).toBe("0.01");
  expect(slider.getAttribute("data-dpad-step")).toBe("0.01");
  expect(slider.getAttribute("data-valid-values")).toBe("steps");
  const input = screen.getByRole("textbox", { name: "Gamma" });
  expect((input as HTMLInputElement).value).toBe("1.055");
  fireEvent.blur(input);
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).not.toHaveBeenCalled();
  fireEvent.change(slider, { target: { value: "1.0499999999999998" } });
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).toHaveBeenLastCalledWith("Game A", { strength: 1.05 });
  expect((input as HTMLInputElement).value).toBe("1.05");
  fireEvent.change(input, { target: { value: "1.05789" } });
  fireEvent.blur(input);
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).toHaveBeenLastCalledWith("Game A", { strength: 1.05789 });
  expect((input as HTMLInputElement).value).toBe("1.05789");
});

test("unbounded numeric fields also shorten readback noise without a save", async () => {
  api.get.mockResolvedValue({
    success: true,
    effects: [
      {
        ...effect,
        parameters: [
          { ...effect.parameters[0], min: null, max: null, value: 1.04999995 },
        ],
      },
    ],
  });
  const close = await open();
  expect(screen.queryByRole("slider")).toBeNull();
  const input = screen.getByRole("textbox", { name: "Strength" });
  expect((input as HTMLInputElement).value).toBe("1.05");
  input.focus();
  await act(async () => {
    fireEvent.click(screen.getByText("Close"));
  });
  expect(api.set).not.toHaveBeenCalled();
  expect(close).toHaveBeenCalledOnce();
});
test("reset uses compiler defaults, and closing drains pending writes", async () => {
  const close = await open();
  fireEvent.click(screen.getByText("Reset shader to defaults"));
  await act(async () => {
    fireEvent.click(screen.getByText("Close"));
  });
  expect(api.set).toHaveBeenCalledWith("Game A", {
    strength: null,
    amount: null,
  });
  expect(close).toHaveBeenCalledTimes(1);
});
test("failed saves stay visible and retry without discarding the edit", async () => {
  api.set.mockResolvedValueOnce({ success: false, error: "disk full" });
  const close = await open();
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.8" } });
  await act(async () => {
    fireEvent.click(screen.getByText("Close"));
  });
  expect(close).not.toHaveBeenCalled();
  expect(screen.getByRole("alert").textContent).toContain("disk full");
  expect(screen.getByRole("status").textContent).toBe("Changes not saved.");
  expect(screen.queryByText("Saved to this profile and applied live.")).toBeNull();
  expect(screen.queryByText("Saving...")).toBeNull();
  await act(async () => {
    fireEvent.click(screen.getByText("Retry save"));
  });
  expect(api.set).toHaveBeenLastCalledWith("Game A", { strength: 0.8 });
  expect(screen.getByRole("status").textContent).toBe("Saving...");
  await act(async () => {
    await vi.advanceTimersByTimeAsync(500);
  });
  expect(screen.getByRole("status").textContent).toBe(
    "Saved to this profile and applied live.",
  );
  expect(screen.queryByRole("alert")).toBeNull();
});
test("numeric input permits intermediate text and commits a valid value on blur", async () => {
  await open();
  const field = screen.getByLabelText("Amount");
  fireEvent.change(field, { target: { value: "-" } });
  fireEvent.blur(field);
  expect(api.set).not.toHaveBeenCalled();
  fireEvent.change(field, { target: { value: "-0.25" } });
  fireEvent.blur(field);
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).toHaveBeenCalledWith("Game A", { amount: -0.25 });
});
test("switching effect and hiding controls keeps edits and effect order", async () => {
  api.get.mockResolvedValue({
    success: true,
    effects: [effect, { ...effect, id: "Custom", name: "Custom" }],
  });
  await open();
  fireEvent.click(screen.getByText("Hide controls"));
  expect(screen.queryByRole("slider")).toBeNull();
  fireEvent.click(screen.getByText("Show controls"));
  expect(screen.getAllByRole("option").map((o) => o.textContent)).toEqual([
    "Tone",
    "Custom",
  ]);
});

test("close waits for in-flight edits and then writes the latest queued value", async () => {
  let complete!: (result: { success: boolean }) => void;
  api.set.mockImplementationOnce(
    () =>
      new Promise((resolve) => {
        complete = resolve;
      }),
  );
  const close = await open();
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.6" } });
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.9" } });
  fireEvent.click(screen.getByText("Close"));
  expect(api.set).toHaveBeenCalledTimes(1);
  expect(close).not.toHaveBeenCalled();
  await act(async () => {
    complete({ success: true });
  });
  expect(api.set).toHaveBeenCalledTimes(2);
  expect(api.set).toHaveBeenLastCalledWith("Game A", { strength: 0.9 });
  expect(close).toHaveBeenCalledTimes(1);
});

test("closing commits the focused numeric draft before draining saves", async () => {
  const close = await open();
  const field = screen.getByLabelText("Amount");
  field.focus();
  fireEvent.change(field, { target: { value: "-0.25" } });
  await act(async () => {
    fireEvent.click(screen.getByText("Close"));
  });
  expect(api.set).toHaveBeenCalledWith("Game A", { amount: -0.25 });
  expect(close).toHaveBeenCalledTimes(1);
});

test("closing commits drafts when Steam renders the modal in another document", async () => {
  const frame = document.createElement("iframe");
  document.body.appendChild(frame);
  const body = frame.contentDocument!.body;
  const close = vi.fn();
  let unmount!: () => void;
  await act(async () => {
    ({ unmount } = render(
      <ShaderParametersModal profileName="Game A" closeModal={close} />,
      { container: body },
    ));
  });
  const field = within(body).getByLabelText("Amount");
  field.scrollIntoView = vi.fn();
  field.focus();
  fireEvent.change(field, { target: { value: "0.75" } });
  await act(async () => {
    fireEvent.click(within(body).getByText("Close"));
  });
  expect(api.set).toHaveBeenCalledExactlyOnceWith("Game A", { amount: 0.75 });
  expect(close).toHaveBeenCalledTimes(1);
  unmount();
  frame.remove();
});

test.each(["B", "Close", "Escape"])(
  "%s returns to Decky exactly once after saving, including hidden controls",
  async (action) => {
    let complete!: (result: { success: boolean }) => void;
    api.set.mockImplementationOnce(
      () =>
        new Promise((resolve) => {
          complete = resolve;
        }),
    );
    const returned = vi.fn();
    const close = await open(vi.fn(), returned);
    fireEvent.change(screen.getByRole("slider"), { target: { value: "0.8" } });
    fireEvent.click(screen.getByText("Hide controls"));
    const cancel = new CustomEvent("cancel", { cancelable: true });
    const stop = vi.spyOn(cancel, "stopPropagation");
    const trigger = () => {
      if (action === "B") navigation.cancel!(cancel);
      else if (action === "Escape") navigation.escape!();
      else fireEvent.click(screen.getByText("Close"));
    };
    await act(async () => {
      trigger();
      trigger();
    });
    expect(close).not.toHaveBeenCalled();
    expect(returned).not.toHaveBeenCalled();
    if (action === "B") {
      expect(cancel.defaultPrevented).toBe(true);
      expect(stop).toHaveBeenCalled();
    }
    await act(async () => {
      complete({ success: true });
    });
    expect(api.set).toHaveBeenCalledExactlyOnceWith("Game A", {
      strength: 0.8,
    });
    expect(close).toHaveBeenCalledTimes(1);
    expect(returned).toHaveBeenCalledTimes(1);
    expect(close.mock.invocationCallOrder[0]).toBeLessThan(
      returned.mock.invocationCallOrder[0],
    );
  },
);

test("B stays in the editor on save failure, and explicit discard returns without retrying", async () => {
  api.set.mockResolvedValueOnce({ success: false, error: "disk full" });
  const returned = vi.fn();
  const close = await open(vi.fn(), returned);
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.8" } });
  await act(async () => {
    navigation.cancel!(new CustomEvent("cancel"));
  });
  expect(close).not.toHaveBeenCalled();
  expect(returned).not.toHaveBeenCalled();
  fireEvent.click(screen.getByText("Discard unsaved changes"));
  expect(close).toHaveBeenCalledTimes(1);
  expect(returned).toHaveBeenCalledTimes(1);
  expect(api.set).toHaveBeenCalledTimes(1);
});

// Layout is supplied explicitly because jsdom does not compute scroll geometry.
function layoutScrollRegion(
  elements: [HTMLElement, number, number?][],
  scale = 1,
) {
  const region = elements[0][0].closest<HTMLElement>(
    ".mako-shader-editor-controls, .mako-shader-editor-feedback",
  )!;
  Object.defineProperties(region, {
    clientHeight: { configurable: true, value: 200 },
    offsetHeight: { configurable: true, value: 200 },
    scrollHeight: { configurable: true, value: 1200 },
  });
  region.getBoundingClientRect = () =>
    ({ top: 100, bottom: 100 + 200 * scale, height: 200 * scale }) as DOMRect;
  for (const [element, offset, height = 40] of elements) {
    element.getBoundingClientRect = () =>
      ({
        top: 100 + (offset - region.scrollTop) * scale,
        bottom: 100 + (offset + height - region.scrollTop) * scale,
        height: height * scale,
      }) as DOMRect;
  }
  region.scrollTo = vi.fn((options: ScrollToOptions | number) => {
    if (typeof options === "object")
      region.scrollTop = options.top ?? region.scrollTop;
  }) as typeof region.scrollTo;
  return region;
}

function expectInside(region: HTMLElement, element: HTMLElement) {
  const frame = region.getBoundingClientRect();
  const target = element.getBoundingClientRect();
  const scale = frame.height / region.offsetHeight;
  const inset = region.classList.contains("mako-shader-editor-controls") ? 12 : 4;
  expect(target.top).toBeGreaterThanOrEqual(frame.top + inset * scale);
  expect(target.bottom).toBeLessThanOrEqual(frame.bottom - inset * scale);
}

test("browser and virtual controller focus scroll only their own region without animation", async () => {
  await open();
  const field = screen.getByLabelText("Amount");
  const region = layoutScrollRegion([[field, 400]]);
  field.focus();
  expectInside(region, field);
  expect(region.scrollTo).toHaveBeenLastCalledWith({
    top: 252,
    behavior: "instant",
  });
  field.blur();
  region.scrollTop = 0;
  navigation.focus!({ target: field });
  expect(document.activeElement).not.toBe(field);
  expectInside(region, field);
  expect(field.scrollIntoView).not.toHaveBeenCalled();
});

test("rapid virtual focus reaches Reset and cancels an older Steam scroll animation", async () => {
  await open();
  const slider = screen.getByRole("slider");
  const field = screen.getByLabelText("Amount");
  const reset = screen.getByText("Reset shader to defaults");
  const region = layoutScrollRegion([
    [slider, 300],
    [field, 600],
    [reset, 1000],
  ]);
  // Steam's wrapper retains a destination while animating and cancels only
  // when scrollTo is called again with a non-smooth behavior.
  const staleAnimation = setTimeout(() => {
    region.scrollTop = 200;
  }, 200);
  region.scrollTo = vi.fn((options: ScrollToOptions | number) => {
    if (typeof options === "object") {
      expect(options.behavior).toBe("instant");
      clearTimeout(staleAnimation);
      region.scrollTop = options.top ?? region.scrollTop;
    }
  }) as typeof region.scrollTo;
  expect(navigation.scrollOwner).toBe("force");
  for (const target of [slider, field, reset]) {
    navigation.focus!({ target });
    // Steam runs its scroll hook after notifying focus observers.
    expect(navigation.scroll!({ Element: target })).toBe(true);
    expectInside(region, target);
    await act(async () => {
      await vi.advanceTimersByTimeAsync(10);
    });
  }
  await act(async () => {
    await vi.advanceTimersByTimeAsync(250);
  });
  expectInside(region, reset);
  expect(document.activeElement).not.toBe(reset); // virtual focus stays supported
  expect(api.set).not.toHaveBeenCalled();
  fireEvent.click(reset);
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).toHaveBeenCalledExactlyOnceWith("Game A", {
    strength: null,
    amount: null,
  });
  for (const target of [field, slider]) {
    navigation.focus!({ target });
    navigation.scroll!({ Element: target });
    expectInside(region, target);
  }
});

test.each([0.75, 1.5])(
  "controller scrolling accounts for Steam UI scale %s and leaves visible controls in place",
  async (scale) => {
    await open();
    const field = screen.getByLabelText("Amount");
    const region = layoutScrollRegion([[field, 400]], scale);
    navigation.scroll!({ Element: field });
    expectInside(region, field);
    const top = region.scrollTop;
    navigation.focus!({ target: field });
    expect(region.scrollTop).toBe(top);
    navigation.focus!({ target: screen.getByText("Close") });
    expect(region.scrollTop).toBe(top);
    expect(document.scrollingElement?.scrollTop ?? 0).toBe(0);
  },
);

test("controller scrolling also reveals retry controls after a long error", async () => {
  api.set.mockResolvedValueOnce({ success: false, error: "disk full" });
  await open();
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.8" } });
  await act(async () => {
    fireEvent.click(screen.getByText("Close"));
  });
  const retry = screen.getByText("Retry save");
  const region = layoutScrollRegion([[retry, 800]]);
  navigation.scroll!({ Element: retry });
  expectInside(region, retry);
  await act(async () => {
    fireEvent.click(retry);
  });
  expect(api.set).toHaveBeenLastCalledWith("Game A", { strength: 0.8 });
});

test("tall focused controls align their start without overscrolling and hidden controls cannot move the panel", async () => {
  await open();
  const field = screen.getByLabelText("Amount");
  const region = layoutScrollRegion([[field, 400, 500]]);
  navigation.scroll!({ Element: field });
  expect(region.scrollTop).toBe(388);
  fireEvent.click(screen.getByText("Hide controls"));
  vi.mocked(region.scrollTo).mockClear();
  navigation.scroll!({ Element: field });
  expect(region.scrollTo).not.toHaveBeenCalled();
  expect(() => navigation.scroll!({ Element: null })).not.toThrow();
});

test("virtual focus in another Steam document scrolls its local panel", async () => {
  const frame = document.createElement("iframe");
  document.body.appendChild(frame);
  const body = frame.contentDocument!.body;
  let unmount!: () => void;
  await act(async () => {
    ({ unmount } = render(<ShaderParametersModal profileName="Game A" />, {
      container: body,
    }));
  });
  const field = within(body).getByLabelText("Amount");
  const region = layoutScrollRegion([[field, 400]]);
  navigation.scroll!({ Element: field });
  expectInside(region, field);
  unmount();
  frame.remove();
});

test("closing drains edits made during the first save and closes only once", async () => {
  let complete!: (result: { success: boolean }) => void;
  api.set.mockImplementationOnce(
    () =>
      new Promise((resolve) => {
        complete = resolve;
      }),
  );
  const close = await open();
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.6" } });
  fireEvent.click(screen.getByText("Close"));
  fireEvent.change(screen.getByRole("slider"), { target: { value: "0.9" } });
  fireEvent.click(screen.getByText("Close"));
  expect(close).not.toHaveBeenCalled();
  await act(async () => {
    complete({ success: true });
  });
  expect(api.set).toHaveBeenLastCalledWith("Game A", { strength: 0.9 });
  expect(close).toHaveBeenCalledTimes(1);
});

test("switching shaders commits the focused draft before its control unmounts", async () => {
  api.get.mockResolvedValue({
    success: true,
    effects: [
      effect,
      { ...effect, id: "Other", name: "Other", parameters: [] },
    ],
  });
  await open();
  const field = screen.getByLabelText("Amount");
  field.focus();
  fireEvent.change(field, { target: { value: "0.75" } });
  fireEvent.change(screen.getByRole("combobox"), {
    target: { value: "Other" },
  });
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).toHaveBeenCalledExactlyOnceWith("Game A", { amount: 0.75 });
});

test("hiding controls commits the draft and invalid text returns to the saved value", async () => {
  await open();
  const field = screen.getByLabelText("Amount");
  fireEvent.change(field, { target: { value: "-" } });
  fireEvent.blur(field);
  expect((field as HTMLInputElement).value).toBe("1");
  field.focus();
  fireEvent.change(field, { target: { value: "0.75" } });
  fireEvent.click(screen.getByText("Hide controls"));
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).toHaveBeenCalledExactlyOnceWith("Game A", { amount: 0.75 });
});

function colorEffect() {
  const group = "makoShader_41_436f6c6f7572_0";
  return {
    ...effect,
    parameters: [0.123456789, 0.2, 1.5, 0.4].map((value, component) => ({
      ...effect.parameters[1],
      key: group.slice(0, -1) + component,
      label: `Border colour [${component + 1}]`,
      value,
      description: "Authored colour help",
      category: "Colour",
      presentation: {
        widget: "color",
        group,
        label: "Border colour",
        component,
        count: 4,
      },
    })),
  };
}

function paletteEffect() {
  return {
    ...effect,
    name: "Nostalgia",
    parameters: Array.from({ length: 16 }, (_, index) => {
      const group = `makoShader_41_${Buffer.from(`Palette${index}`).toString("hex")}_0`;
      return colorEffect().parameters.slice(0, 3).map((p, component) => ({
        ...p,
        key: group.slice(0, -1) + component,
        label: `Color ${index} [${component + 1}]`,
        category: "Custom palette",
        description: "",
        min: null,
        max: null,
        step: null,
        presentation: {
          ...p.presentation,
          group,
          label: `Color ${index}`,
          count: 3,
        },
      }));
    }).flat(),
  };
}

test("a sixteen-colour palette shows swatches and edits one colour without losing focused drafts", async () => {
  const palette = paletteEffect();
  api.get.mockResolvedValue({ success: true, effects: [palette] });
  api.set.mockResolvedValue({ success: true });
  await open();
  expect(screen.getAllByRole("button", { name: /^Color \d+$/ })).toHaveLength(16);
  expect(screen.getAllByRole("textbox")).toHaveLength(3);
  expect(screen.getAllByRole("slider")).toHaveLength(3);
  expect(screen.getAllByText("Custom palette")).toHaveLength(1);
  const first = screen.getByRole("button", { name: "Color 0" });
  const next = screen.getByRole("button", { name: "Color 5" });
  expect(first.getAttribute("aria-pressed")).toBe("true");

  // Controller focus can move without blurring the browser's active input.
  const red = screen.getByRole("textbox", { name: "Red" });
  red.focus();
  fireEvent.change(red, { target: { value: "0.345678912" } });
  fireEvent(next, new CustomEvent("vgp_onfocus"));
  expect(next.style.outline).toBe("2px solid #edf8fb");
  expect(next.getAttribute("aria-pressed")).toBe("false");
  expect(api.set).not.toHaveBeenCalled();
  fireEvent(next, new CustomEvent("vgp_onactivate"));
  expect(next.getAttribute("aria-pressed")).toBe("true");
  expect(first.getAttribute("aria-pressed")).toBe("false");
  expect((screen.getByRole("textbox", { name: "Red" }) as HTMLInputElement).value)
    .toBe("0.123456789");
  await act(async () => { await vi.advanceTimersByTimeAsync(350); });
  expect(api.set).toHaveBeenCalledExactlyOnceWith("Game A", {
    [palette.parameters[0].key]: 0.345678912,
  });

  fireEvent.keyDown(first, { key: "Enter" });
  expect((screen.getByRole("textbox", { name: "Red" }) as HTMLInputElement).value)
    .toBe("0.345678912");
  expect((screen.getByRole("textbox", { name: "Blue" }) as HTMLInputElement).value)
    .toBe("1.5");
  expect(first.querySelector('[aria-hidden="true"] > div')?.getAttribute("style"))
    .toContain("rgb(88, 51, 255)");
  fireEvent.keyDown(next, { key: " " });
  expect(next.getAttribute("aria-pressed")).toBe("true");
  fireEvent.click(first);
  expect(first.getAttribute("aria-pressed")).toBe("true");
  await act(async () => { await vi.advanceTimersByTimeAsync(350); });
  expect(api.set).toHaveBeenCalledTimes(1);
});

test("palette resets include hidden colours and preserve protected channels", async () => {
  const palette = paletteEffect();
  const protectedKey = palette.parameters[4].key;
  const parameters = palette.parameters.map((p) => ({
    ...p,
    resettable: p.key !== protectedKey,
  }));
  api.get.mockResolvedValue({ success: true, effects: [{ ...palette, parameters }] });
  api.set.mockResolvedValue({ success: true });
  await open();
  fireEvent.click(screen.getByRole("button", { name: "Color 15" }));
  fireEvent.click(screen.getByRole("button", { name: "Reset shader to defaults" }));
  await act(async () => { await vi.advanceTimersByTimeAsync(350); });
  expect(api.set).toHaveBeenCalledExactlyOnceWith("Game A", Object.fromEntries(
    parameters.filter((p) => p.resettable).map((p) => [p.key, null]),
  ));
  expect(screen.getByRole("button", { name: "Color 15" }).getAttribute("aria-pressed"))
    .toBe("true");
});

test("palette grouping respects category boundaries, intervening controls and missing metadata", async () => {
  const palette = paletteEffect();
  api.get.mockResolvedValue({ success: true, effects: [{
    ...palette,
    parameters: [
      ...palette.parameters.slice(0, 6),
      effect.parameters[1],
      ...palette.parameters.slice(6, 9),
      ...palette.parameters.slice(9, 12).map((p) => ({ ...p, category: "Other" })),
      ...palette.parameters.slice(12, 15).map(({ presentation: _, ...p }) => p),
    ],
  }] });
  await open();
  expect(screen.getAllByRole("button", { name: /^Color \d+$/ })).toHaveLength(2);
  expect(screen.getAllByRole("img", { name: "Colour preview" })).toHaveLength(3);
  expect(screen.getByRole("textbox", { name: "Amount" })).toBeTruthy();
  expect(screen.getByRole("textbox", { name: "Color 4 [1]" })).toBeTruthy();
  expect(api.set).not.toHaveBeenCalled();
});

test("declared colours use named channels and preview without quantizing or clamping saved values", async () => {
  const colour = colorEffect();
  api.get.mockResolvedValue({ success: true, effects: [colour] });
  api.set.mockResolvedValue({ success: true });
  await open();
  expect(screen.getByRole("img", { name: "Colour preview" })).toBeTruthy();
  expect(screen.getByText("Border colour")).toBeTruthy();
  expect(screen.getAllByText("Authored colour help")).toHaveLength(1);
  expect(
    (screen.getByRole("textbox", { name: "Red" }) as HTMLInputElement).value,
  ).toBe("0.123456789");
  expect(
    (screen.getByRole("textbox", { name: "Blue" }) as HTMLInputElement).value,
  ).toBe("1.5");
  expect(
    screen.getByText("Preview is limited to 0–1. Exact values are preserved."),
  ).toBeTruthy();
  expect(api.set).not.toHaveBeenCalled();
  fireEvent.change(screen.getByRole("slider", { name: "Green: 0.2" }), {
    target: { value: "0.7" },
  });
  const alpha = screen.getByRole("textbox", { name: "Alpha" });
  fireEvent.change(alpha, { target: { value: "0.456789123" } });
  fireEvent.blur(alpha);
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).toHaveBeenCalledExactlyOnceWith("Game A", {
    [colour.parameters[1].key]: 0.7,
    [colour.parameters[3].key]: 0.456789123,
  });
});

test("genuine vectors use axes while old metadata retains its exact labels", async () => {
  const vector = colorEffect();
  vector.parameters = vector.parameters.slice(0, 2).map((p) => ({
    ...p,
    presentation: { ...p.presentation, widget: "drag", count: 2 },
  }));
  api.get.mockResolvedValue({ success: true, effects: [vector] });
  await open();
  expect(screen.getByRole("textbox", { name: "X" })).toBeTruthy();
  expect(screen.getByRole("textbox", { name: "Y" })).toBeTruthy();
  expect(screen.queryByRole("img")).toBeNull();
  cleanup();
  api.get.mockResolvedValue({
    success: true,
    effects: [
      {
        ...vector,
        parameters: vector.parameters.map(({ presentation: _, ...p }) => p),
      },
    ],
  });
  await open();
  expect(
    screen.getByRole("textbox", { name: "Border colour [1]" }),
  ).toBeTruthy();
  expect(
    screen.getByRole("textbox", { name: "Border colour [2]" }),
  ).toBeTruthy();
});

test("explicit inputs stay numeric and empty enum entries keep their original indices", async () => {
  api.get.mockResolvedValue({
    success: true,
    effects: [
      {
        ...effect,
        parameters: [
          {
            ...effect.parameters[0],
            presentation: {
              widget: "input",
              group: "strength",
              label: "Strength",
              count: 1,
              component: 0,
            },
          },
          {
            ...effect.parameters[1],
            type: "int",
            value: 2,
            items: ["First", "", "Third"],
          },
        ],
      },
    ],
  });
  api.set.mockResolvedValue({ success: true });
  await open();
  expect(screen.queryByRole("slider")).toBeNull();
  expect(screen.getByRole("textbox", { name: "Strength" })).toBeTruthy();
  const choices = screen.getByRole("combobox", { name: "Amount" });
  expect((choices as HTMLSelectElement).value).toBe("2");
  expect(
    (within(choices).getByRole("option", { name: "1" }) as HTMLOptionElement)
      .value,
  ).toBe("1");
  fireEvent.change(choices, { target: { value: "1" } });
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).toHaveBeenCalledExactlyOnceWith("Game A", { amount: 1 });
});

test("reset respects authored restrictions and commits a focused draft before resetting", async () => {
  api.get.mockResolvedValue({
    success: true,
    effects: [
      {
        ...effect,
        parameters: [
          effect.parameters[0],
          { ...effect.parameters[1], resettable: false },
        ],
      },
    ],
  });
  api.set.mockResolvedValue({ success: true });
  await open();
  const input = screen.getByRole("textbox", { name: "Strength" });
  input.focus();
  fireEvent.change(input, { target: { value: "0.9" } });
  fireEvent.click(
    screen.getByRole("button", { name: "Reset shader to defaults" }),
  );
  await act(async () => {
    await vi.advanceTimersByTimeAsync(350);
  });
  expect(api.set).toHaveBeenCalledExactlyOnceWith("Game A", { strength: null });
});
test("reset is absent when every parameter forbids it", async () => {
  api.get.mockResolvedValue({
    success: true,
    effects: [
      {
        ...effect,
        parameters: effect.parameters.map((p) => ({ ...p, resettable: false })),
      },
    ],
  });
  await open();
  expect(
    screen.queryByRole("button", { name: "Reset shader to defaults" }),
  ).toBeNull();
});
