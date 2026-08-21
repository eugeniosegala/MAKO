import React from "react";
import { fireEvent, render, screen } from "@testing-library/react";
import { beforeEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  ToggleField: ({ label }: { label: React.ReactNode }) => <div>{label}</div>,
  SliderField: ({ label }: { label: React.ReactNode }) => <div>{label}</div>,
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
  MakoSectionHeader: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  makoDangerTextColor: "#d08aa0",
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { ConfigurationSection } from "../../src/components/ConfigurationSection";
import { getDefaults } from "../../src/config/configSchema";

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
});
