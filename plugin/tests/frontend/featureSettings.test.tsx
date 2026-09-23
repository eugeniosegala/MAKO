import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoSectionHeader: ({
    children,
    description,
  }: {
    children: React.ReactNode;
    description?: React.ReactNode;
  }) => (
    <div>
      <h2>{children}</h2>
      {description}
    </div>
  ),
  MakoInlineTip: ({
    children,
    tone,
  }: {
    children: React.ReactNode;
    tone?: string;
  }) => (
    <div role="note" data-tone={tone}>
      {children}
    </div>
  ),
  MakoFocusable: ({
    children,
    onActivate: _onActivate,
    onGamepadFocus: _onGamepadFocus,
    onGamepadBlur: _onGamepadBlur,
    noFocusRing: _noFocusRing,
    "flow-children": _flowChildren,
    ...props
  }: React.HTMLAttributes<HTMLDivElement> & {
    onActivate?: () => void;
    onGamepadFocus?: () => void;
    onGamepadBlur?: () => void;
    noFocusRing?: boolean;
    "flow-children"?: string;
  }) => (
    <div tabIndex={0} {...props}>
      {children}
    </div>
  ),
  makoDialogButtonStyle: () => ({}),
}));
vi.mock("../../src/components/FpsMultiplierControl", () => ({
  FpsMultiplierControl: () => <div>Frame Generation controls</div>,
}));
vi.mock("../../src/components/ScalingControl", () => ({
  ScalingControl: () => <div>Upscaling controls</div>,
}));
vi.mock("../../src/components/settings/PerformanceConfigurationGroup", () => ({
  PerformanceConfigurationGroup: () => <div>FG performance controls</div>,
}));
vi.mock("../../src/components/settings/ShadersConfigurationGroup", () => ({
  ShadersConfigurationGroup: () => <div>Shader controls</div>,
}));
vi.mock("../../src/components/ConfigurationSection", () => ({
  FrameGenerationConfigurationSection: () => <div>FG advanced controls</div>,
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { FeatureSettings } from "../../src/components/FeatureSettings";
import { getDefaults } from "../../src/config/configSchema";
import { EMPTY_RUNTIME_SCALING_UI_STATE } from "../../src/utils/runtimeScalingUtils";

afterEach(cleanup);

describe("primary feature organization", () => {
  test("switches the three modalities while keeping lower settings visible", () => {
    window.SP_REACT = React;
    const { container } = render(
      <FeatureSettings
        config={getDefaults()}
        runtimeState={EMPTY_RUNTIME_SCALING_UI_STATE}
        profileName="mako"
        vkBasaltConfigPath="/home/deck/.config/vkBasalt/vkBasalt.conf"
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(screen.getByText("Image Processing")).toBeTruthy();
    const sharedPerformanceInfo = screen.getByText(
      "Combining Frame Generation, Scaling, and Shaders can affect performance. Test each game, disable features you do not need, and try both Windowed and Fullscreen modes.",
    );
    expect(sharedPerformanceInfo.closest('[data-tone="info"]')).toBeTruthy();
    expect(
      screen.getByRole("tablist", { name: "Image Processing" }),
    ).toBeTruthy();
    expect(
      sharedPerformanceInfo.compareDocumentPosition(
        screen.getByRole("tablist", { name: "Image Processing" }),
      ) & Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
    expect(screen.getAllByRole("tab")).toHaveLength(3);
    expect(
      screen
        .getByRole("tab", { name: "Frame-gen" })
        .getAttribute("aria-selected"),
    ).toBe("true");
    expect(screen.getByRole("tab", { name: "Frame-gen" }).style.height).toBe(
      "46px",
    );
    expect(screen.getByRole("tab", { name: "Frame-gen" }).style.boxSizing).toBe(
      "border-box",
    );
    expect(
      container.querySelector('[data-mako-modality-ribbon="frame-generation"]')
        ?.textContent,
    ).toBe("Frame-gen");
    expect(
      (
        container.querySelector(
          '[data-mako-modality-selector="true"]',
        ) as HTMLElement
      ).style.margin,
    ).toBe("14px 0px 12px");
    const frameGenerationIcon = screen
      .getByRole("tab", { name: "Frame-gen" })
      .querySelector("svg") as SVGElement;
    expect(frameGenerationIcon.style.transform).toBe("translateY(0) scale(1)");
    expect(screen.getByText("Frame Generation controls")).toBeTruthy();
    expect(screen.getByText("FG performance controls")).toBeTruthy();
    expect(screen.getByText("FG advanced controls")).toBeTruthy();
    expect(screen.queryByText("Upscaling controls")).toBeNull();
    expect(screen.queryByText("Shader controls")).toBeNull();

    const spatialTab = screen.getByRole("tab", { name: "Scaling" });
    const spatialRibbon = container.querySelector(
      '[data-mako-modality-ribbon="spatial"]',
    ) as HTMLElement;
    expect(spatialRibbon.style.opacity).toBe("0");
    fireEvent.mouseEnter(spatialTab);
    expect(spatialRibbon.style.opacity).toBe("1");
    expect(
      (spatialTab.querySelector("svg") as SVGElement).style.transform,
    ).toBe("translateY(-5px) scale(0.94)");
    fireEvent.mouseLeave(spatialTab);
    expect(spatialRibbon.style.opacity).toBe("0");
    fireEvent.focus(spatialTab);
    expect(spatialRibbon.style.opacity).toBe("1");
    fireEvent.blur(spatialTab);
    expect(spatialRibbon.style.opacity).toBe("0");

    fireEvent.click(spatialTab);
    expect(screen.queryByText("Frame Generation controls")).toBeNull();
    expect(screen.getByText("Upscaling controls")).toBeTruthy();
    expect(screen.queryByText("Shader controls")).toBeNull();
    expect(spatialTab.getAttribute("aria-selected")).toBe("true");
    expect(screen.getByText("FG performance controls")).toBeTruthy();
    expect(screen.getByText("FG advanced controls")).toBeTruthy();

    const shadersTab = screen.getByRole("tab", { name: "Shaders" });
    fireEvent.click(shadersTab);
    expect(screen.queryByText("Frame Generation controls")).toBeNull();
    expect(screen.queryByText("Upscaling controls")).toBeNull();
    expect(screen.getByText("Shader controls")).toBeTruthy();
    expect(shadersTab.getAttribute("aria-selected")).toBe("true");

    const modalityPanel = container.querySelector(
      '[data-mako-modality-panel="shaders"]',
    ) as HTMLElement;
    expect(modalityPanel.style.marginTop).toBe("4px");
    const performance = screen.getByText("FG performance controls");
    const advanced = screen.getByText("FG advanced controls");
    expect(
      modalityPanel.compareDocumentPosition(performance) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
    expect(
      performance.compareDocumentPosition(advanced) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
  });
});
