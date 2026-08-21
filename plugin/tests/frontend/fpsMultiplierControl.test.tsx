import React from "react";
import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  ToggleField: ({
    label,
    checked,
  }: {
    label: React.ReactNode;
    checked: boolean;
  }) => <div data-checked={String(checked)}>{label}</div>,
  SliderField: ({ label }: { label: React.ReactNode }) => <div>{label}</div>,
  Focusable: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  DialogButton: ({ children }: { children: React.ReactNode }) => (
    <button>{children}</button>
  ),
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoSectionHeader: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  makoDangerTextColor: "#d08aa0",
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
});
