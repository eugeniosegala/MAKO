import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

const mocks = vi.hoisted(() => ({
  copyWithVerification: vi.fn(),
  showClipboardErrorToast: vi.fn(),
}));

vi.mock("../../src/utils/clipboardUtils", () => ({
  copyWithVerification: mocks.copyWithVerification,
}));
vi.mock("../../src/utils/toastUtils", () => ({
  showClipboardErrorToast: mocks.showClipboardErrorToast,
}));

import { useClipboardFeedback } from "../../src/hooks/useClipboardFeedback";

describe("clipboard feedback", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
    cleanup();
  });

  test("locks copying while work is pending and keeps success visible for three seconds", async () => {
    let finishCopy!: (result: { success: boolean; verified: boolean }) => void;
    mocks.copyWithVerification.mockReturnValue(
      new Promise((resolve) => {
        finishCopy = resolve;
      }),
    );
    const getText = vi.fn().mockResolvedValue("launch option");
    const log = vi.spyOn(console, "log").mockImplementation(() => undefined);
    const { result } = renderHook(() => useClipboardFeedback(getText));

    let operation!: Promise<void>;
    act(() => {
      operation = result.current.copyToClipboard();
    });

    expect(result.current.isLoading).toBe(true);
    expect(result.current.showSuccess).toBe(false);
    await act(() => result.current.copyToClipboard());
    expect(getText).toHaveBeenCalledOnce();

    await act(async () => {
      finishCopy({ success: true, verified: false });
      await operation;
    });

    expect(result.current.isLoading).toBe(false);
    expect(result.current.showSuccess).toBe(true);
    expect(mocks.copyWithVerification).toHaveBeenCalledWith("launch option");
    expect(mocks.showClipboardErrorToast).not.toHaveBeenCalled();
    expect(log).toHaveBeenCalledWith(
      "Copy verification failed but copy likely worked",
    );

    await act(async () => {
      await vi.advanceTimersByTimeAsync(2999);
    });
    expect(result.current.showSuccess).toBe(true);

    await act(async () => {
      await vi.advanceTimersByTimeAsync(1);
    });
    expect(result.current.showSuccess).toBe(false);
  });

  test("retains provider and clipboard failure behavior", async () => {
    const providerError = new Error("launch option unavailable");
    const getText = vi.fn().mockRejectedValue(providerError);
    const { result, rerender } = renderHook(
      ({ provider }: { provider: () => Promise<string> }) =>
        useClipboardFeedback(provider),
      { initialProps: { provider: getText } },
    );

    await act(() => result.current.copyToClipboard());
    expect(mocks.copyWithVerification).not.toHaveBeenCalled();
    expect(mocks.showClipboardErrorToast).toHaveBeenCalledOnce();

    mocks.showClipboardErrorToast.mockClear();
    const successfulProvider = vi.fn().mockResolvedValue("fgmod launch option");
    mocks.copyWithVerification.mockResolvedValue({
      success: false,
      verified: false,
    });
    rerender({ provider: successfulProvider });

    await act(() => result.current.copyToClipboard());
    expect(mocks.copyWithVerification).toHaveBeenCalledWith(
      "fgmod launch option",
    );
    expect(mocks.showClipboardErrorToast).toHaveBeenCalledOnce();
    expect(result.current.showSuccess).toBe(false);
  });
});
