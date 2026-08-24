import React from "react";
import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  Spinner: () => <span />,
}));

import {
  MakoButtonTheme,
  MakoReleaseIdentity,
  makoDialogButtonStyle,
} from "../../src/components/MakoUi";

afterEach(cleanup);

describe("MAKO release identity", () => {
  test("renders the version and codename at half opacity", () => {
    window.SP_REACT = React;

    render(<MakoReleaseIdentity version="2.1.0" codename="Abyss Ascending" />);

    const identity = screen.getByLabelText(
      "Current release: MAKO Decky v2.1.0, Abyss Ascending",
    );
    expect(identity.style.opacity).toBe("0.5");
    expect(screen.getByText("abyss-ascending")).toBeTruthy();
  });
});

describe("MAKO button theme", () => {
  test("keeps dialog-button hover and focus within the MAKO palette", () => {
    window.SP_REACT = React;

    const { container } = render(<MakoButtonTheme />);
    const theme = container.querySelector("style")?.textContent;

    expect(theme).toContain(".Mako_DialogButton:not(.disabled)");
    expect(theme).toContain(".Mako_DialogButton--danger:not(.disabled)");
    expect(theme).toContain("#0d6875");
    expect(theme).toContain("#913852");
    expect(theme).toContain("outline: 2px solid #52d5e8");
    expect(makoDialogButtonStyle(true).outline).toBe("2px solid #52d5e8");
  });
});
