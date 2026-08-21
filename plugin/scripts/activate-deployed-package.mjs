#!/usr/bin/env node

import {
  callDeckyRoute,
  installedPackageVersion,
  resolvePluginName,
} from "./decky-loader-client.mjs";

const pluginName = resolvePluginName(process.argv[2]);

async function waitForDeployedVersion(expectedVersion) {
  const deadline = Date.now() + 30_000;
  while (Date.now() < deadline) {
    const plugins = await callDeckyRoute("loader/get_plugins", [], {
      operation: "report the reloaded MAKO Decky version",
    });
    const loaded = Array.isArray(plugins)
      ? plugins.find((plugin) => plugin?.name === pluginName)
      : undefined;
    if (loaded?.version === expectedVersion) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
  throw new Error(
    `Decky did not load MAKO Decky package version ${expectedVersion} within 30 seconds`,
  );
}

async function activateDeployedPackage() {
  const expectedVersion = installedPackageVersion();
  if (!expectedVersion) {
    throw new Error("The deployed MAKO Decky package has no readable version");
  }
  await callDeckyRoute("loader/reload_plugin", [pluginName], {
    operation: "reload the deployed MAKO Decky package",
  });
  await waitForDeployedVersion(expectedVersion);
  console.log(
    `Reloaded deployed MAKO Decky package: ${pluginName} ${expectedVersion}`,
  );

  const result = await callDeckyRoute(
    "loader/call_plugin_method",
    [pluginName, "install_mako"],
    {
      timeoutMs: 180_000,
      operation: "install the deployed package's MAKO Renderer",
    },
  );
  if (!result || result.success !== true) {
    const detail = result?.error || result?.message || "unknown MAKO Decky error";
    throw new Error(`MAKO Renderer installation failed: ${detail}`);
  }
  console.log(result.message || "Installed MAKO Renderer from the deployed package.");
}

try {
  await activateDeployedPackage();
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
}
