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
  SliderField: ({
    label,
    description,
  }: {
    label: React.ReactNode;
    description?: React.ReactNode;
  }) => (
    <div>
      {description}
      {label}
    </div>
  ),
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
    disabled,
    onClick,
  }: {
    children: React.ReactNode;
    className?: string;
    disabled?: boolean;
    onClick?: () => void;
  }) => (
    <button className={className} disabled={disabled} onClick={onClick}>
      {children}
    </button>
  ),
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoFocusable: ({
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
  MakoInlineTip: ({
    children,
    tone,
  }: {
    children: React.ReactNode;
    tone?: string;
  }) => <div data-tone={tone}>{children}</div>,
  MakoRestartLabel: ({ label }: { label: string }) => label,
  MakoSettingRelationship: ({ children }: { children: React.ReactNode }) => (
    <div data-mako-setting-relationship="true">{children}</div>
  ),
  makoDialogButtonStyle: () => ({}),
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { FpsMultiplierControl } from "../../src/components/FpsMultiplierControl";
import { getDefaults } from "../../src/config/configSchema";

afterEach(cleanup);

describe("Frame Generation controls", () => {
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
      .getByText("Frame Generation Factor")
      .closest<HTMLElement>('[data-field-kind="standard"]');
    expect(fixedMultiplierField).toBeTruthy();
    expect(fixedMultiplierField?.getAttribute("data-bottom-separator")).toBe(
      "default",
    );
    const fixedMultiplierControls = screen
      .getByText("0x")
      .closest<HTMLElement>('[data-focusable="true"]');
    expect(fixedMultiplierControls?.style.marginTop).toBe("6px");
    const lighterModel = screen.getByText("Lighter FG Model");
    const smoothCadence = screen.getByText("Smooth Cadence");
    expect(lighterModel.getAttribute("data-bottom-separator")).toBe("none");
    expect(
      screen
        .getByText("Frame Generation Factor")
        .compareDocumentPosition(smoothCadence) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
    expect(
      smoothCadence.compareDocumentPosition(lighterModel) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
    fireEvent.click(lighterModel);
    expect(onConfigChange).toHaveBeenCalledWith("performance_mode", true);
    const fixedMultiplierDescription = screen.getByText(
      /0x pauses generation live. Fixed mode uses 2x–5x; 5x is a high-cost option for high-refresh displays/,
    );
    expect(fixedMultiplierDescription.style.paddingTop).toBe("8px");
    expect(fixedMultiplierDescription.style.marginBottom).toBe("");
    expect(
      screen.getByText(
        /Enable Fractional Adaptive to keep more real frames, but test it per game/,
      ),
    ).toBeTruthy();
    expect(
      screen.queryByText(/MAKO prepares and swaps private resources live/),
    ).toBeNull();
    expect(screen.queryByText(/may require a restart/)).toBeNull();
    expect(
      screen
        .getByText(
          "Use 0x below to pause or resume Frame Generation live without unloading its resources.",
        )
        .getAttribute("data-tone"),
    ).toBe("info");
    expect(screen.getByText("0x").className).toBe("Mako_DialogButton");
    expect(screen.getByText("2x").className).toBe("Mako_DialogButton");
    expect(screen.getByText("5x").className).toBe("Mako_DialogButton");
    fireEvent.click(screen.getByText("5x"));
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
    expect(
      screen.queryByText(/MAKO prepares and swaps private resources live/),
    ).toBeNull();
    expect(
      screen
        .getByText("Adaptive Frame Generation")
        .getAttribute("data-bottom-separator"),
    ).toBe("default");
    expect(
      screen
        .getByText("Frame Generation Factor")
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
        "The 2x–5x Fixed factors are unavailable in Adaptive mode; 0x can still pause it live.",
      ),
    ).toBeTruthy();
    expect(screen.getByText("0x").className).toBe("Mako_DialogButton");
    expect(screen.getByText("Adaptive").className).toBe("Mako_DialogButton");
    expect(screen.getByText(/^Interpolation ceiling/).style.paddingBottom).toBe(
      "2px",
    );

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
        .getByText("Frame Generation (Restart)")
        .getAttribute("data-checked"),
    ).toBe("true");
    expect(screen.getByText("Adaptive Frame Generation")).toBeTruthy();
    expect(screen.getByText("Fractional Adaptive")).toBeTruthy();
    expect(screen.getByText(/Target FPS \(90\)$/)).toBeTruthy();
    expect(screen.getByText("Frame Generation Factor")).toBeTruthy();
    expect(screen.getByText("0x")).toBeTruthy();
    expect(screen.getByText("Adaptive")).toBeTruthy();

    fireEvent.click(screen.getByText("Adaptive"));
    expect(onConfigChange).toHaveBeenCalledWith(
      "frame_generation_enabled",
      true,
    );
    expect(onConfigUpdate).not.toHaveBeenCalled();

    rerender(
      <FpsMultiplierControl
        config={{ ...config, frame_generation_enabled: true }}
        onConfigChange={onConfigChange}
        onConfigUpdate={onConfigUpdate}
      />,
    );

    expect(screen.getByText("Adaptive Frame Generation")).toBeTruthy();
    expect(screen.getByText("Adaptive").textContent).toBe("Adaptive");
    expect(
      screen.getByText("Fractional Adaptive").getAttribute("data-checked"),
    ).toBe("true");
    expect(screen.getByText(/Target FPS \(90\)$/)).toBeTruthy();
    fireEvent.click(screen.getByText("0x"));
    expect(onConfigChange).toHaveBeenLastCalledWith(
      "frame_generation_enabled",
      false,
    );
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

    const provision = screen.getByText("Frame Generation (Restart)");
    expect(provision.getAttribute("data-checked")).toBe("false");
    expect(provision.getAttribute("data-bottom-separator")).toBe("none");
    expect(screen.queryByText("Adaptive Frame Generation")).toBeNull();
    expect(screen.queryByText("Frame Generation Factor")).toBeNull();
    expect(
      screen.queryByText(
        "Use 0x below to pause or resume Frame Generation live without unloading its resources.",
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
