import React from "react";
import {
  cleanup,
  fireEvent,
  render,
  screen,
  within,
} from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  Dropdown: ({
    rgOptions,
    selectedOption,
    onChange,
  }: {
    rgOptions: Array<{ data: string; label: React.ReactNode }>;
    selectedOption: string;
    onChange: (option: { data: string; label: React.ReactNode }) => void;
  }) => (
    <button
      data-testid="real-frame-priority-dropdown"
      data-options={JSON.stringify(rgOptions.map((option) => option.data))}
      data-labels={JSON.stringify(rgOptions.map((option) => option.label))}
      onClick={() => onChange(rgOptions[2])}
    >
      {rgOptions.find((option) => option.data === selectedOption)?.label}
    </button>
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
      <span>{label}</span>
      <span>{description}</span>
      {children}
    </div>
  ),
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
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
  SliderField: ({
    label,
    description,
    value,
    min,
    max,
    step,
    validValues,
    notchCount,
    notchTicksVisible,
    onChange,
  }: {
    label: React.ReactNode;
    description?: React.ReactNode;
    value: number;
    min?: number;
    max?: number;
    step?: number;
    validValues?: "steps" | "range" | ((value: number) => boolean);
    notchCount?: number;
    notchTicksVisible?: boolean;
    onChange?: (value: number) => void;
  }) => (
    <div
      data-slider-field="true"
      data-value={value}
      data-min={min}
      data-max={max}
      data-step={step}
      data-valid-values={
        typeof validValues === "string" ? validValues : "callback"
      }
      data-notch-count={notchCount}
      data-notch-ticks-visible={String(Boolean(notchTicksVisible))}
    >
      {description}
      <span>{label}</span>
      <button onClick={() => onChange?.(0)}>Set 0x</button>
      <button onClick={() => onChange?.(4)}>Set 5x</button>
    </div>
  ),
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoRestartLabel: ({ label }: { label: string }) => label,
  MakoSettingRelationship: ({ children }: { children: React.ReactNode }) => (
    <div data-mako-setting-relationship="true">{children}</div>
  ),
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (
    _key: string,
    fallback: string,
    replacements: Record<string, string | number> = {},
  ) =>
    Object.entries(replacements).reduce(
      (text, [name, value]) => text.split(`{${name}}`).join(String(value)),
      fallback,
    ),
}));

import { FpsMultiplierControl } from "../../src/components/FpsMultiplierControl";
import { getDefaults } from "../../src/config/configSchema";

afterEach(cleanup);

