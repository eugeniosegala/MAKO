import { readFileSync } from "node:fs";

const DEFAULT_DECKY_URL = "http://127.0.0.1:1337";
const DEFAULT_PLUGIN_NAME = "MAKO - Scaling & Frame Generation";

function installedPluginRoot() {
  return (
    process.env.DECKY_PLUGIN_DIR ||
    `${process.env.HOME}/homebrew/plugins/Mako`
  );
}

export function installedPluginName() {
  const manifestPath =
    process.env.DECKY_PLUGIN_MANIFEST ||
    `${installedPluginRoot()}/plugin.json`;
  try {
    return JSON.parse(readFileSync(manifestPath, "utf8")).name;
  } catch {
    return undefined;
  }
}

export function installedPackageVersion() {
  const packagePath =
    process.env.DECKY_PLUGIN_PACKAGE ||
    `${installedPluginRoot()}/package.json`;
  try {
    const version = JSON.parse(readFileSync(packagePath, "utf8")).version;
    return typeof version === "string" && version ? version : undefined;
  } catch {
    return undefined;
  }
}

export function resolvePluginName(argument) {
  return argument || installedPluginName() || DEFAULT_PLUGIN_NAME;
}

function websocketUrl(baseUrl, token) {
  const url = new URL("/ws", baseUrl);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  url.searchParams.set("auth", token);
  return url;
}

function errorDetail(message) {
  const detail = message.error || message.result || "unknown Decky error";
  if (typeof detail === "string") {
    return detail;
  }
  if (typeof detail?.message === "string") {
    return detail.message;
  }
  return JSON.stringify(detail);
}

export async function callDeckyRoute(
  route,
  args = [],
  { timeoutMs = 15_000, operation = route } = {},
) {
  const deckyUrl = process.env.DECKY_LOADER_URL || DEFAULT_DECKY_URL;
  const tokenResponse = await fetch(new URL("/auth/token", deckyUrl));
  if (!tokenResponse.ok) {
    throw new Error(
      `Decky authentication failed with HTTP ${tokenResponse.status}`,
    );
  }

  const token = (await tokenResponse.text()).trim();
  if (!token) {
    throw new Error("Decky returned an empty authentication token");
  }

  return await new Promise((resolve, reject) => {
    const socket = new WebSocket(websocketUrl(deckyUrl, token));
    const timeout = setTimeout(() => {
      socket.close();
      reject(new Error(`Timed out waiting for Decky to ${operation}`));
    }, timeoutMs);

    const finish = (callback) => {
      clearTimeout(timeout);
      socket.close();
      callback();
    };

    socket.addEventListener("open", () => {
      socket.send(
        JSON.stringify({
          type: 0,
          id: 1,
          route,
          args,
        }),
      );
    });

    socket.addEventListener("message", (event) => {
      let message;
      try {
        message = JSON.parse(String(event.data));
      } catch (error) {
        finish(() => reject(new Error(`Invalid response from Decky: ${error}`)));
        return;
      }
      if (message.id !== 1 || (message.type !== 1 && message.type !== -1)) {
        return;
      }
      if (message.type === -1) {
        finish(() =>
          reject(new Error(`Decky could not ${operation}: ${errorDetail(message)}`)),
        );
        return;
      }
      finish(() => resolve(message.result));
    });

    socket.addEventListener("error", () => {
      finish(() =>
        reject(new Error(`Could not connect to Decky at ${deckyUrl}`)),
      );
    });
  });
}
