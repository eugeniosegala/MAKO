import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

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
    localStorage.clear();
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

  test("shows concise restart guidance and remembers when tips are collapsed", () => {
    const { unmount } = render(
      <ContentNotices {...baseProps} showWelcome={true} />,
    );

    expect(screen.getByText("🦈")).toBeTruthy();
    expect(screen.getByText("Hello from the MAKO Team!")).toBeTruthy();
    expect(
      screen.getByText(
        "Here's a few tips to make your experience even better.",
      ),
    ).toBeTruthy();
    const welcome = screen.getByRole("note");
    expect(welcome.textContent).toContain("Many settings apply live.");
    expect(welcome.textContent).toContain(
      "Options marked Restart require a game restart.",
    );
    expect(welcome.textContent).toContain(
      "Game resolution and scaling changes can affect performance.",
    );
    expect(welcome.textContent).toContain(
      "If anything looks or feels wrong after several changes, restart the game for a clean new session.",
    );
    const finalTip = screen.getByText(/keep an eye on the release page!$/);
    const underlinedPhrases = Array.from(welcome.querySelectorAll("span"))
      .filter((element) => element.style.textDecorationLine === "underline")
      .map((element) => element.textContent);
    expect(underlinedPhrases).toEqual([
      "looks or feels wrong",
      "several changes",
      "restart the game for a clean new session.",
    ]);

    const collapseButton = screen.getByRole("button", { name: "Hide tips" });
    expect(collapseButton.getAttribute("aria-expanded")).toBe("true");
    expect(collapseButton.parentElement?.style.justifyContent).toBe("center");
    expect(
      finalTip.compareDocumentPosition(collapseButton) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
    fireEvent.click(collapseButton);

    expect(localStorage.getItem("mako-welcome-tips-collapsed")).toBe("true");
    expect(screen.getByRole("button", { name: "Show tips" })).toBeTruthy();
    expect(screen.queryByText(/Here's a few tips/)).toBeNull();
    expect(screen.queryByText(/Every game is different/)).toBeNull();

    unmount();
    render(<ContentNotices {...baseProps} showWelcome={true} />);
    expect(screen.getByRole("button", { name: "Show tips" })).toBeTruthy();
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
