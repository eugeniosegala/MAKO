import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";
import { usePersistentCollapseState } from "../../src/hooks/usePersistentCollapseState";

describe("persistent collapse state", () => {
  beforeEach(() => {
    localStorage.clear();
  });

  afterEach(cleanup);

  test("restores the existing boolean and writes to the same storage key", () => {
    localStorage.setItem("mako-example-collapsed", "true");
    const { result } = renderHook(() =>
      usePersistentCollapseState(
        "mako-example-collapsed",
        false,
        "example",
      ),
    );

    expect(result.current[0]).toBe(true);

    act(() => result.current[1](false));
    expect(localStorage.getItem("mako-example-collapsed")).toBe("false");
  });

  test("falls back to the product default when stored JSON is damaged", () => {
    localStorage.setItem("mako-example-collapsed", "not-json");

    const { result } = renderHook(() =>
      usePersistentCollapseState(
        "mako-example-collapsed",
        true,
        "example",
      ),
    );

    expect(result.current[0]).toBe(true);
    expect(localStorage.getItem("mako-example-collapsed")).toBe("true");
  });

  test("keeps controls usable and retains the established warning on write failure", () => {
    const warning = vi.spyOn(console, "warn").mockImplementation(() => {});
    const writeFailure = new Error("storage unavailable");
    const storageWrite = vi
      .spyOn(Storage.prototype, "setItem")
      .mockImplementation(() => {
        throw writeFailure;
      });

    const { result } = renderHook(() =>
      usePersistentCollapseState(
        "mako-example-collapsed",
        false,
        "example",
      ),
    );
    act(() => result.current[1](true));

    expect(result.current[0]).toBe(true);
    expect(warning).toHaveBeenLastCalledWith(
      "Failed to save example collapse state:",
      writeFailure,
    );
    storageWrite.mockRestore();
  });
});
