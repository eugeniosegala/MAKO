import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  DropdownItem: ({ label, selectedOption, rgOptions, onChange }: any) => (
    <select
      aria-label={label}
      value={selectedOption}
      onChange={(e) => onChange({ data: Number(e.target.value) })}
    >
      {rgOptions.map((option: any) => (
        <option key={option.data} value={option.data}>
          {option.label}
        </option>
      ))}
    </select>
  ),
  SliderField: ({
    label,
    disabled,
    description,
    onChange,
  }: {
    label: React.ReactNode;
    disabled?: boolean;
    description?: React.ReactNode;
    onChange?: (value: number) => void;
  }) => (
    <div>
      <button disabled={disabled} onClick={() => onChange?.(0.85)}>{label}</button>
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

import { PerformanceConfigurationGroup } from "../../src/components/settings/PerformanceConfigurationGroup";
import { getDefaults } from "../../src/config/configSchema";

afterEach(cleanup);

describe("Performance Settings", () => {
  test("headroom is opt-in, presets are bounded, and enabling it clears competing recovery", () => {
    window.SP_REACT = React;
    const onConfigUpdate = vi.fn(async () => undefined);
    const onConfigChange = vi.fn(async () => undefined);
    const config = getDefaults();
    const view = render(
      <PerformanceConfigurationGroup
        config={config}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );
    expect(
      screen.queryByRole("combobox", { name: "Reserved GPU capacity" }),
    ).toBeNull();
    fireEvent.click(screen.getByText("GPU Headroom"));
    expect(onConfigUpdate).toHaveBeenCalledWith({
      gpu_headroom_percent: 10,
      dynamic_cadence_recovery: false,
    });
    view.rerender(
      <PerformanceConfigurationGroup
        config={{ ...config, gpu_headroom_percent: 10 }}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );
    const options = screen.getByRole("combobox", {
      name: "Reserved GPU capacity",
    });
    expect(
      [...options.querySelectorAll("option")].map((o) => o.textContent),
    ).toEqual(["5%", "10%", "15%", "20%"]);
    fireEvent.change(options, { target: { value: "20" } });
    expect(onConfigUpdate).toHaveBeenLastCalledWith({
      gpu_headroom_percent: 20,
      dynamic_cadence_recovery: false,
    });
    fireEvent.click(screen.getByText("GPU Headroom"));
    expect(onConfigUpdate).toHaveBeenLastCalledWith({
      gpu_headroom_percent: 0,
    });
  });

  test("MAKO FG exposes saved Flow Scale outside Ultra Performance", () => {
    window.SP_REACT = React;
    const onConfigChange = vi.fn(async () => undefined);
    render(<PerformanceConfigurationGroup config={{...getDefaults(), frame_generation_model: "mako-fg", ultra_performance: false, flow_scale: 0.75}} onConfigChange={onConfigChange} onConfigUpdate={vi.fn(async () => undefined)} />);
    const slider = screen.getByText("Flow Scale (75%)") as HTMLButtonElement;
    expect(slider.disabled).toBe(false);
    fireEvent.click(slider);
    expect(onConfigChange).toHaveBeenCalledWith("flow_scale", 0.85);
  });

  test("MAKO FG keeps Ultra Performance available and applies its Flow Scale", () => {
    window.SP_REACT = React;
    const onConfigUpdate = vi.fn(async () => undefined);
    render(<PerformanceConfigurationGroup config={{...getDefaults(), frame_generation_model: "mako-fg", ultra_performance: true, flow_scale: 0.75}} onConfigChange={vi.fn(async () => undefined)} onConfigUpdate={onConfigUpdate} />);
    const ultra = screen.getByText("Ultra Performance (Restart)").closest("button") as HTMLButtonElement;
    expect(ultra.disabled).toBe(false);
    expect(ultra.getAttribute("data-checked")).toBe("true");
    expect((screen.getByText("Flow Scale (60%)") as HTMLButtonElement).disabled).toBe(true);
    fireEvent.click(ultra);
    expect(onConfigUpdate).toHaveBeenCalledWith({
      ultra_performance: false,
      flow_scale: 0.8,
      performance_mode: false,
    });
  });

  test("groups Ultra Performance, Flow Scale, and FP16 without duplicating the lighter model", () => {
    window.SP_REACT = React;
    const onConfigChange = vi.fn(async () => undefined);
    const onConfigUpdate = vi.fn(async () => undefined);

    render(
      <PerformanceConfigurationGroup
        config={{
          ...getDefaults(),
          frame_generation_model: "lossless-scaling",
          ultra_performance: true,
          performance_mode: false,
          allow_fp16: false,
        }}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    expect(screen.queryByText("Performance Settings")).toBeNull();
    const overlay = screen.getByText("Disable Steam Overlay (Restart)");
    expect(overlay.getAttribute("data-checked")).toBe("true");
    expect(screen.getByText(/may affect Steam Input/)).toBeTruthy();
    fireEvent.click(overlay);
    expect(onConfigChange).toHaveBeenCalledWith("disable_steam_overlay", false);
    onConfigChange.mockClear();
    expect(
      screen
        .getByText("Ultra Performance (Restart)")
        .closest("button")
        ?.querySelector('svg[aria-hidden="true"]'),
    ).toBeTruthy();
    expect(screen.queryByText("Lighter FG Model")).toBeNull();
    expect(
      (screen.getByText("Flow Scale (70%)") as HTMLButtonElement).disabled,
    ).toBe(true);
    const allowFp16 = screen.getByText("Allow FP16 (Restart)");
    expect((allowFp16 as HTMLButtonElement).disabled).toBe(false);
    expect(allowFp16.getAttribute("data-checked")).toBe("false");
    expect(screen.getByText(/LS1 always uses FP32/)).toBeTruthy();
    fireEvent.click(allowFp16);
    expect(onConfigChange).toHaveBeenCalledWith("allow_fp16", true);
    expect(allowFp16.getAttribute("data-bottom-separator")).toBe("none");
    expect(
      screen.queryByText(/private frame-generation context live/),
    ).toBeNull();
    expect(
      screen.getByText(/Cuts GPU and memory use on low-power devices/),
    ).toBeTruthy();
    expect(
      screen.getByText(/Locks Flow Scale at 60% for MAKO FG and 70% for LSFG/),
    ).toBeTruthy();
    expect(
      screen.getByText(/MAKO Scaler when Scaling is enabled/),
    ).toBeTruthy();
    expect(screen.queryByRole("note")).toBeNull();

    onConfigChange.mockClear();
    fireEvent.click(screen.getByText("Ultra Performance (Restart)"));
    expect(onConfigChange).not.toHaveBeenCalled();
    expect(onConfigUpdate).toHaveBeenCalledWith({
      ultra_performance: false,
      flow_scale: 0.8,
      performance_mode: false,
    });
  });
});
