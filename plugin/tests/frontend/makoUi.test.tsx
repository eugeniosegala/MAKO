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
  MakoInlineWarning,
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
  test("matches dialog-button hover and focus to each button palette", () => {
    window.SP_REACT = React;

    const { container } = render(<MakoButtonTheme />);
    const theme = container.querySelector("style")?.textContent;

    expect(theme).toContain(".Mako_DialogButton:not(.disabled)");
    expect(theme).toContain(".Mako_DialogButton--danger:not(.disabled)");
    expect(theme).toContain("#0d6875");
    expect(theme).toContain("#913852");
    expect(theme).toContain("outline: 2px solid #52d5e8");
    expect(theme).toContain(".Mako_BrandButton--danger button:focus");
    expect(theme).toContain("outline: 2px solid #e36a79");
    expect(makoDialogButtonStyle(true).outline).toBe("2px solid #52d5e8");
    expect(makoDialogButtonStyle(true, "danger").outline).toBe(
      "2px solid #e36a79",
    );
  });
});

describe("MAKO inline warnings", () => {
  test("uses the orange warning treatment when requested", () => {
    render(
      <MakoInlineWarning tone="warning">Restart required</MakoInlineWarning>,
    );

    const warning = screen.getByRole("note");
    expect(warning.style.borderLeft).toContain("244, 162, 89");
    expect(warning.style.background).toContain("104, 59, 19");
  });
});
