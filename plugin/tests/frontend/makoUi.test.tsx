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
  MakoExperimentalBadge,
  MakoExperimentalSettingLabel,
  MakoInlineTip,
  MakoReleaseIdentity,
  MakoRestartLabel,
  MakoSectionHeader,
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

describe("MAKO section headers", () => {
  test("uses the emphasized four-pixel section divider", () => {
    window.SP_REACT = React;

    render(<MakoSectionHeader>Spatial Scaling</MakoSectionHeader>);

    const heading = screen.getByText("Spatial Scaling");
    expect(heading.style.borderBottom).toBe(
      "4px solid rgba(77, 170, 190, 0.48)",
    );
    expect(heading.parentElement?.style.marginTop).toBe("26px");
  });
});

describe("MAKO restart labels", () => {
  test("renders only the translated parenthetical marker at a smaller size", () => {
    const { container, rerender } = render(
      <MakoRestartLabel label="Enable Scaling (Restart)" />,
    );

    const marker = screen.getByText("(Restart)");
    expect(marker.getAttribute("data-mako-restart-marker")).toBe("true");
    expect(marker.style.fontSize).toBe("0.72em");
    expect(container.textContent).toBe("Enable Scaling (Restart)");

    rerender(<MakoRestartLabel label="スケーリングを有効化（再起動）" />);
    expect(screen.getByText("（再起動）").style.fontSize).toBe("0.72em");
    expect(container.textContent).toBe("スケーリングを有効化（再起動）");
  });

  test("leaves a label without a trailing marker unchanged", () => {
    const { container } = render(<MakoRestartLabel label="Flow Scale" />);

    expect(container.textContent).toBe("Flow Scale");
    expect(
      container.querySelector('[data-mako-restart-marker="true"]'),
    ).toBeNull();
  });
});

describe("MAKO experimental badges", () => {
  test("renders a compact non-warning status marker", () => {
    render(<MakoExperimentalBadge label="Experimental" />);

    const badge = screen.getByText("Experimental");
    expect(badge.getAttribute("data-mako-experimental-badge")).toBe("true");
    expect(badge.style.textTransform).toBe("uppercase");
    expect(badge.style.borderRadius).toBe("999px");
  });

  test("uses the original compact spacing between the setting and badge", () => {
    const { container } = render(
      <MakoExperimentalSettingLabel
        label="Enable Scaling (Restart)"
        badgeLabel="Experimental"
      />,
    );

    const settingLabel = container.querySelector<HTMLElement>(
      '[data-mako-experimental-setting-label="true"]',
    );
    expect(settingLabel?.style.columnGap).toBe("6px");
    expect(settingLabel?.style.rowGap).toBe("6px");
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

describe("MAKO inline tips", () => {
  test("uses the blue information treatment by default", () => {
    render(<MakoInlineTip>Performance context</MakoInlineTip>);

    const info = screen.getByRole("note");
    expect(info.getAttribute("data-tone")).toBe("info");
    expect(info.style.borderLeft).toContain("131, 191, 240");
    expect(info.style.background).toContain("24, 67, 94");
  });

  test("uses the orange warning treatment when requested", () => {
    render(
      <MakoInlineTip tone="warning">
        This option adds runtime overhead
      </MakoInlineTip>,
    );

    const warning = screen.getByRole("note");
    expect(warning.getAttribute("data-tone")).toBe("warning");
    expect(warning.style.borderLeft).toContain("244, 162, 89");
    expect(warning.style.background).toContain("104, 59, 19");
  });
});
