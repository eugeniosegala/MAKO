import React from "react";
import { expect, test, vi } from "vitest";
import {
  panelGeneration,
  rememberPanel,
  restorePanel,
} from "../../src/hooks/panelSnapshot";

vi.mock("@decky/api", () => ({
  definePlugin: (factory: () => unknown) => factory,
}));
vi.mock("@decky/ui", () => ({ staticClasses: { Title: "title" } }));
vi.mock("../../src/components/Content", () => ({ Content: () => null }));
vi.mock("../../src/utils/frameRecording", () => ({
  registerShortcutRecording: () => () => undefined,
}));

import createPlugin from "../../src/index";

test("Decky unloading the plugin forgets the remembered panel", () => {
  rememberPanel(
    {
      installation: {
        engineUpdateRequired: false,
        installedEngineVersion: null,
        expectedEngineVersion: null,
      },
    },
    panelGeneration(),
  );
  expect(restorePanel(null)).not.toBeNull();
  window.SP_REACT = React;
  const plugin = (
    createPlugin as unknown as () => { onDismount: () => void }
  )();
  plugin.onDismount();
  expect(restorePanel(null)).toBeNull();
  expect(localStorage.getItem("mako-panel-snapshot")).toBeNull();
});
