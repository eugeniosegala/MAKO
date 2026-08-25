import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
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
    step,
    notchCount,
    notchTicksVisible,
    disabled,
    onChange,
  }: {
    label: React.ReactNode;
    description?: React.ReactNode;
    value: number;
    min?: number;
    max?: number;
    step?: number;
    notchCount?: number;
    notchTicksVisible?: boolean;
    disabled?: boolean;
    onChange: (value: number) => void;
  }) => (
    <div>
      {description}
      <button
        data-minimum={min}
        data-maximum={max}
        data-step={step}
        data-notch-count={notchCount ?? "none"}
        data-notch-ticks-visible={String(notchTicksVisible ?? false)}
        disabled={disabled}
        onClick={() => onChange(value === 0 ? 30 : value + 1)}
      >
        {label}
      </button>
    </div>
  ),
  Field: ({
    label,
    description,
    children,
  }: {
    label: React.ReactNode;
    description?: React.ReactNode;
    children?: React.ReactNode;
  }) => (
    <div>
      {label}
      {description}
      {children}
    </div>
  ),
  Dropdown: ({
    rgOptions,
    selectedOption,
    onChange,
  }: {
    rgOptions: Array<{ data: number; label: React.ReactNode }>;
    selectedOption: number;
    onChange: (option: { data: number; label: React.ReactNode }) => void;
  }) => (
    <button
      data-testid="cadence-probe-interval-dropdown"
      data-options={JSON.stringify(rgOptions.map((option) => option.data))}
      onClick={() => onChange(rgOptions[0])}
    >
      {selectedOption}s
    </button>
  ),
  TextField: ({ label }: { label: React.ReactNode }) => <div>{label}</div>,
  ButtonItem: ({
    children,
    onClick,
  }: {
    children: React.ReactNode;
    onClick: () => void;
  }) => <button onClick={onClick}>{children}</button>,
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoInlineWarning: ({
    children,
    tone,
  }: {
    children: React.ReactNode;
    tone?: string;
  }) => <div data-tone={tone}>{children}</div>,
  MakoSettingRelationship: ({
    children,
  }: {
    children: React.ReactNode;
  }) => <div data-mako-setting-relationship="true">{children}</div>,
  MakoSectionHeader: ({
    children,
    topMargin,
  }: {
    children: React.ReactNode;
    topMargin?: React.CSSProperties["marginTop"];
  }) => <div data-top-margin={topMargin}>{children}</div>,
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (
    _key: string,
    fallback: string,
    replacements: Record<string, string | number> = {},
  ) =>
    Object.entries(replacements).reduce(
      (text, [name, value]) =>
        text.split(`{${name}}`).join(String(value)),
      fallback,
    ),
}));

import { ConfigurationSection } from "../../src/components/ConfigurationSection";
import { getDefaults } from "../../src/config/configSchema";

afterEach(cleanup);