describe("Frame Generation controls", () => {
  test("shows one 0x-capable multiplier slider for the selected mode", () => {
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

    expect(screen.queryByText("Fractional Adaptive")).toBeNull();
    expect(
      screen
        .getByText("Adaptive Frame Generation")
        .getAttribute("data-bottom-separator"),
    ).toBe("default");
    const fixedMultiplierField = screen
      .getByText("Fixed Multiplier (2x)")
      .closest<HTMLElement>('[data-slider-field="true"]');
    expect(fixedMultiplierField).toBeTruthy();
    expect(fixedMultiplierField?.getAttribute("data-value")).toBe("1");
    expect(fixedMultiplierField?.getAttribute("data-min")).toBe("0");
    expect(fixedMultiplierField?.getAttribute("data-max")).toBe("4");
    expect(fixedMultiplierField?.getAttribute("data-valid-values")).toBe(
      "steps",
    );
    expect(fixedMultiplierField?.getAttribute("data-notch-count")).toBe("5");
    expect(fixedMultiplierField?.getAttribute("data-notch-ticks-visible")).toBe(
      "true",
    );
    expect(screen.queryByText(/Maximum Adaptive Multiplier/)).toBeNull();
    const lighterModel = screen.getByText("Lighter FG Model");
    const smoothCadence = screen.getByText("Smooth Cadence");
    expect(lighterModel.getAttribute("data-bottom-separator")).toBe("none");
    expect(
      screen
        .getByText("Fixed Multiplier (2x)")
        .compareDocumentPosition(smoothCadence) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
    expect(
      smoothCadence.compareDocumentPosition(lighterModel) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
    fireEvent.click(lighterModel);
    expect(onConfigChange).toHaveBeenCalledWith("performance_mode", true);
    expect(
      within(fixedMultiplierField as HTMLElement).getByText(
        /Use 0x to pause or resume Frame Generation live without unloading its resources. Select 2x–5x for a constant generation ratio/,
      ),
    ).toBeTruthy();
    expect(
      screen.getByText(
        /Enable Fractional Adaptive to keep more real frames, but test it per game/,
      ),
    ).toBeTruthy();
    expect(
      screen.queryByText(/MAKO prepares and swaps private resources live/),
    ).toBeNull();
    expect(screen.queryByText(/may require a restart/)).toBeNull();
    fireEvent.click(
      within(fixedMultiplierField as HTMLElement).getByText("Set 5x"),
    );
    expect(onConfigUpdate).toHaveBeenCalledWith({
      frame_generation_enabled: true,
      multiplier: 5,
    });

    rerender(
      <FpsMultiplierControl
        config={{ ...defaults, adaptive: true }}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    expect(screen.getByText("Fractional Adaptive")).toBeTruthy();
    expect(screen.queryByText("Real Frame Priority")).toBeNull();
    expect(
      screen.queryByText(/MAKO prepares and swaps private resources live/),
    ).toBeNull();
    expect(
      screen
        .getByText("Adaptive Frame Generation")
        .getAttribute("data-bottom-separator"),
    ).toBe("default");
    const adaptiveMultiplierField = screen
      .getByText("Maximum Adaptive Multiplier (3x)")
      .closest<HTMLElement>('[data-slider-field="true"]');
    expect(adaptiveMultiplierField).toBeTruthy();
    expect(screen.queryByText(/Fixed Multiplier/)).toBeNull();
    expect(
      screen.getByText(
        "Cannot be combined with Steady Base Cap. Changing it also turns Dynamic Cadence Recovery off.",
      ),
    ).toBeTruthy();
    expect(
      screen.getByText(
        "Overrides Base FPS Cap. Cannot be combined with Fractional Adaptive or Dynamic Cadence Recovery.",
      ),
    ).toBeTruthy();
    expect(
      within(adaptiveMultiplierField as HTMLElement).getByText(
        /^Use 0x to pause or resume Frame Generation live without unloading its resources. Otherwise this is/,
      ).style.paddingBottom,
    ).toBe("2px");
    fireEvent.click(
      within(adaptiveMultiplierField as HTMLElement).getByText("Set 5x"),
    );
    expect(onConfigUpdate).toHaveBeenLastCalledWith({
      frame_generation_enabled: true,
      adaptive_max_multiplier: 5,
    });

    rerender(
      <FpsMultiplierControl
        config={{ ...defaults, adaptive: false, ultra_performance: true }}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );
    const lockedLighterModel = screen.getByText("Lighter FG Model");
    expect((lockedLighterModel as HTMLButtonElement).disabled).toBe(true);
    expect(lockedLighterModel.getAttribute("data-checked")).toBe("true");
  });

  test("uses 0x as the live generation switch without collapsing saved controls", () => {
    window.SP_REACT = React;
    const config = {
      ...getDefaults(),
      frame_generation_enabled: false,
      adaptive: true,
      adaptive_auto_base_fps_cap: false,
    };
    const onConfigChange = vi.fn(async () => undefined);
    const onConfigUpdate = vi.fn(async () => undefined);

    const { rerender } = render(
      <FpsMultiplierControl
        config={config}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    expect(
      screen
        .getByText("Enable Frame-gen (Restart)")
        .getAttribute("data-checked"),
    ).toBe("true");
    expect(
      screen.getByText(
        "Enable before starting the game. Loads and provisions MAKO Frame Generation. Turn it off when you only want Scaling or Shaders.",
      ),
    ).toBeTruthy();
    expect(screen.getByText("Adaptive Frame Generation")).toBeTruthy();
    expect(screen.getByText("Fractional Adaptive")).toBeTruthy();
    expect(screen.getByText("Real Frame Priority")).toBeTruthy();
    expect(
      screen.getByText(
        "Automatic keeps Fractional Adaptive's current behavior. Base FPS Cap remains available.",
      ),
    ).toBeTruthy();
    const realFramePriority = screen.getByTestId(
      "real-frame-priority-dropdown",
    );
    expect(realFramePriority.textContent).toBe("Automatic");
    expect(
      JSON.parse(realFramePriority.getAttribute("data-options") || "[]"),
    ).toEqual(["auto", "low", "medium", "high", "very-high"]);
    expect(
      JSON.parse(realFramePriority.getAttribute("data-labels") || "[]"),
    ).toEqual([
      "Automatic",
      "Low — up to 54 real FPS (60% of target)",
      "Medium — up to 60 real FPS (67% of target)",
      "High — up to 67.5 real FPS (75% of target)",
      "Very High — up to 72 real FPS (80% of target)",
    ]);
    fireEvent.click(realFramePriority);
    expect(onConfigUpdate).toHaveBeenCalledWith({
      adaptive_fractional_real_frame_priority: "medium",
      dynamic_cadence_recovery: false,
    });
    expect(screen.getByText(/Target FPS \(90\)$/)).toBeTruthy();
    const pausedAdaptiveMultiplier = screen
      .getByText("Maximum Adaptive Multiplier (0x)")
      .closest<HTMLElement>('[data-slider-field="true"]');
    expect(pausedAdaptiveMultiplier?.getAttribute("data-value")).toBe("0");
    expect(screen.queryByText(/Fixed Multiplier/)).toBeNull();

    fireEvent.click(
      within(pausedAdaptiveMultiplier as HTMLElement).getByText("Set 5x"),
    );
    expect(onConfigUpdate).toHaveBeenCalledWith({
      frame_generation_enabled: true,
      adaptive_max_multiplier: 5,
    });

    rerender(
      <FpsMultiplierControl
        config={{ ...config, frame_generation_enabled: true }}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    expect(screen.getByText("Adaptive Frame Generation")).toBeTruthy();
    const activeAdaptiveMultiplier = screen
      .getByText("Maximum Adaptive Multiplier (3x)")
      .closest<HTMLElement>('[data-slider-field="true"]');
    expect(
      screen.getByText("Fractional Adaptive").getAttribute("data-checked"),
    ).toBe("true");
    expect(screen.getByText(/Target FPS \(90\)$/)).toBeTruthy();
    fireEvent.click(
      within(activeAdaptiveMultiplier as HTMLElement).getByText("Set 0x"),
    );
    expect(onConfigChange).toHaveBeenLastCalledWith(
      "frame_generation_enabled",
      false,
    );

    rerender(
      <FpsMultiplierControl
        config={{ ...config, target_fps: 120 }}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );
    expect(
      JSON.parse(
        screen
          .getByTestId("real-frame-priority-dropdown")
          .getAttribute("data-labels") || "[]",
      ),
    ).toEqual([
      "Automatic",
      "Low — up to 72 real FPS (60% of target)",
      "Medium — up to 80 real FPS (67% of target)",
      "High — up to 90 real FPS (75% of target)",
      "Very High — up to 96 real FPS (80% of target)",
    ]);

    rerender(
      <FpsMultiplierControl
        config={{
          ...config,
          target_fps: 120,
          adaptive_fractional_real_frame_priority: "medium",
        }}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );
    expect(
      screen.getByText(
        "At a 120 FPS target, the estimated split is 80 real / 40 generated FPS (about 2:1) if the target is met. Actual rates vary. This overrides Base FPS Cap.",
      ),
    ).toBeTruthy();
  });

  test("collapses Frame Generation controls only when provisioning is off", () => {
    window.SP_REACT = React;
    const config = {
      ...getDefaults(),
      frame_generation_provisioned: false,
    };
    const onConfigChange = vi.fn(async () => undefined);

    render(
      <FpsMultiplierControl
        config={config}
        onConfigChange={onConfigChange}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    const provision = screen.getByText("Enable Frame-gen (Restart)");
    expect(provision.getAttribute("data-checked")).toBe("false");
    expect(provision.getAttribute("data-bottom-separator")).toBe("none");
    expect(screen.queryByText("Adaptive Frame Generation")).toBeNull();
    expect(screen.queryByText(/Fixed Multiplier/)).toBeNull();
    expect(screen.queryByText(/Maximum Adaptive Multiplier/)).toBeNull();
    expect(
      screen.queryByText(
        /Use 0x to pause or resume Frame Generation live without unloading its resources/,
      ),
    ).toBeNull();

    fireEvent.click(provision);
    expect(onConfigChange).toHaveBeenCalledWith(
      "frame_generation_provisioned",
      true,
    );
  });

  test("preserves every Adaptive subsetting when the mode is re-enabled", () => {
    window.SP_REACT = React;
    const onConfigUpdate = vi.fn(async () => undefined);
    const fractionalConfig = {
      ...getDefaults(),
      adaptive: false,
      adaptive_auto_base_fps_cap: false,
      target_fps: 120,
      adaptive_max_multiplier: 4,
      adaptive_stable_cadence: false,
      dynamic_cadence_recovery: true,
    };

    render(
      <FpsMultiplierControl
        config={fractionalConfig}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    fireEvent.click(screen.getByText("Adaptive Frame Generation"));
    expect(onConfigUpdate).toHaveBeenCalledWith({ adaptive: true });
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

  test("shows Recovery's fractional state and direct preset changes disable Recovery", () => {
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
    const fractional = screen.getByText("Fractional Adaptive");
    const steady = screen.getByText("Steady Base Cap (45 FPS)");
    expect((fractional as HTMLButtonElement).disabled).toBe(false);
    expect(fractional.getAttribute("data-checked")).toBe("true");
    expect((steady as HTMLButtonElement).disabled).toBe(false);

    fireEvent.click(fractional);
    expect(onConfigUpdate).toHaveBeenCalledWith({
      adaptive_auto_base_fps_cap: true,
      dynamic_cadence_recovery: false,
    });

    fireEvent.click(steady);
    expect(onConfigUpdate).toHaveBeenCalledWith({
      adaptive_auto_base_fps_cap: true,
      dynamic_cadence_recovery: false,
    });
  });

  test("turning Steady Base Cap off explicitly keeps Recovery disabled", () => {
    window.SP_REACT = React;
    const onConfigUpdate = vi.fn(async () => undefined);

    render(
      <FpsMultiplierControl
        config={{
          ...getDefaults(),
          adaptive: true,
          adaptive_auto_base_fps_cap: true,
          dynamic_cadence_recovery: false,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    fireEvent.click(screen.getByText("Steady Base Cap (45 FPS)"));
    expect(onConfigUpdate).toHaveBeenCalledWith({
      adaptive_auto_base_fps_cap: false,
      dynamic_cadence_recovery: false,
    });
  });
});
