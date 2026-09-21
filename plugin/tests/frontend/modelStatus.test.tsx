import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, expect, test, vi } from "vitest";

const checkScalingModel = vi.hoisted(() => vi.fn());
const checkFrameGenerationModel = vi.hoisted(() => vi.fn());
vi.mock("../../src/api/makoApi", () => ({
  checkScalingModel,
  checkFrameGenerationModel,
}));

import { useModelStatus } from "../../src/hooks/useModelStatus";
import { getDefaults } from "../../src/config/configSchema";

beforeEach(() => {
  vi.useFakeTimers();
  checkScalingModel.mockReset();
  checkFrameGenerationModel.mockReset();
  checkFrameGenerationModel.mockResolvedValue({
    compatible: true,
    reason: null,
  });
  checkScalingModel.mockResolvedValue({ compatible: true, reason: null });
});
afterEach(() => {
  cleanup();
  vi.useRealTimers();
});
const config = () => ({
  ...getDefaults(),
  scaling_enabled: true,
  frame_generation_enabled: false,
  scaling_method: "ls1",
  dll: "/user/owned.dll",
});
const settle = () => act(() => vi.advanceTimersByTimeAsync(500));

test("checks a saved LS1 selection without writing it and refreshes after DLL updates", async () => {
  const saved = config();
  const original = { ...saved };
  const { result } = renderHook(() => useModelStatus(saved, true));
  expect(result.current.ls1).toBeNull();
  checkScalingModel.mockResolvedValue({
    compatible: false,
    reason: "ls1-unavailable",
  });
  await settle();
  expect(result.current.ls1?.compatible).toBe(false);
  expect(checkScalingModel).toHaveBeenCalledWith(
    saved.dll,
    "ls1",
    saved.scaling_sharpness,
  );
  checkScalingModel.mockResolvedValue({ compatible: true, reason: null });
  await act(() => vi.advanceTimersByTimeAsync(30000));
  expect(result.current.ls1?.compatible).toBe(true);
  expect(saved).toEqual(original);
});

test("late replies cannot attach a previous profile's result to the selected model", async () => {
  let resolveOld!: (result: { compatible: boolean }) => void;
  checkScalingModel.mockReturnValueOnce(
    new Promise((resolve) => {
      resolveOld = resolve;
    }),
  );
  const { result, rerender } = renderHook(
    (saved) => useModelStatus(saved, true),
    { initialProps: config() },
  );
  await settle();
  rerender({
    ...config(),
    dll: "/different/profile.dll",
    scaling_method: "ls1-performance",
  });
  expect(result.current.ls1).toBeNull();
  await settle();
  expect(result.current.ls1?.compatible).toBe(true);
  await act(async () => resolveOld({ compatible: false }));
  expect(result.current.ls1?.compatible).toBe(true);
});

test("debounces sharpness changes and inspects Ultra Performance's effective model", async () => {
  const { rerender } = renderHook((saved) => useModelStatus(saved, true), {
    initialProps: config(),
  });
  rerender({ ...config(), scaling_sharpness: 0.25 });
  rerender({ ...config(), scaling_sharpness: 0.75, ultra_performance: true });
  await settle();
  expect(checkScalingModel).toHaveBeenCalledTimes(1);
  expect(checkScalingModel).toHaveBeenCalledWith(
    config().dll,
    "ls1-performance",
    0.75,
  );
});

test("model-free choices and disabled scaling never start a probe", async () => {
  const { result, rerender } = renderHook(
    (saved) => useModelStatus(saved, true),
    { initialProps: { ...config(), scaling_enabled: false } },
  );
  await settle();
  rerender({ ...config(), scaling_method: "mako" });
  await settle();
  rerender({ ...config(), scaling_method: "native" });
  await settle();
  expect(result.current.ls1).toBeNull();
  expect(checkScalingModel).not.toHaveBeenCalled();
});

test("missing inspector RPC is unknown, and unmount stops subsequent polling", async () => {
  checkScalingModel.mockRejectedValue(new Error("old backend"));
  const { result, unmount } = renderHook(() => useModelStatus(config(), true));
  await settle();
  expect(result.current.ls1).toBeNull();
  unmount();
  await act(() => vi.advanceTimersByTimeAsync(60000));
  expect(checkScalingModel).toHaveBeenCalledTimes(1);
});

test("checks enabled families sequentially and keeps their failures independent", async () => {
  checkFrameGenerationModel.mockResolvedValue({
    compatible: false,
    reason: "lsfg-unavailable",
  });
  const { result, rerender } = renderHook(
    (saved) => useModelStatus(saved, true),
    {
      initialProps: { ...config(), frame_generation_enabled: true },
    },
  );
  await settle();
  expect(result.current.ls1?.compatible).toBe(true);
  expect(result.current.lsfg?.compatible).toBe(false);
  expect(checkFrameGenerationModel).toHaveBeenCalledWith(config().dll, true);
  expect(checkFrameGenerationModel.mock.invocationCallOrder[0]).toBeLessThan(
    checkScalingModel.mock.invocationCallOrder[0],
  );
  rerender({ ...config(), frame_generation_enabled: true, allow_fp16: false });
  expect(result.current.lsfg).toBeNull();
  await settle();
  expect(checkFrameGenerationModel).toHaveBeenLastCalledWith(
    config().dll,
    false,
  );
  rerender({
    ...config(),
    frame_generation_enabled: true,
    allow_fp16: false,
    ultra_performance: true,
  });
  await settle();
  expect(checkFrameGenerationModel).toHaveBeenLastCalledWith(
    config().dll,
    true,
  );
  rerender({ ...config(), frame_generation_enabled: false });
  expect(result.current.lsfg).toBeNull();
  rerender({
    ...config(),
    frame_generation_provisioned: false,
    frame_generation_enabled: true,
  });
  await settle();
  expect(result.current.lsfg).toBeNull();
  expect(checkFrameGenerationModel).toHaveBeenCalledTimes(3);
});

test("a disabled Renderer and disabled features never probe or retain warnings", async () => {
  const { result, rerender } = renderHook(
    (saved) => useModelStatus(saved, true),
    {
      initialProps: {
        ...config(),
        disable_mako: true,
        frame_generation_enabled: true,
      },
    },
  );
  await settle();
  expect(checkScalingModel).not.toHaveBeenCalled();
  expect(checkFrameGenerationModel).not.toHaveBeenCalled();
  rerender({
    ...config(),
    disable_mako: false,
    scaling_enabled: false,
    frame_generation_enabled: false,
  });
  await settle();
  expect(result.current).toEqual({ ls1: null, lsfg: null });
  expect(checkFrameGenerationModel).not.toHaveBeenCalled();
});
