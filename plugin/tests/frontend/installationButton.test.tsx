import React from "react";
import { render, screen } from "@testing-library/react";
import { describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  ButtonItem: ({
    children,
    disabled,
  }: {
    children: React.ReactNode;
    disabled?: boolean;
  }) => <button disabled={disabled}>{children}</button>,
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { InstallationButton } from "../../src/components/InstallationButton";

describe("MAKO Renderer installation completion", () => {
  test("keeps the restart recommendation inside the disabled installer row", () => {
    window.SP_REACT = React;
    render(
      <InstallationButton
        isInstalled={false}
        isInstalling={true}
        isInstallCompletionVisible={true}
        isUninstalling={false}
        hostArchitectureSupported={true}
        onInstall={vi.fn()}
        onUninstall={vi.fn()}
      />,
    );

    expect(screen.getByText("Installation Complete")).toBeTruthy();
    expect(
      screen.getByText("Restarting your device is recommended."),
    ).toBeTruthy();
    expect((screen.getByRole("button") as HTMLButtonElement).disabled).toBe(
      true,
    );
    expect(screen.queryByText("Installing MAKO Renderer...")).toBeNull();
  });
});
