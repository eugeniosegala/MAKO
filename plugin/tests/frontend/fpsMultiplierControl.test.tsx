import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  ToggleField: ({
    label,
    checked,
    disabled,
    onChange,
  }: {
    label: React.ReactNode;
    checked: boolean;
    disabled?: boolean;
    onChange: (value: boolean) => void;
  }) => (
    <button
      data-checked={String(checked)}
      disabled={disabled}
      onClick={() => onChange(!checked)}
    >
      {label}
    </button>
  ),
  SliderField: ({ label }: { label: React.ReactNode }) => <div>{label}</div>,
  Focusable: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  DialogButton: ({ children }: { children: React.ReactNode }) => (
    <button>{children}</button>
  ),
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoInlineWarning: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  MakoSectionHeader: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  makoDialogButtonStyle: () => ({}),
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { FpsMultiplierControl } from "../../src/components/FpsMultiplierControl";
import { getDefaults } from "../../src/config/configSchema";

afterEach(cleanup);

describe("Frame Generation Mode controls", () => {
  test("shows the Fractional preset only while Adaptive is enabled", () => {
    window.SP_REACT = React;
    const onConfigChange = vi.fn(async () => undefined);
    const onConfigUpdate = vi.fn(async () => undefined);
    const defaults = getDefaults();
    const { rerender } = render(
      <FpsMultiplierControl
        config={{ ...defaults, adaptive: false }}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    expect(screen.queryByText("Fractional Adaptive (Preset)")).toBeNull();

    rerender(
      <FpsMultiplierControl
        config={{ ...defaults, adaptive: true }}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    expect(screen.getByText("Fractional Adaptive (Preset)")).toBeTruthy();
  });

  test("uses the canonical steady-cap default for a partial legacy config", () => {
    window.SP_REACT = React;
    const defaults = getDefaults();
    const {
      adaptive_auto_base_fps_cap: _missingSteadyCap,
      ...legacyPartialConfig
    } = { ...defaults, adaptive: true };

    render(
      <FpsMultiplierControl
        config={legacyPartialConfig as ReturnType<typeof getDefaults>}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(
      screen.getByText("Steady Base Cap (45 FPS)").getAttribute("data-checked"),
    ).toBe("true");
  });

  test("lets incompatible Adaptive presets replace cadence recovery", () => {
    window.SP_REACT = React;
    const defaults = getDefaults();
    const onConfigUpdate = vi.fn(async () => undefined);

    render(
      <FpsMultiplierControl
        config={{
          ...defaults,
          adaptive: true,
          adaptive_auto_base_fps_cap: false,
          dynamic_cadence_recovery: true,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    expect(screen.queryByText("Dynamic Cadence Recovery")).toBeNull();
    const fractional = screen.getByText("Fractional Adaptive (Preset)");
    const steady = screen.getByText("Steady Base Cap (45 FPS)");
    expect((fractional as HTMLButtonElement).disabled).toBe(false);
    expect(fractional.getAttribute("data-checked")).toBe("false");
    expect((steady as HTMLButtonElement).disabled).toBe(false);

    fireEvent.click(fractional);
    expect(onConfigUpdate).toHaveBeenCalledWith({
      frame_generation_enabled: true,
      adaptive: true,
      adaptive_auto_base_fps_cap: false,
      dynamic_cadence_recovery: false,
    });

    fireEvent.click(steady);
    expect(onConfigUpdate).toHaveBeenCalledWith({
      adaptive_auto_base_fps_cap: true,
      dynamic_cadence_recovery: false,
    });
  });
});
