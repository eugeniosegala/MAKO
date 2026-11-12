import React from "react";
import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

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
  afterEach(cleanup);

  test("keeps the restart recommendation inside the disabled installer row", () => {
    window.SP_REACT = React;
    render(
      <InstallationButton
        isInstalled={false}
        isInstalling={true}
        isInstallCompletionVisible={true}
        isUninstalling={false}
        hostArchitectureSupported={true}
        installationVerified={true}
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

  test("stays disabled until the real installation check finishes", () => {
    window.SP_REACT = React;
    const props = {
      isInstalled: true,
      isInstalling: false,
      isInstallCompletionVisible: false,
      isUninstalling: false,
      hostArchitectureSupported: true,
      onInstall: vi.fn(),
      onUninstall: vi.fn(),
    };
    const { rerender } = render(
      <InstallationButton {...props} installationVerified={false} />,
    );
    const button = screen.getByRole("button") as HTMLButtonElement;
    expect(button.disabled).toBe(true);
    rerender(<InstallationButton {...props} installationVerified={true} />);
    expect(button.disabled).toBe(false);
  });
});
