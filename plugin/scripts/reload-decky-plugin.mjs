#!/usr/bin/env node

const DEFAULT_DECKY_URL = "http://127.0.0.1:1337";
const DEFAULT_PLUGIN_NAME = "Decky MAKO Experimental";
const REQUEST_TIMEOUT_MS = 15_000;

const pluginName = process.argv[2] || DEFAULT_PLUGIN_NAME;
const deckyUrl = process.env.DECKY_LOADER_URL || DEFAULT_DECKY_URL;

function websocketUrl(baseUrl, token) {
  const url = new URL("/ws", baseUrl);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  url.searchParams.set("auth", token);
  return url;
}

async function reloadPlugin() {
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

  await new Promise((resolve, reject) => {
    const socket = new WebSocket(websocketUrl(deckyUrl, token));
    const timeout = setTimeout(() => {
      socket.close();
      reject(new Error("Timed out waiting for Decky to reload the plugin"));
    }, REQUEST_TIMEOUT_MS);

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
          route: "loader/reload_plugin",
          args: [pluginName],
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
        const detail = message.error || message.result || "unknown Decky error";
        finish(() => reject(new Error(`Decky could not reload the plugin: ${detail}`)));
        return;
      }
      finish(resolve);
    });

    socket.addEventListener("error", () => {
      finish(() => reject(new Error(`Could not connect to Decky at ${deckyUrl}`)));
    });
  });

  console.log(`Reloaded Decky plugin: ${pluginName}`);
}

try {
  await reloadPlugin();
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
}