describe("External Tools controls", () => {
  beforeEach(() => {
    window.SP_REACT = React;
    localStorage.clear();
  });

  test("uses consistent spacing before configuration section headers", () => {
    render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(
      screen
        .getByText("Advanced Rendering Settings")
        .getAttribute("data-top-margin"),
    ).toBe("26px");
    expect(
      screen
        .getByText("Compatibility Settings")
        .getAttribute("data-top-margin"),
    ).toBe("26px");
  });

  test("shows Ultra Performance's effective flow and FP16 values as locked", () => {
    render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          ultra_performance: true,
          flow_scale: 0.9,
          allow_fp16: false,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    const flowScale = screen.getByText("Flow Scale (Restart) (75%)");
    const allowFp16 = screen.getByText("Allow FP16");
    expect((flowScale as HTMLButtonElement).disabled).toBe(true);
    expect((allowFp16 as HTMLButtonElement).disabled).toBe(true);
    expect(allowFp16.getAttribute("data-checked")).toBe("true");
  });

  test("explains setting ownership with quiet relationship hints", () => {
    const { container } = render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          adaptive: true,
          adaptive_auto_base_fps_cap: true,
          base_fps_cap: 35,
          target_fps: 120,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(
      screen.getByText(
        "Controlled by Steady Base Cap (60 FPS). Your manual value remains saved.",
      ),
    ).toBeTruthy();

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_WorkaroundsCollapseButton_Container button",
    );
    fireEvent.click(collapseButton!);

    expect(
      screen.getByText(
        "Turning this on disables Steady Base Cap and Base FPS Cap. Changing either cap later turns Recovery off.",
      ),
    ).toBeTruthy();
    expect(
      container.querySelectorAll('[data-mako-setting-relationship="true"]')
        .length,
    ).toBe(2);
  });

  test("uses the orange warning treatment for Experimental Gamescope WSI", () => {
    const { container } = render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_WorkaroundsCollapseButton_Container button",
    );
    fireEvent.click(collapseButton!);

    const warning = container.querySelector('[data-tone="warning"]');
    expect(warning?.textContent).toContain(
      "Keep it off if not needed. It may reduce performance or interfere with frame generation.",
    );
  });

  test("starts collapsed and remembers when it is expanded", () => {
    const { container } = render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(screen.getByText("External Tools")).toBeTruthy();
    expect(screen.queryByText("Enable MangoHud (Restart)")).toBeNull();
    expect(
      screen.queryByText("Enable vkBasalt (Experimental, Restart)"),
    ).toBeNull();

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_ExternalToolsCollapseButton_Container button",
    );
    expect(collapseButton).toBeTruthy();
    fireEvent.click(collapseButton!);

    expect(screen.getByText("Enable MangoHud (Restart)")).toBeTruthy();
    expect(
      screen.getByText("Enable vkBasalt (Experimental, Restart)"),
    ).toBeTruthy();
    expect(localStorage.getItem("mako-external-tools-collapsed")).toBe("false");
  });

  test("enables global cadence recovery atomically from Compatibility", () => {
    const onConfigUpdate = vi.fn(async () => undefined);
    const { container } = render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          adaptive: false,
          adaptive_auto_base_fps_cap: true,
          base_fps_cap: 30,
          dynamic_cadence_recovery: false,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_WorkaroundsCollapseButton_Container button",
    );
    fireEvent.click(collapseButton!);
    fireEvent.click(screen.getByText("Dynamic Cadence Recovery"));

    expect(onConfigUpdate).toHaveBeenCalledWith({
      dynamic_cadence_recovery: true,
      adaptive_auto_base_fps_cap: false,
      base_fps_cap: 0,
    });
  });

  test("reveals discrete live probe intervals only while Recovery is enabled", () => {
    const onConfigChange = vi.fn(async () => undefined);
    const { container, rerender } = render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          dynamic_cadence_recovery: false,
          dynamic_cadence_probe_interval_seconds: 2,
        }}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_WorkaroundsCollapseButton_Container button",
    );
    fireEvent.click(collapseButton!);
    expect(screen.queryByText("Cadence Probe Interval")).toBeNull();

    rerender(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          dynamic_cadence_recovery: true,
          dynamic_cadence_probe_interval_seconds: 2,
        }}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(screen.getByText("Cadence Probe Interval")).toBeTruthy();
    const dropdown = screen.getByTestId("cadence-probe-interval-dropdown");
    expect(JSON.parse(dropdown.getAttribute("data-options") || "[]")).toEqual([
      0.1,
      0.2,
      0.25,
      0.5,
      0.75,
      1,
      1.5,
      2,
      3,
    ]);
    expect(dropdown.textContent).toBe("2s");
    expect(
      screen.getByText(/How often Recovery tests the native frame rate/).style
        .paddingBottom,
    ).toBe("6px");
    fireEvent.click(dropdown);
    expect(onConfigChange).toHaveBeenCalledWith(
      "dynamic_cadence_probe_interval_seconds",
      0.1,
    );

    rerender(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          dynamic_cadence_recovery: true,
          dynamic_cadence_probe_interval_seconds: 0.25,
        }}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );
    expect(
      screen.getByTestId("cadence-probe-interval-dropdown").textContent,
    ).toBe("0.25s");
  });

  test("preserves a valid non-preset cadence interval in the selector", () => {
    const { container } = render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          dynamic_cadence_recovery: true,
          dynamic_cadence_probe_interval_seconds: 1.25,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_WorkaroundsCollapseButton_Container button",
    );
    fireEvent.click(collapseButton!);

    const dropdown = screen.getByTestId("cadence-probe-interval-dropdown");
    expect(dropdown.textContent).toBe("1.25s");
    expect(JSON.parse(dropdown.getAttribute("data-options") || "[]")).toContain(
      1.25,
    );
  });

  test("enabling the Base FPS Cap turns cadence recovery off", () => {
    const onConfigUpdate = vi.fn(async () => undefined);
    render(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          adaptive: false,
          adaptive_auto_base_fps_cap: false,
          base_fps_cap: 0,
          dynamic_cadence_recovery: true,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    fireEvent.click(screen.getByText("Base FPS Cap (Off)"));

    expect(onConfigUpdate).toHaveBeenCalledWith({
      base_fps_cap: 30,
      dynamic_cadence_recovery: false,
    });
  });

  test("offers manual Base FPS caps through 120 FPS", () => {
    render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(
      screen.getByText("Base FPS Cap (Off)").getAttribute("data-maximum"),
    ).toBe("120");
  });

  test("enables and configures the per-profile refresh-rate guard", () => {
    const onConfigChange = vi.fn(async () => undefined);
    const { rerender } = render(
      <ConfigurationSection
        config={getDefaults()}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    fireEvent.click(
      screen.getByText("Auto-disable Frame Generation by Refresh Rate"),
    );
    expect(onConfigChange).toHaveBeenCalledWith(
      "frame_generation_refresh_threshold",
      60,
    );

    rerender(
      <ConfigurationSection
        config={{
          ...getDefaults(),
          frame_generation_refresh_threshold: 130,
        }}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );
    fireEvent.click(screen.getByText("Refresh Rate Threshold (130 Hz)"));
    expect(onConfigChange).toHaveBeenLastCalledWith(
      "frame_generation_refresh_threshold",
      131,
    );
  });
});
