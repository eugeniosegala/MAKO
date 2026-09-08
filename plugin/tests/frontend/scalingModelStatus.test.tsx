import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, expect, test, vi } from "vitest";

const checkScalingModel = vi.hoisted(() => vi.fn());
vi.mock("../../src/api/makoApi", () => ({ checkScalingModel }));

import { useScalingModelStatus } from "../../src/hooks/useScalingModelStatus";
import { getDefaults } from "../../src/config/configSchema";

beforeEach(() => {
  vi.useFakeTimers();
  checkScalingModel.mockReset();
  checkScalingModel.mockResolvedValue({ compatible: true, reason: null });
});
afterEach(() => {
  cleanup();
  vi.useRealTimers();
});
const config = () => ({
  ...getDefaults(),
  scaling_enabled: true,
  scaling_method: "ls1",
  dll: "/user/owned.dll",
});
const settle = () => act(() => vi.advanceTimersByTimeAsync(500));

test("checks a saved LS1 selection without writing it and refreshes after DLL updates", async () => {
  const saved = config();
  const original = { ...saved };
  const { result } = renderHook(() => useScalingModelStatus(saved, true));
  expect(result.current).toBeNull();
  checkScalingModel.mockResolvedValue({
    compatible: false,
    reason: "ls1-unavailable",
  });
  await settle();
  expect(result.current).toBe(false);
  expect(checkScalingModel).toHaveBeenCalledWith(
    saved.dll,
    "ls1",
    saved.scaling_sharpness,
  );
  checkScalingModel.mockResolvedValue({ compatible: true, reason: null });
  await act(() => vi.advanceTimersByTimeAsync(30000));
  expect(result.current).toBe(true);
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
    (saved) => useScalingModelStatus(saved, true),
    { initialProps: config() },
  );
  await settle();
  rerender({
    ...config(),
    dll: "/different/profile.dll",
    scaling_method: "ls1-performance",
  });
  expect(result.current).toBeNull();
  await settle();
  expect(result.current).toBe(true);
  await act(async () => resolveOld({ compatible: false }));
  expect(result.current).toBe(true);
});

test("debounces sharpness changes and inspects Ultra Performance's effective model", async () => {
  const { rerender } = renderHook(
    (saved) => useScalingModelStatus(saved, true),
    { initialProps: config() },
  );
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
    (saved) => useScalingModelStatus(saved, true),
    { initialProps: { ...config(), scaling_enabled: false } },
  );
  await settle();
  rerender({ ...config(), scaling_method: "mako" });
  await settle();
  rerender({ ...config(), scaling_method: "native" });
  await settle();
  expect(result.current).toBeNull();
  expect(checkScalingModel).not.toHaveBeenCalled();
});

test("missing inspector RPC is unknown, and unmount stops subsequent polling", async () => {
  checkScalingModel.mockRejectedValue(new Error("old backend"));
  const { result, unmount } = renderHook(() =>
    useScalingModelStatus(config(), true),
  );
  await settle();
  expect(result.current).toBeNull();
  unmount();
  await act(() => vi.advanceTimersByTimeAsync(60000));
  expect(checkScalingModel).toHaveBeenCalledTimes(1);
});
