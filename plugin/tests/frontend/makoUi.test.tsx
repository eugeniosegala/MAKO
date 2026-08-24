import React from "react";
import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  Spinner: () => <span />,
}));

import { MakoReleaseIdentity } from "../../src/components/MakoUi";

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
