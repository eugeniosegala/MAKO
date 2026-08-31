import React from "react";
import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (
    _key: string,
    fallback: string,
    replacements: Record<string, string | number> = {},
  ) =>
    Object.entries(replacements).reduce(
      (text, [name, value]) => text.split(`{${name}}`).join(String(value)),
      fallback,
    ),
}));

import { RuntimeStatusCard } from "../../src/components/RuntimeStatusCard";
import { EMPTY_RUNTIME_SCALING_UI_STATE } from "../../src/utils/runtimeScalingUtils";

afterEach(cleanup);

describe("authoritative live status", () => {
  test("shows applied Frame Generation and scaling geometry", () => {
    window.SP_REACT = React;
    const { container } = render(
      <RuntimeStatusCard
        runtimeState={{
          ...EMPTY_RUNTIME_SCALING_UI_STATE,
          hasContext: true,
          phase: "active",
          frameGenerationActive: true,
          frameGenerationEnabled: true,
          frameGenerationMode: "adaptive",
          frameGenerationTargetFps: 120,
          frameGenerationMultiplier: 3,
          scalingActive: true,
          scalingEnabled: true,
          sourceWidth: 960,
          sourceHeight: 540,
          presentationWidth: 1440,
          presentationHeight: 810,
          requestedMethod: "ls1",
          activeMethod: "mako",
          effectiveFactor: 1.5,
          pipeline: "pre-frame-generation",
          supersamplingActive: true,
          fallbackReason: "translator unavailable",
        }}
      />,
    );

    expect(screen.getByText("MAKO is active")).toBeTruthy();
    expect(
      container.querySelector(
        '[data-mako-live-status-grid="compact-two-column"]',
      ),
    ).toBeTruthy();
    expect(screen.getByText("Adaptive · 120 FPS · up to 3x")).toBeTruthy();
    expect(
      screen.getByText("MAKO Scaler · 960 × 540 → 1440 × 810 · 1.50x"),
    ).toBeTruthy();
    expect(
      screen.getByText("Upscaling runs before generated frames."),
    ).toBeTruthy();
    expect(
      screen.getByText(
        "Quality Supersampling is on for a sharper final image.",
      ),
    ).toBeTruthy();
    expect(
      screen.getByText(
        "You selected LS1 Quality; MAKO is using MAKO Scaler instead.",
      ),
    ).toBeTruthy();
  });

  test("states clearly when a running game is not using MAKO", () => {
    window.SP_REACT = React;
    render(<RuntimeStatusCard runtimeState={EMPTY_RUNTIME_SCALING_UI_STATE} />);

    expect(screen.getByText("Waiting for MAKO")).toBeTruthy();
    expect(screen.getByText(/running game is not using MAKO yet/)).toBeTruthy();
  });
});
