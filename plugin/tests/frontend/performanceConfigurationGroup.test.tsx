import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  SliderField: ({
    label,
    disabled,
    description,
  }: {
    label: React.ReactNode;
    disabled?: boolean;
    description?: React.ReactNode;
  }) => (
    <div>
      <button disabled={disabled}>{label}</button>
      {description}
    </div>
  ),
  ToggleField: ({
    label,
    checked,
    disabled,
    description,
    bottomSeparator,
    onChange,
  }: {
    label: React.ReactNode;
    checked: boolean;
    disabled?: boolean;
    description?: React.ReactNode;
    bottomSeparator?: string;
    onChange: (value: boolean) => void;
  }) => (
    <div>
      <button
        data-checked={String(checked)}
        data-bottom-separator={bottomSeparator ?? "default"}
        disabled={disabled}
        onClick={() => onChange(!checked)}
      >
        {label}
      </button>
      {description}
    </div>
  ),
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoRestartLabel: ({ label }: { label: string }) => label,
  MakoInlineTip: ({
    children,
    tone,
  }: {
    children: React.ReactNode;
    tone?: string;
  }) => (
    <div role="note" data-tone={tone}>
      {children}
    </div>
  ),
  MakoSettingRelationship: ({ children }: { children: React.ReactNode }) => (
    <div data-mako-setting-relationship="true">{children}</div>
  ),
  MakoSectionHeader: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { PerformanceConfigurationGroup } from "../../src/components/ConfigurationSectionGroups";
import { getDefaults } from "../../src/config/configSchema";

afterEach(cleanup);

describe("Performance Settings", () => {
  test("groups Ultra Performance, Flow Scale, and FP16 without duplicating the lighter model", () => {
    window.SP_REACT = React;
    const onConfigChange = vi.fn(async () => undefined);
    const onConfigUpdate = vi.fn(async () => undefined);

    render(
      <PerformanceConfigurationGroup
        config={{
          ...getDefaults(),
          ultra_performance: true,
          performance_mode: false,
        }}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    expect(screen.getByText("Performance Settings")).toBeTruthy();
    expect(
      screen
        .getByText("Ultra Performance (Restart)")
        .closest("button")
        ?.querySelector('svg[aria-hidden="true"]'),
    ).toBeTruthy();
    expect(screen.queryByText("Lighter FG Model")).toBeNull();
    expect(
      (screen.getByText("Flow Scale (75%)") as HTMLButtonElement).disabled,
    ).toBe(true);
    const allowFp16 = screen.getByText("Allow FP16 (Restart)");
    expect((allowFp16 as HTMLButtonElement).disabled).toBe(true);
    expect(allowFp16.getAttribute("data-checked")).toBe("true");
    expect(allowFp16.getAttribute("data-bottom-separator")).toBe("none");
    expect(
      screen.queryByText(/private frame-generation context live/),
    ).toBeNull();
    expect(
      screen.getByText(/Reduces MAKO's GPU workload on low-power devices/),
    ).toBeTruthy();
    expect(
      screen.getByText(/LS1 Performance when Scaling is enabled/),
    ).toBeTruthy();
    expect(
      screen.getByText(/performance across the active MAKO features/),
    ).toBeTruthy();
    const info = screen.getByRole("note");
    expect(info.getAttribute("data-tone")).toBe("info");
    expect(info.textContent).toContain(
      "Turning Ultra Performance on or off requires a game restart. Other compatible profile controls remain available after startup.",
    );

    fireEvent.click(screen.getByText("Ultra Performance (Restart)"));
    expect(onConfigChange).not.toHaveBeenCalled();
    expect(onConfigUpdate).toHaveBeenCalledWith({
      ultra_performance: false,
      flow_scale: 0.9,
      performance_mode: false,
      allow_fp16: true,
    });
  });
});
