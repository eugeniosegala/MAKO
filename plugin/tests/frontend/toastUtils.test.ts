import React from "react";
import { beforeEach, describe, expect, test, vi } from "vitest";

const mocks = vi.hoisted(() => ({
  toast: vi.fn()
}));

vi.mock("@decky/api", () => ({
  toaster: {
    toast: mocks.toast
  }
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback
}));

import { showInstallSuccessToast } from "../../src/utils/toastUtils";

describe("MAKO Renderer installation completion toast", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    window.SP_REACT = React;
  });

  test("recommends a restart for the full three-second countdown", () => {
    showInstallSuccessToast();

    expect(mocks.toast).toHaveBeenCalledWith(expect.objectContaining({
      title: "Installation Complete",
      body: "Restarting your device is recommended.",
      icon: expect.anything(),
      duration: 3000,
      expiration: 3000
    }));
  });
});
