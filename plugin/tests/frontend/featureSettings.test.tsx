import React from "react";
import { cleanup, render, screen } from "@testing-library/react";
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
  makoDialogButtonStyle: () => ({}),
}));
vi.mock("../../src/components/FpsMultiplierControl", () => ({
  FpsMultiplierControl: () => <div>Frame Generation controls</div>,
}));
vi.mock("../../src/components/ScalingControl", () => ({
  ScalingControl: () => <div>Upscaling controls</div>,
}));
vi.mock("../../src/components/ConfigurationSectionGroups", () => ({
  PerformanceConfigurationGroup: () => <div>FG performance controls</div>,
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
  test("shows the feature groups in their required order without section subtitles", () => {
    window.SP_REACT = React;
    render(
      <FeatureSettings
        config={getDefaults()}
        runtimeState={EMPTY_RUNTIME_SCALING_UI_STATE}
        onConfigChange={vi.fn(async () => undefined)}
        onConfigUpdate={vi.fn(async () => undefined)}
      />,
    );

    expect(screen.getByText("Frame Generation controls")).toBeTruthy();
    expect(screen.getByText("FG performance controls")).toBeTruthy();
    expect(screen.getByText("FG advanced controls")).toBeTruthy();
    expect(screen.getByText("Upscaling controls")).toBeTruthy();
    expect(screen.getByText("Spatial Settings")).toBeTruthy();
    expect(
      screen.queryByText(
        "Choose Fixed or Adaptive behavior for the same Frame Generation feature.",
      ),
    ).toBeNull();
    expect(
      screen.queryByText(
        "Reconstruct a lower game resolution to the selected output, independently or before Frame Generation.",
      ),
    ).toBeNull();
    expect(screen.queryByRole("button", { name: "Upscaling" })).toBeNull();

    const frameGeneration = screen.getByText("Frame Generation controls");
    const spatialSettings = screen.getByText("Spatial Settings");
    const performance = screen.getByText("FG performance controls");
    const advanced = screen.getByText("FG advanced controls");
    expect(
      frameGeneration.compareDocumentPosition(spatialSettings) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
    expect(
      spatialSettings.compareDocumentPosition(performance) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
    expect(
      performance.compareDocumentPosition(advanced) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).not.toBe(0);
  });
});
