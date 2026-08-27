import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  Field: ({
    label,
    description,
    children,
  }: {
    label: React.ReactNode;
    description?: React.ReactNode;
    children: React.ReactNode;
  }) => (
    <div>
      <span>{label}</span>
      {description}
      {children}
    </div>
  ),
  Dropdown: ({
    rgOptions,
    selectedOption,
    disabled,
    onChange,
  }: {
    rgOptions: { data: string; label: React.ReactNode }[];
    selectedOption: string;
    disabled?: boolean;
    onChange: (option: { data: string; label: React.ReactNode }) => void;
  }) => (
    <div>
      <button
        data-selected={selectedOption}
        disabled={disabled}
        onClick={() => onChange(rgOptions[1])}
      >
        Scaling Method
      </button>
      {rgOptions.map((option) => (
        <span key={option.data}>{option.label}</span>
      ))}
    </div>
  ),
  ToggleField: ({
    label,
    checked,
    disabled,
    description,
    onChange,
  }: {
    label: React.ReactNode;
    checked: boolean;
    disabled?: boolean;
    description?: React.ReactNode;
    onChange: (value: boolean) => void;
  }) => (
    <div>
      <button
        data-checked={String(checked)}
        disabled={disabled}
        onClick={() => onChange(!checked)}
      >
        {label}
      </button>
      {description}
    </div>
  ),
  SliderField: ({
    label,
    description,
    value,
    min,
    max,
    step = 1,
    notchCount,
    notchTicksVisible,
    disabled,
    onChange,
  }: {
    label: React.ReactNode;
    description?: React.ReactNode;
    value: number;
    min: number;
    max: number;
    step?: number;
    notchCount?: number;
    notchTicksVisible?: boolean;
    disabled?: boolean;
    onChange: (value: number) => void;
  }) => (
    <div>
      <button
        data-minimum={min}
        data-maximum={max}
        data-step={step}
        data-notch-count={notchCount ?? "none"}
        data-notch-ticks-visible={String(notchTicksVisible ?? false)}
        disabled={disabled}
        onClick={() => onChange(Math.min(max, value + step))}
      >
        {label}
      </button>
      {description}
    </div>
  ),
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { ScalingControl } from "../../src/components/ScalingControl";
import {
  SCALING_ENABLED,
  SCALING_FACTOR,
  SCALING_METHOD,
  SCALING_METHOD_LS1,
  SCALING_SHARPNESS,
  getDefaults,
} from "../../src/config/configSchema";

afterEach(cleanup);

describe("Scaling controls", () => {
  test("the master toggle hides and shows every dependent control", () => {
    window.SP_REACT = React;
    const onConfigChange = vi.fn(async () => undefined);

    const { rerender } = render(
      <ScalingControl config={getDefaults()} onConfigChange={onConfigChange} />,
    );

    const enabled = screen.getByText("Enable Scaling");
    expect(enabled.getAttribute("data-checked")).toBe("false");
    expect(screen.queryByText("Scale Factor (1.5x)")).toBeNull();
    expect(screen.queryByText("Scaling Sharpness (50%)")).toBeNull();
    expect(
      screen.queryByRole("button", { name: "Scaling Method" }),
    ).toBeNull();
    expect(screen.queryByText("MAKO (Open)")).toBeNull();

    fireEvent.click(enabled);
    expect(onConfigChange).toHaveBeenCalledWith(SCALING_ENABLED, true);

    rerender(
      <ScalingControl
        config={{ ...getDefaults(), scaling_enabled: true }}
        onConfigChange={onConfigChange}
      />,
    );

    const factor = screen.getByText("Scale Factor (1.5x)");
    const sharpness = screen.getByText("Scaling Sharpness (50%)");
    const method = screen.getByRole("button", { name: "Scaling Method" });
    expect((factor as HTMLButtonElement).disabled).toBe(false);
    expect((sharpness as HTMLButtonElement).disabled).toBe(false);
    expect((method as HTMLButtonElement).disabled).toBe(false);
    expect(factor.getAttribute("data-minimum")).toBe("1");
    expect(factor.getAttribute("data-maximum")).toBe("2");
    expect(factor.getAttribute("data-step")).toBe("0.1");
    expect(factor.getAttribute("data-notch-count")).toBe("11");
    expect(factor.getAttribute("data-notch-ticks-visible")).toBe("true");
    expect(screen.getByText("MAKO (Open)")).toBeTruthy();
    expect(screen.getByText("LS1 Quality")).toBeTruthy();
    expect(screen.getByText("LS1 Performance")).toBeTruthy();
    expect(
      screen.getByText(
        "Enable Scaling before launching a game so MAKO Decky can provision its Gamescope presentation path, then select an in-game resolution below the display resolution. In that session, the toggle, method, factor, and sharpness apply through swapchain recreation; a brief flicker is normal. Scaling can run alone or before Frame Generation.",
      ),
    ).toBeTruthy();
    expect(
      screen.getByText(
        "MAKO is the built-in open single-pass scaler and does not need Lossless.dll. LS1 Quality uses Lossless Scaling's proprietary full neural network; LS1 Performance uses its lower-cost network. If LS1 cannot start, MAKO takes over for that swapchain. Changing the method recreates the swapchain.",
      ),
    ).toBeTruthy();
  });

  test("writes only the selected scaling field when combined with frame generation", () => {
    window.SP_REACT = React;
    const onConfigChange = vi.fn(async () => undefined);

    render(
      <ScalingControl
        config={{
          ...getDefaults(),
          scaling_enabled: true,
          frame_generation_enabled: true,
          adaptive: true,
        }}
        onConfigChange={onConfigChange}
      />,
    );

    fireEvent.click(screen.getByText("Scale Factor (1.5x)"));
    fireEvent.click(screen.getByText("Scaling Sharpness (50%)"));
    fireEvent.click(screen.getByRole("button", { name: "Scaling Method" }));

    expect(onConfigChange.mock.calls).toEqual([
      [SCALING_FACTOR, 1.6],
      [SCALING_SHARPNESS, 0.51],
      [SCALING_METHOD, SCALING_METHOD_LS1],
    ]);
    expect(onConfigChange).not.toHaveBeenCalledWith(
      "frame_generation_enabled",
      expect.anything(),
    );
    expect(onConfigChange).not.toHaveBeenCalledWith(
      "adaptive",
      expect.anything(),
    );
  });

  test("locks the full group until the installed Renderer is current", () => {
    window.SP_REACT = React;

    render(
      <ScalingControl
        config={{ ...getDefaults(), scaling_enabled: true }}
        disabled
        onConfigChange={vi.fn(async () => undefined)}
      />,
    );

    expect(
      (screen.getByText("Enable Scaling") as HTMLButtonElement)
        .disabled,
    ).toBe(true);
    expect(
      (screen.getByText("Scale Factor (1.5x)") as HTMLButtonElement)
        .disabled,
    ).toBe(true);
    expect(
      (
        screen.getByText(
          "Scaling Sharpness (50%)",
        ) as HTMLButtonElement
      ).disabled,
    ).toBe(true);
  });
});
