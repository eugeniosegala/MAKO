import React from "react";
import { fireEvent, render, screen } from "@testing-library/react";
import { beforeEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  DialogButton: ({
    children,
    onClick,
    ...props
  }: React.ButtonHTMLAttributes<HTMLButtonElement>) => (
    <button onClick={onClick} {...props}>
      {children}
    </button>
  ),
  ButtonItem: ({
    children,
    onClick,
    disabled,
  }: React.ButtonHTMLAttributes<HTMLButtonElement>) => (
    <button onClick={onClick} disabled={disabled}>
      {children}
    </button>
  ),
}));
vi.mock("../../src/components/MakoInstallCountdown", () => ({
  MakoInstallCompletion: () => <span>Install complete</span>,
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoCompactSpinner: () => <span>Working</span>,
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { ContentNotices } from "../../src/components/ContentNotices";
import type { LocalDevelopmentBuildInfo } from "../../src/config/devBuildInfo";

const developmentBuildInfo: LocalDevelopmentBuildInfo = {
  generatedAt: "2026-08-21T10:00:00.000Z",
  plugin: {
    commit: "abc1234",
    dirty: true,
    frontendDeployed: true,
    backendDeployed: false,
  },
  engine: {
    commit: "def5678",
    dirty: false,
    layer64Sha256: "a".repeat(64),
    layer32Sha256: null,
    flatpakArchiveSha256: "b".repeat(64),
  },
};

const baseProps = {
  developmentBuildInfo: null,
  mainRunningApp: undefined,
  engineUpdateRequired: false,
  installedEngineVersion: undefined,
  expectedEngineVersion: undefined,
  isInstalling: false,
  isInstallCompletionVisible: false,
  isUninstalling: false,
  onInstall: vi.fn(async () => undefined),
};

describe("content status notices", () => {
  beforeEach(() => {
    window.SP_REACT = React;
  });

  test("retains development identity and reveals deployment details on demand", () => {
    render(
      <ContentNotices
        {...baseProps}
        developmentBuildInfo={developmentBuildInfo}
      />,
    );

    expect(screen.getByText("🧪 Local development deployment")).toBeTruthy();
    expect(screen.getByText("Details").getAttribute("aria-expanded")).toBe(
      "false",
    );
    expect(screen.queryByText(/^64-bit layer:/)).toBeNull();

    fireEvent.click(screen.getByText("Details"));

    expect(screen.getByText("Hide").getAttribute("aria-expanded")).toBe("true");
    expect(screen.getByText("MAKO Decky")).toBeTruthy();
    expect(screen.getByText("MAKO Renderer")).toBeTruthy();
    expect(screen.getByText(/^64-bit layer:/)).toBeTruthy();
    expect(screen.getByText(/^32-bit layer:/)).toBeTruthy();
    expect(screen.getByText(/23\.08, 24\.08, 25\.08 deployed/)).toBeTruthy();
  });

  test("renders running and update notices with the existing install action", () => {
    const onInstall = vi.fn(async () => undefined);
    const { rerender } = render(
      <ContentNotices
        {...baseProps}
        mainRunningApp={{ appid: 123, display_name: "Test Game" } as never}
        engineUpdateRequired={true}
        installedEngineVersion="2.0.0"
        expectedEngineVersion="2.1.0"
        onInstall={onInstall}
      />,
    );

    expect(screen.getByText("Test Game")).toBeTruthy();
    expect(screen.getByText("MAKO Renderer update required")).toBeTruthy();
    fireEvent.click(screen.getByText("Update MAKO Renderer"));
    expect(onInstall).toHaveBeenCalledOnce();

    rerender(
      <ContentNotices
        {...baseProps}
        engineUpdateRequired={true}
        isInstalling={true}
        onInstall={onInstall}
      />,
    );
    expect(
      (
        screen
          .getByText("Updating MAKO Renderer...")
          .closest("button") as HTMLButtonElement
      ).disabled,
    ).toBe(true);
  });
});
