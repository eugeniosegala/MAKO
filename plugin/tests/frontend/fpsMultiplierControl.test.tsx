import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  Field: ({
    children,
    label,
    description,
    bottomSeparator,
  }: {
    children: React.ReactNode;
    label: React.ReactNode;
    description?: React.ReactNode;
    bottomSeparator?: string;
  }) => (
    <div
      data-field-kind="standard"
      data-bottom-separator={bottomSeparator ?? "default"}
    >
      <span>{label}</span>
      {description && <span>{description}</span>}
      {children}
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
  SliderField: ({ label }: { label: React.ReactNode }) => <div>{label}</div>,
  Focusable: ({
    children,
    style,
  }: {
    children: React.ReactNode;
    style?: React.CSSProperties;
  }) => (
    <div data-focusable="true" style={style}>
      {children}
    </div>
  ),
  DialogButton: ({
    children,
    className,
  }: {
    children: React.ReactNode;
    className?: string;
  }) => <button className={className}>{children}</button>,
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoInlineWarning: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  MakoSettingRelationship: ({
    children,
  }: {
    children: React.ReactNode;
  }) => <div data-mako-setting-relationship="true">{children}</div>,
  makoDialogButtonStyle: () => ({}),
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { FpsMultiplierControl } from "../../src/components/FpsMultiplierControl";
import { getDefaults } from "../../src/config/configSchema";

afterEach(cleanup);

describe("Frame Generation Mode controls", () => {
  test("keeps Adaptive and Fixed Multiplier as standard rows", () => {
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
      .getByText("Fixed FPS Multiplier")
      .closest<HTMLElement>('[data-field-kind="standard"]');
    expect(fixedMultiplierField).toBeTruthy();
    expect(fixedMultiplierField?.getAttribute("data-bottom-separator")).toBe(
      "none",
    );
    const fixedMultiplierControls = screen
      .getByText("−")
      .closest<HTMLElement>('[data-focusable="true"]');
    expect(fixedMultiplierControls?.style.marginTop).toBe("6px");
    const fixedMultiplierDescription = screen.getByText(
      /Fixed may perform better than Adaptive in some games, especially when frame pacing is uneven or unstable. Test both per game/,
    );
    expect(fixedMultiplierDescription.style.paddingTop).toBe("8px");
    expect(fixedMultiplierDescription.style.marginBottom).toBe("");
    expect(screen.getAllByText(/MAKO briefly asks the game/)).toHaveLength(2);
    expect(screen.queryByText(/may require a restart/)).toBeNull();
    expect(screen.getByText("−").className).toBe("Mako_DialogButton");
    expect(screen.getByText("+").className).toBe("Mako_DialogButton");

    rerender(
      <FpsMultiplierControl
        config={{ ...defaults, adaptive: true }}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    expect(screen.getByText("Fractional Adaptive")).toBeTruthy();
    expect(screen.getAllByText(/MAKO briefly asks the game/)).toHaveLength(2);
    expect(
      screen
        .getByText("Adaptive Frame Generation")
        .getAttribute("data-bottom-separator"),
    ).toBe("default");
    expect(
      screen
        .getByText("Fixed FPS Multiplier")
        .closest('[data-field-kind="standard"]'),
    ).toBeTruthy();
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
      screen.getByText(
        "Unavailable while Adaptive Frame Generation is enabled.",
      ),
    ).toBeTruthy();
  });

  test("keeps the saved Fractional selection visible while generation is off", () => {
    window.SP_REACT = React;

    render(
      <FpsMultiplierControl
        config={{
          ...getDefaults(),
          frame_generation_enabled: false,
          adaptive: true,
          adaptive_auto_base_fps_cap: false,
        }}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(
      screen.getByText("Fractional Adaptive").getAttribute("data-checked"),
    ).toBe("true");
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
