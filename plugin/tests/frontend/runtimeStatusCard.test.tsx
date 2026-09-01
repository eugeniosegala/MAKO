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
          frameGenerationAdaptiveStyle: "fractional",
          frameGenerationTargetFps: 120,
          frameGenerationMultiplier: 3,
          frameGenerationPending: true,
          scalingActive: true,
          scalingEnabled: true,
          sourceWidth: 960,
          sourceHeight: 540,
          presentationWidth: 1440,
          presentationHeight: 810,
          gamescopeTargetWidth: 1280,
          gamescopeTargetHeight: 720,
          requestedMethod: "ls1",
          activeMethod: "mako",
          effectiveFactor: 1.5,
          pipeline: "pre-frame-generation",
          supersamplingActive: true,
          fallbackReason: "translator unavailable",
          scalingPending: true,
        }}
      />,
    );

    expect(screen.getByText("MAKO is active")).toBeTruthy();
    expect(
      screen
        .getByLabelText("Live Status")
        .closest('[data-mako-section-tail="true"]'),
    ).toBeTruthy();
    expect(
      container.querySelector(
        '[data-mako-live-status-grid="compact-two-column"]',
      ),
    ).toBeTruthy();
    expect(screen.getByText("Mode")).toBeTruthy();
    expect(screen.getByText("Adaptive")).toBeTruthy();
    expect(screen.getByText("Style")).toBeTruthy();
    expect(screen.getByText("Fractional")).toBeTruthy();
    expect(screen.getByText("Target")).toBeTruthy();
    expect(screen.getByText("120 FPS")).toBeTruthy();
    expect(screen.getByText("Max factor")).toBeTruthy();
    expect(screen.getByText("3×")).toBeTruthy();
    expect(screen.getByText("Model")).toBeTruthy();
    expect(screen.getByText("MAKO Scaler")).toBeTruthy();
    expect(screen.getByText("Input")).toBeTruthy();
    expect(screen.getByText("960 × 540")).toBeTruthy();
    expect(screen.getByText("Output")).toBeTruthy();
    expect(screen.getByText("1440 × 810 → 1280 × 720")).toBeTruthy();
    expect(screen.getByText("1.50×")).toBeTruthy();
    expect(screen.getByText("Factor")).toBeTruthy();
    const notices = container.querySelectorAll(
      '[data-mako-live-status-notices="true"]',
    );
    expect(notices).toHaveLength(2);
    notices.forEach((notice) => {
      expect((notice as HTMLElement).style.marginTop).toBe("8px");
    });
    expect(screen.queryByText(/Upscaling runs/)).toBeNull();
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

  test("keeps the compact output resolution unchanged without supersampling", () => {
    window.SP_REACT = React;
    render(
      <RuntimeStatusCard
        runtimeState={{
          ...EMPTY_RUNTIME_SCALING_UI_STATE,
          hasContext: true,
          scalingActive: true,
          scalingEnabled: true,
          sourceWidth: 960,
          sourceHeight: 540,
          presentationWidth: 1440,
          presentationHeight: 810,
          effectiveFactor: 1.5,
        }}
      />,
    );

    expect(screen.getByText("1440 × 810")).toBeTruthy();
    expect(screen.queryByText(/1440 × 810 →/)).toBeNull();
  });

  test("states clearly when a running game is not using MAKO", () => {
    window.SP_REACT = React;
    render(<RuntimeStatusCard runtimeState={EMPTY_RUNTIME_SCALING_UI_STATE} />);

    expect(screen.getByText("Waiting for MAKO")).toBeTruthy();
    expect(screen.getByText(/running game is not using MAKO yet/)).toBeTruthy();
  });

  test("states when scaling is unavailable for the running surface", () => {
    window.SP_REACT = React;
    render(
      <RuntimeStatusCard
        runtimeState={{
          ...EMPTY_RUNTIME_SCALING_UI_STATE,
          hasContext: true,
          scalingEnabled: true,
          scalingActivationSupported: false,
        }}
      />,
    );

    expect(
      screen.getByText("Unavailable for this running surface."),
    ).toBeTruthy();
  });
});
