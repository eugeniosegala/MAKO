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
          requestedFactor: 2,
          constraintReason: "variable-surface-memory-budget",
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
    expect(screen.getByText("Render")).toBeTruthy();
    expect(screen.getByText("1440 × 810")).toBeTruthy();
    expect(screen.getByText("Display")).toBeTruthy();
    expect(screen.getByText("1280 × 720")).toBeTruthy();
    expect(screen.queryByText(/→/)).toBeNull();
    expect(screen.getByText("1.50×")).toBeTruthy();
    expect(screen.getByText("Factor")).toBeTruthy();
    const notices = container.querySelectorAll(
      '[data-mako-live-status-notices="true"]',
    );
    expect(notices).toHaveLength(1);
    expect((notices[0] as HTMLElement).style.marginTop).toBe("8px");
    const footer = container.querySelector(
      '[data-mako-live-status-footer="true"]',
    );
    expect(footer).toBeTruthy();
    expect(
      container
        .querySelector('[data-mako-live-status-grid="compact-two-column"]')
        ?.contains(footer),
    ).toBe(false);
    container
      .querySelectorAll('[data-mako-live-status-detail="true"]')
      .forEach((detail) => {
        expect((detail as HTMLElement).style.gridTemplateColumns).toBe(
          "minmax(0, 0.8fr) minmax(0, 1.2fr)",
        );
      });
    expect(screen.queryByText(/Upscaling runs/)).toBeNull();
    expect(screen.getByText("Quality Supersampling active.")).toBeTruthy();
    expect(
      screen.getByText(
        "Requested 2.00×; limited to 1.50× by this GPU's memory safety limit.",
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

  test.each([
    [
      "variable-surface-memory-budget",
      "The requested render resolution exceeds this GPU's memory safety limit. Lower the in-game resolution.",
    ],
    [
      "gamescope-presentation-target-no-headroom",
      "This input already fills the display target. Lower the in-game resolution or enable Quality Supersampling.",
    ],
  ])("explains the %s scaling limit", (inactiveReason, message) => {
    window.SP_REACT = React;
    const { container } = render(
      <RuntimeStatusCard
        runtimeState={{
          ...EMPTY_RUNTIME_SCALING_UI_STATE,
          hasContext: true,
          scalingEnabled: true,
          scalingActivationSupported: true,
          inactiveReason,
        }}
      />,
    );

    expect(screen.getByText(message)).toBeTruthy();
    const footer = screen
      .getByText(message)
      .closest('[data-mako-live-status-footer="true"]');
    expect(footer).toBeTruthy();
    expect(
      container
        .querySelector('[data-mako-live-status-grid="compact-two-column"]')
        ?.contains(footer),
    ).toBe(false);
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
