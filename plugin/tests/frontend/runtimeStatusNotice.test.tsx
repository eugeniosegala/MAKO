import React from "react";
import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, test, vi } from "vitest";

vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoCompactSpinner: () => <span>Working</span>,
  makoPanelStyle: {},
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (
    _key: string,
    fallback: string,
    replacements?: Record<string, string | number>,
  ) =>
    Object.entries(replacements ?? {}).reduce(
      (text, [name, value]) =>
        text.split(`{${name}}`).join(String(value)),
      fallback,
    ),
}));

import { RuntimeStatusNotice } from "../../src/components/RuntimeStatusNotice";
import type {
  RuntimeContextState,
  RuntimeProfileSnapshot,
  RuntimeStatusResult,
} from "../../src/api/makoApi";

afterEach(cleanup);

function profile(multiplier: number): RuntimeProfileSnapshot {
  return {
    name: "game-profile",
    gpu: null,
    multiplier,
    frame_generation_enabled: true,
    scaling_enabled: false,
    scaling_method: "mako",
    scaling_factor: 1.5,
    scaling_sharpness: 0.5,
    frame_generation_refresh_threshold: 0,
    base_fps_cap: 0,
    adaptive: false,
    adaptive_auto_base_fps_cap: false,
    target_fps: 90,
    adaptive_max_multiplier: multiplier,
    adaptive_stable_cadence: false,
    dynamic_cadence_recovery: false,
    dynamic_cadence_probe_interval_seconds: 2,
    ultra_performance: false,
    flow_scale: 0.85,
    effective_flow_scale: 0.85,
    performance_mode: false,
    effective_performance_mode: false,
    pacing: "none",
    required_generated_capacity: multiplier - 1,
  };
}

function context(
  phase: RuntimeContextState["phase"],
  error: string | null = null,
): RuntimeContextState {
  return {
    pid: 123,
    process_start_ticks: 456,
    context: 789,
    role: "frame-generation",
    updated_unix_ms: 1000,
    state_revision: 3,
    phase,
    reason: "frame-generation-resources",
    pending: {
      frame_generation_private: phase !== "active",
      spatial_private: false,
      swapchain_recreation: false,
      process_restart: false,
    },
    applied_generated_capacity: 1,
    requested: profile(5),
    applied: profile(2),
    error,
  };
}

function status(
  phase: RuntimeStatusResult["phase"],
  error: string | null = null,
): RuntimeStatusResult {
  return {
    success: true,
    phase,
    contexts: [context(phase === "inactive" ? "active" : phase, error)],
    message: "1 active MAKO Renderer context(s)",
    error: null,
  };
}

describe("Renderer runtime status notice", () => {
  test("shows requested versus applied FG state while rebuilding", () => {
    window.SP_REACT = React;
    render(<RuntimeStatusNotice status={status("draining")} />);

    expect(screen.getByRole("status").dataset.runtimePhase).toBe("draining");
    expect(screen.getByText("Applying live settings…")).toBeTruthy();
    expect(screen.getByText("Working")).toBeTruthy();
    expect(
      screen.getByText(
        "Frame Generation: requested 5x · active 2x · capacity 1",
      ),
    ).toBeTruthy();
  });

  test("reports rollback-safe transition failures", () => {
    window.SP_REACT = React;
    render(
      <RuntimeStatusNotice
        status={status("failed", "replacement allocation failed")}
      />,
    );

    expect(
      screen.getByText(
        "Live update failed; the previous settings remain active",
      ),
    ).toBeTruthy();
    expect(screen.getByText("replacement allocation failed")).toBeTruthy();
  });

  test("does not render an inactive status", () => {
    window.SP_REACT = React;
    const { container } = render(
      <RuntimeStatusNotice
        status={{
          success: true,
          phase: "inactive",
          contexts: [],
          message: "No active MAKO Renderer context",
          error: null,
        }}
      />,
    );

    expect(container.textContent).toBe("");
  });
});
