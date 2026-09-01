import React from "react";
import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, test, vi } from "vitest";

const mocks = vi.hoisted(() => ({
  checkFgmodDirectory: vi.fn(),
  copyWithVerification: vi.fn(),
  getLaunchOption: vi.fn(),
  showClipboardErrorToast: vi.fn(),
}));

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
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
vi.mock("../../src/api/makoApi", () => ({
  checkFgmodDirectory: mocks.checkFgmodDirectory,
  DEFAULT_STEAM_LAUNCH_OPTION: "/home/deck/.local/bin/mako-run %command%",
  getLaunchOption: mocks.getLaunchOption,
}));
vi.mock("../../src/utils/clipboardUtils", () => ({
  copyWithVerification: mocks.copyWithVerification,
}));
vi.mock("../../src/utils/toastUtils", () => ({
  showClipboardErrorToast: mocks.showClipboardErrorToast,
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { FgmodClipboardButton } from "../../src/components/FgmodClipboardButton";
import { SmartClipboardButton } from "../../src/components/SmartClipboardButton";
import { UsageInstructions } from "../../src/components/UsageInstructions";

describe("clipboard buttons", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    window.SP_REACT = React;
    mocks.copyWithVerification.mockResolvedValue({
      success: true,
      verified: true,
    });
  });

  test("keeps the normal launch-option fallback when its RPC is unavailable", async () => {
    mocks.getLaunchOption.mockRejectedValue(new Error("RPC unavailable"));

    render(<SmartClipboardButton />);
    fireEvent.click(screen.getByText("Copy Launch Option"));

    await screen.findByText("Copied to clipboard");
    expect(mocks.copyWithVerification).toHaveBeenCalledWith(
      "/home/deck/.local/bin/mako-run %command%",
    );
    expect(mocks.showClipboardErrorToast).not.toHaveBeenCalled();
  });

  test("keeps the copy action inside usage instructions before Renderer installation", async () => {
    mocks.getLaunchOption.mockRejectedValue(new Error("Renderer unavailable"));

    render(<UsageInstructions />);

    expect(await screen.findByText("Usage Instructions")).toBeTruthy();
    expect(screen.getByText("Copy Launch Option")).toBeTruthy();
    expect(
      screen.getByText("/home/deck/.local/bin/mako-run %command%"),
    ).toBeTruthy();
  });

  test("keeps the DeckyFG provider failure visible instead of copying a partial command", async () => {
    mocks.checkFgmodDirectory.mockResolvedValue({ exists: true });
    mocks.getLaunchOption.mockRejectedValue(new Error("RPC unavailable"));

    render(<FgmodClipboardButton />);
    fireEvent.click(await screen.findByText("MAKO + DeckyFG"));

    await waitFor(() =>
      expect(mocks.showClipboardErrorToast).toHaveBeenCalledOnce(),
    );
    expect(mocks.copyWithVerification).not.toHaveBeenCalled();
    expect(screen.getByText("MAKO + DeckyFG")).toBeTruthy();
  });
});
