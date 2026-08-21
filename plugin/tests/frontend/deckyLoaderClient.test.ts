import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

type Listener = (event?: { data?: string }) => void;

class FakeWebSocket {
  static requests: Array<{ route: string; args: unknown[] }> = [];
  private listeners = new Map<string, Listener>();

  constructor(_url: URL) {
    queueMicrotask(() => this.listeners.get("open")?.());
  }

  addEventListener(name: string, listener: Listener) {
    this.listeners.set(name, listener);
  }

  send(payload: string) {
    const request = JSON.parse(payload) as {
      id: number;
      route: string;
      args: unknown[];
    };
    FakeWebSocket.requests.push({ route: request.route, args: request.args });
    queueMicrotask(() =>
      this.listeners.get("message")?.({
        data: JSON.stringify({
          type: 1,
          id: request.id,
          result: { success: true },
        }),
      }),
    );
  }

  close() {}
}

describe("Decky Loader script client", () => {
  beforeEach(() => {
    FakeWebSocket.requests = [];
    vi.stubGlobal("WebSocket", FakeWebSocket);
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue({
        ok: true,
        text: async () => "test-token",
      }),
    );
  });
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  test("forwards the supported plugin method route and positional arguments", async () => {
    // This small Node-side module is intentionally plain JavaScript so the
    // release runner can use it without a TypeScript build step.
    // @ts-expect-error No declaration file is needed for the internal script.
    const { callDeckyRoute } = await import("../../scripts/decky-loader-client.mjs");

    await callDeckyRoute("loader/call_plugin_method", [
      "MAKO - Frame Generation",
      "install_mako",
    ]);

    expect(FakeWebSocket.requests).toEqual([
      {
        route: "loader/call_plugin_method",
        args: ["MAKO - Frame Generation", "install_mako"],
      },
    ]);
  });
});
