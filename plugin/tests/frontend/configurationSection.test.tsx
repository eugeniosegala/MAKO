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
    onChange,
  }: {
    label: React.ReactNode;
    checked: boolean;
    disabled?: boolean;
    onChange: (value: boolean) => void;
  }) => (
    <button disabled={disabled} onClick={() => onChange(!checked)}>
      {label}
    </button>
  ),
  SliderField: ({
    label,
    value,
    disabled,
    onChange,
  }: {
    label: React.ReactNode;
    value: number;
    disabled?: boolean;
    onChange: (value: number) => void;
  }) => (
    <button
      disabled={disabled}
      onClick={() => onChange(value === 0 ? 30 : value)}
    >
      {label}
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
  MakoInlineWarning: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  MakoSectionHeader: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { ConfigurationSection } from "../../src/components/ConfigurationSection";
import { getDefaults } from "../../src/config/configSchema";

afterEach(cleanup);

describe("External Tools controls", () => {
  beforeEach(() => {
    window.SP_REACT = React;
    localStorage.clear();
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
    expect(screen.queryByText("Enable vkBasalt (Experimental, Restart)")).toBeNull();

    const collapseButton = container.querySelector<HTMLButtonElement>(
      ".MAKO_ExternalToolsCollapseButton_Container button",
    );
    expect(collapseButton).toBeTruthy();
    fireEvent.click(collapseButton!);

    expect(screen.getByText("Enable MangoHud (Restart)")).toBeTruthy();
    expect(screen.getByText("Enable vkBasalt (Experimental, Restart)")).toBeTruthy();
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
});
