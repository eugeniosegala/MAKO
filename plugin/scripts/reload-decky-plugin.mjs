#!/usr/bin/env node

import {
  callDeckyRoute,
  resolvePluginName,
} from "./decky-loader-client.mjs";

const pluginName = resolvePluginName(process.argv[2]);

async function reloadPlugin() {
  await callDeckyRoute("loader/reload_plugin", [pluginName], {
    operation: "reload MAKO Decky",
  });
  console.log(`Reloaded MAKO Decky: ${pluginName}`);
}

try {
  await reloadPlugin();
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
}
