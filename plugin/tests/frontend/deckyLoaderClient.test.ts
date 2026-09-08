import { afterEach, beforeEach, describe, expect, test, vi } from "vitest";

type Listener = (event?: { data?: string }) => void;

class FakeWebSocket {
  static requests: Array<{ route: string; args: unknown[] }> = [];
  static responses = new Map<number, { success: boolean; method: unknown }>();
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
    // Model Decky's response cache across connections, including connections
    // from separate script invocations. A reused ID must expose stale results.
    if (!FakeWebSocket.responses.has(request.id)) {
      FakeWebSocket.responses.set(request.id, {
        success: true,
        method: request.args[1],
      });
    }
    queueMicrotask(() =>
      this.listeners.get("message")?.({
        data: JSON.stringify({
          type: 1,
          id: request.id,
          result: FakeWebSocket.responses.get(request.id),
        }),
      }),
    );
  }

  close() {}
}

describe("Decky Loader script client", () => {
  beforeEach(() => {
    FakeWebSocket.requests = [];
    FakeWebSocket.responses.clear();
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

  test("does not reuse status responses when installing over a new connection", async () => {
    // @ts-expect-error No declaration file is needed for the internal script.
    const { callDeckyRoute } = await import("../../scripts/decky-loader-client.mjs");
    const status = await callDeckyRoute("loader/call_plugin_method", [
      "MAKO - Frame Generation",
      "check_mako_installed",
    ]);
    const install = await callDeckyRoute("loader/call_plugin_method", [
      "MAKO - Frame Generation",
      "install_mako",
    ]);

    expect(status).toEqual({ success: true, method: "check_mako_installed" });
    expect(install).toEqual({ success: true, method: "install_mako" });
  });

  test("does not reuse responses from a previous script invocation", async () => {
    // @ts-expect-error No declaration file is needed for the internal script.
    const firstClient = await import("../../scripts/decky-loader-client.mjs");
    await firstClient.callDeckyRoute("loader/call_plugin_method", [
      "MAKO - Frame Generation",
      "check_mako_installed",
    ]);
    vi.resetModules();
    // @ts-expect-error No declaration file is needed for the internal script.
    const secondClient = await import("../../scripts/decky-loader-client.mjs");
    const result = await secondClient.callDeckyRoute("loader/call_plugin_method", [
      "MAKO - Frame Generation",
      "install_mako",
    ]);

    expect(result).toEqual({ success: true, method: "install_mako" });
  });
});
