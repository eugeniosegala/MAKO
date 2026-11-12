import { act, cleanup, renderHook } from "@testing-library/react";
import { useState } from "react";
import { afterEach, expect, test, vi } from "vitest";
import { useModelFallback } from "../../src/hooks/useModelFallback";
import {
  getDefaults,
  type ConfigurationPatch,
} from "../../src/config/configSchema";

afterEach(cleanup);
const initialOptions = {
  profileName: "game",
  powerMode: "handheld",
  enabled: true,
  dllMissing: true,
  dllDetected: false,
  lsfg: null as { compatible: boolean | null; reason: string | null } | null,
  ls1: null as { compatible: boolean | null; reason: string | null } | null,
  ls1RuntimeFallback: false,
};
function mount(changes: ConfigurationPatch = {}, options = initialOptions) {
  const saved = { ...getDefaults(), ...changes };
  let update!: (changes: ConfigurationPatch) => void;
  const save = vi.fn(async (patch: ConfigurationPatch) => update(patch));
  const view = renderHook(
    (props) => {
      const [config, setConfig] = useState(saved);
      update = (patch) => setConfig((current) => ({ ...current, ...patch }));
      const notice = useModelFallback({
        ...props,
        config,
        saveConfigChanges: save,
      });
      return { config, notice };
    },
    { initialProps: options },
  );
  return {
    ...view,
    save,
    saved,
    update: (patch: ConfigurationPatch) => act(() => update(patch)),
  };
}

test("a missing DLL keeps both saved choices and explains the fallback", () => {
  const view = mount({
    frame_generation_model: "lossless-scaling",
    scaling_method: "ls1-performance",
    scaling_enabled: true,
    performance_mode: true,
    flow_scale: 0.7,
  });
  expect(view.save).not.toHaveBeenCalled();
  expect(view.result.current.config).toEqual(view.saved);
  expect(view.result.current.notice.dllMissing).toBe(true);
  view.rerender({
    ...initialOptions,
    lsfg: { compatible: false, reason: "dll-unavailable" },
    ls1: { compatible: false, reason: "dll-unavailable" },
    ls1RuntimeFallback: true,
  });
  expect(view.save).not.toHaveBeenCalled();
});

test("a detection that also reports the DLL unavailable keeps the saved choice", () => {
  const view = mount(
    { frame_generation_model: "lossless-scaling" },
    {
      ...initialOptions,
      dllMissing: false,
      lsfg: { compatible: false, reason: "dll-unavailable" },
    },
  );
  expect(view.save).not.toHaveBeenCalled();
});

test("MAKO defaults and Native Resolution do not need correction or a missing-DLL warning", () => {
  for (const method of ["native", "mako"]) {
    const view = mount({ scaling_method: method });
    expect(view.save).not.toHaveBeenCalled();
    expect(view.result.current.notice.dllMissing).toBe(false);
    view.unmount();
  }
});

test.each([null, true])(
  "a DLL detection/inspection without confirmed failure (%s) preserves LS selections",
  (compatible) => {
    const view = mount(
      { frame_generation_model: "lossless-scaling", scaling_method: "ls1" },
      {
        ...initialOptions,
        dllMissing: false,
        lsfg: { compatible, reason: null },
        ls1: { compatible, reason: null },
      },
    );
    expect(view.save).not.toHaveBeenCalled();
    expect(view.result.current.config).toEqual(view.saved);
  },
);

test.each(["lsfg", "ls1"] as const)(
  "a failed %s check changes only that family and retains the failed status",
  (family) => {
    const failure = { compatible: false, reason: "unsupported-model" };
    const options = { ...initialOptions, dllMissing: false, [family]: failure };
    const view = mount(
      { frame_generation_model: "lossless-scaling", scaling_method: "ls1" },
      options,
    );
    expect(view.save).toHaveBeenCalledWith(
      family === "lsfg"
        ? { frame_generation_model: "mako-fg" }
        : { scaling_method: "mako" },
    );
    view.rerender({ ...options, [family]: null });
    expect(view.result.current.notice[family]).toEqual(failure);
  },
);

test("a confirmed runtime LS1 fallback selects MAKO Scaler without a successful inspector", () => {
  const view = mount(
    { scaling_method: "ls1" },
    { ...initialOptions, dllMissing: false, ls1RuntimeFallback: true },
  );
  expect(view.save).toHaveBeenCalledWith({ scaling_method: "mako" });
  expect(view.result.current.notice.ls1RuntimeFallback).toBe(true);
});

test("loading blocks automatic edits and fallback notices do not leak between profiles or power modes", () => {
  const failed = {
    ...initialOptions,
    dllMissing: false,
    dllDetected: true,
    lsfg: { compatible: false, reason: "unsupported-model" },
  };
  const view = mount(
    { frame_generation_model: "lossless-scaling" },
    { ...failed, enabled: false },
  );
  expect(view.save).not.toHaveBeenCalled();
  view.rerender(failed);
  expect(view.save).toHaveBeenCalledTimes(1);
  expect(view.result.current.notice.lsfg).toEqual(failed.lsfg);
  view.rerender({ ...failed, lsfg: null, powerMode: "docked" });
  expect(view.result.current.notice.lsfg).toBeNull();
  view.rerender({ ...failed, lsfg: null, profileName: "other" });
  expect(view.result.current.notice.lsfg).toBeNull();
  expect(view.save).toHaveBeenCalledTimes(1);
});

test("reconciled failed saves do not create an automatic write loop", () => {
  const failed = {
    ...initialOptions,
    dllMissing: false,
    dllDetected: true,
    lsfg: { compatible: false, reason: "unsupported-model" },
  };
  const view = mount({ frame_generation_model: "lossless-scaling" }, failed);
  view.update({ frame_generation_model: "lossless-scaling" });
  expect(view.save).toHaveBeenCalledTimes(1);
});

test("a saved choice survives the DLL leaving and returning", () => {
  const view = mount({ frame_generation_model: "lossless-scaling" });
  view.rerender({ ...initialOptions, dllMissing: false, dllDetected: true });
  view.rerender(initialOptions);
  view.rerender({ ...initialOptions, dllMissing: false, dllDetected: true });
  expect(view.save).not.toHaveBeenCalled();
  expect(view.result.current.config.frame_generation_model).toBe(
    "lossless-scaling",
  );
});

test("DLL restoration clears remembered DLL failures after the selection switched", () => {
  const view = mount(
    { frame_generation_model: "lossless-scaling" },
    {
      ...initialOptions,
      lsfg: { compatible: false, reason: "dll-unavailable" },
    },
  );
  expect(view.result.current.notice.lsfg?.compatible).toBe(false);
  view.rerender({ ...initialOptions, dllDetected: true, dllMissing: false });
  expect(view.result.current.notice.dllMissing).toBe(false);
  expect(view.result.current.notice.lsfg).toBeNull();
});
