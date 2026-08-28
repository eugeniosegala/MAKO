import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

const navigation = vi.hoisted(() => ({
  NavigateToExternalWeb: vi.fn(),
}));

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
  Navigation: navigation,
}));
vi.mock("../../src/components/MakoInstallCountdown", () => ({
  MakoInstallCompletion: () => <span>Install complete</span>,
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoCompactSpinner: () => <span>Working</span>,
  makoAccentColor: "#83bff0",
  makoPanelDivider: "1px solid",
  makoPanelStyle: {},
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { ContentNotices } from "../../src/components/ContentNotices";
import type { LocalDevelopmentBuildInfo } from "../../src/config/devBuildInfo";

afterEach(cleanup);

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
  showWelcome: false,
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

  test("shows succinct restart guidance and opens the MAKO documentation", () => {
    render(<ContentNotices {...baseProps} showWelcome={true} />);

    expect(screen.getByText("Hello from the MAKO Team")).toBeTruthy();
    expect(screen.getByText(/options marked Restart/)).toBeTruthy();
    expect(screen.getByText(/Every game is different/)).toBeTruthy();

    fireEvent.click(screen.getByText("Read the MAKO documentation"));
    expect(navigation.NavigateToExternalWeb).toHaveBeenCalledWith(
      "https://github.com/eugeniosegala/MAKO#documentation",
    );
  });

  test("places guidance between development identity and the running game", () => {
    render(
      <ContentNotices
        {...baseProps}
        developmentBuildInfo={developmentBuildInfo}
        mainRunningApp={{ appid: 123, display_name: "Test Game" } as never}
        showWelcome={true}
      />,
    );

    const development = screen.getByText("🧪 Local development deployment");
    const welcome = screen.getByRole("note");
    const running = screen.getByText("Test Game").parentElement;

    expect(
      development.compareDocumentPosition(welcome) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
    expect(
      welcome.compareDocumentPosition(running!) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
    expect(welcome.style.marginTop).toBe("8px");
    expect(running?.style.marginTop).toBe("8px");
  });
});
