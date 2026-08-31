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
    <div data-field-label={typeof label === "string" ? label : undefined}>
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
        onClick={() => onChange(rgOptions[2])}
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
  SCALING_METHOD_MAKO,
  SCALING_METHOD_NATIVE,
  SCALING_SHARPNESS,
  SCALING_SUPERSAMPLING,
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

    const enabled = screen.getByRole("button", {
      name: "Enable Scaling (Restart) Experimental",
    });
    expect(
      screen
        .getByText("Experimental")
        .getAttribute("data-mako-experimental-badge"),
    ).toBe("true");
    expect(enabled.getAttribute("data-checked")).toBe("false");
    expect(screen.queryByText("Scale Factor (1.5x)")).toBeNull();
    expect(screen.queryByText("Quality Supersampling")).toBeNull();
    expect(screen.queryByText("Scaling Sharpness (80%)")).toBeNull();
    expect(screen.queryByRole("button", { name: "Scaling Method" })).toBeNull();
    expect(screen.queryByText("MAKO Scaler")).toBeNull();

    fireEvent.click(enabled);
    expect(onConfigChange).toHaveBeenCalledWith(SCALING_ENABLED, true);

    rerender(
      <ScalingControl
        config={{
          ...getDefaults(),
          scaling_enabled: true,
          scaling_method: SCALING_METHOD_MAKO,
        }}
        onConfigChange={onConfigChange}
      />,
    );

    const factor = screen.getByText("Scale Factor (1.5x)");
    expect(screen.getByText("Quality Supersampling")).toBeTruthy();
    const sharpness = screen.getByText("Scaling Sharpness (80%)");
    const method = screen.getByRole("button", { name: "Scaling Method" });
    expect((factor as HTMLButtonElement).disabled).toBe(false);
    expect((sharpness as HTMLButtonElement).disabled).toBe(false);
    expect((method as HTMLButtonElement).disabled).toBe(false);
    expect(factor.getAttribute("data-minimum")).toBe("1");
    expect(factor.getAttribute("data-maximum")).toBe("2");
    expect(factor.getAttribute("data-step")).toBe("0.1");
    expect(factor.getAttribute("data-notch-count")).toBe("11");
    expect(factor.getAttribute("data-notch-ticks-visible")).toBe("true");
    expect(screen.getByText("MAKO Scaler")).toBeTruthy();
    expect(screen.getByText("Native Resolution")).toBeTruthy();
    expect(screen.getByText("LS1 Quality")).toBeTruthy();
    expect(screen.getByText("LS1 Performance")).toBeTruthy();
    expect(
      screen.getByText(
        "Sets the output-to-input size ratio for every method, including Native Resolution. Higher values render fewer source pixels.",
      ),
    ).toBeTruthy();
    expect(screen.queryByText(/guarded game-owned recreation/)).toBeNull();
    expect(
      screen.getByText(
        "For MAKO, applies this 0–100% multiplier to its 2x sharpening baseline. For LS1, selects one of five learned sharpness variants.",
      ),
    ).toBeTruthy();
    expect(screen.queryByText(/private scaler rebuild/)).toBeNull();
    expect(
      screen.getByText(
        "Enable before starting the game. When off, scaling is fully disabled. Supports Lossless Scaling models and MAKO Scaler.",
      ),
    ).toBeTruthy();
    expect(
      screen
        .getByText(
          "Leave Scaling off when you do not need it, as it consumes resources. Using it with Frame Generation may affect performance; try different performance settings or a lower in-game resolution.",
        )
        .closest('[data-tone="warning"]'),
    ).toBeTruthy();
    expect(
      screen.getByText(
        "Choose the scaling model. You can change it while the game is running.",
      ),
    ).toBeTruthy();
    expect(
      screen.getByText(/How scaling works:/).closest('[data-tone="info"]'),
    ).toBeTruthy();
    expect(screen.getByText(/Steam Machine: 3840 × 2160/)).toBeTruthy();
    expect(screen.queryByText(/\/ 4K/)).toBeNull();
    expect(screen.getByText(/480p, 720p, or more/)).toBeTruthy();
    expect(screen.queryByText(/higher on Steam Machine/)).toBeNull();
    expect(screen.getByText(/2x doubles your resolution/)).toBeTruthy();
    expect(
      screen.getByText(
        /Reducing the resolution of the game and scaling it back can substantially increase performance/,
      ),
    ).toBeTruthy();
    expect(screen.queryByText(/scale from 2K to 4K/)).toBeNull();

    rerender(
      <ScalingControl
        config={{
          ...getDefaults(),
          scaling_enabled: true,
          scaling_method: SCALING_METHOD_NATIVE,
        }}
        onConfigChange={onConfigChange}
      />,
    );
    expect(screen.getByRole("button", { name: "Scaling Method" })).toBeTruthy();
    expect(screen.getByText("Scale Factor (1.5x)")).toBeTruthy();
    expect(screen.queryByText("Scaling Sharpness (80%)")).toBeNull();
  });

  test("limits the factor to live display geometry until supersampling is enabled", () => {
    window.SP_REACT = React;
    const onConfigChange = vi.fn(async () => undefined);
    const ordinaryConfig = {
      ...getDefaults(),
      scaling_enabled: true,
      scaling_factor: 1.8,
      scaling_supersampling: false,
    };
    const { rerender } = render(
      <ScalingControl
        config={ordinaryConfig}
        runtimeFactorCeiling={4 / 3}
        onConfigChange={onConfigChange}
      />,
    );

    const limited = screen.getByText("Scale Factor (1.3x display limit)");
    expect(limited.getAttribute("data-maximum")).toBe("1.3");
    expect(limited.getAttribute("data-notch-count")).toBe("4");
    fireEvent.click(
      screen.getByRole("button", {
        name: "Quality Supersampling",
      }),
    );
    expect(onConfigChange).toHaveBeenCalledWith(SCALING_SUPERSAMPLING, true);

    rerender(
      <ScalingControl
        config={{ ...ordinaryConfig, scaling_supersampling: true }}
        runtimeFactorCeiling={4 / 3}
        onConfigChange={onConfigChange}
      />,
    );
    const expanded = screen.getByText("Scale Factor (1.8x)");
    expect(expanded.getAttribute("data-maximum")).toBe("2");
    expect(
      screen.getByText(
        "Supersampling is enabled. MAKO can render above the display target for a sharper downsampled image.",
      ),
    ).toBeTruthy();
  });

  test("disables a factor with no display headroom and explains the alternatives", () => {
    window.SP_REACT = React;

    render(
      <ScalingControl
        config={{
          ...getDefaults(),
          scaling_enabled: true,
          scaling_factor: 1.5,
          scaling_supersampling: false,
        }}
        runtimeFactorCeiling={1}
        onConfigChange={vi.fn(async () => undefined)}
      />,
    );

    const factor = screen.getByText("Scale Factor (1.0x display limit)");
    expect((factor as HTMLButtonElement).disabled).toBe(true);
    expect(factor.getAttribute("data-notch-count")).toBe("3");
    expect(
      screen.getByText(
        "This resolution already fills the display. Lower the in-game resolution or enable Quality Supersampling.",
      ),
    ).toBeTruthy();
  });

  test("keeps pre-game choices saved and only clamps their live presentation", () => {
    window.SP_REACT = React;
    const onConfigChange = vi.fn(async () => undefined);
    const config = {
      ...getDefaults(),
      scaling_enabled: true,
      scaling_factor: 1.8,
      scaling_supersampling: false,
    };
    const { rerender } = render(
      <ScalingControl config={config} onConfigChange={onConfigChange} />,
    );

    expect(screen.getByText("Scale Factor (1.8x)")).toBeTruthy();
    expect(onConfigChange).not.toHaveBeenCalled();

    rerender(
      <ScalingControl
        config={config}
        runtimeFactorCeiling={1.2}
        onConfigChange={onConfigChange}
      />,
    );
    expect(screen.getByText("Scale Factor (1.2x display limit)")).toBeTruthy();
    expect(onConfigChange).not.toHaveBeenCalled();
  });

  test("shows Ultra Performance's effective scaler as locked", () => {
    window.SP_REACT = React;
    render(
      <ScalingControl
        config={{
          ...getDefaults(),
          scaling_enabled: true,
          scaling_method: SCALING_METHOD_MAKO,
          ultra_performance: true,
        }}
        onConfigChange={vi.fn(async () => undefined)}
      />,
    );

    const method = screen.getByRole("button", { name: "Scaling Method" });
    expect(method.getAttribute("data-selected")).toBe("ls1-performance");
    expect((method as HTMLButtonElement).disabled).toBe(true);
  });

  test("replaces scaling guidance with the running-surface warning", () => {
    window.SP_REACT = React;

    render(
      <ScalingControl
        config={{
          ...getDefaults(),
          scaling_enabled: true,
          scaling_method: SCALING_METHOD_MAKO,
        }}
        runtimeInactiveReason="gamescope-wsi-surface-unproven"
        onConfigChange={vi.fn(async () => undefined)}
      />,
    );

    const warning = screen.getByText(
      "Scaling is unavailable for this running surface because Gamescope WSI did not create it. Model and factor changes are saved for the next supported surface; Frame Generation continues at native resolution.",
    );
    expect(warning.closest('[data-tone="warning"]')).toBeTruthy();
    expect(warning.closest('[data-field-label="Scaling Method"]')).toBeTruthy();
    expect(screen.queryByText(/How scaling works:/)).toBeNull();
  });

  test("writes only the selected scaling field when combined with frame generation", () => {
    window.SP_REACT = React;
    const onConfigChange = vi.fn(async () => undefined);

    render(
      <ScalingControl
        config={{
          ...getDefaults(),
          scaling_enabled: true,
          scaling_method: SCALING_METHOD_MAKO,
          scaling_factor: 1.5,
          scaling_sharpness: 0.5,
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
        config={{
          ...getDefaults(),
          scaling_enabled: true,
          scaling_method: SCALING_METHOD_MAKO,
        }}
        disabled
        onConfigChange={vi.fn(async () => undefined)}
      />,
    );

    expect(
      (
        screen.getByRole("button", {
          name: "Enable Scaling (Restart) Experimental",
        }) as HTMLButtonElement
      ).disabled,
    ).toBe(true);
    expect(
      (screen.getByText("Scale Factor (1.5x)") as HTMLButtonElement).disabled,
    ).toBe(true);
    expect(
      (screen.getByText("Scaling Sharpness (80%)") as HTMLButtonElement)
        .disabled,
    ).toBe(true);
  });
});
