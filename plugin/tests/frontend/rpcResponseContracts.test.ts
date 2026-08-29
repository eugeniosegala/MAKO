import { describe, expect, test } from "vitest";
import type {
  ConfigResult,
  ConfigUpdateResult,
  DllDetectionResult,
  DllStatsResult,
  FileContentResult,
  FlatpakAppInfo,
  FlatpakExtensionStatus,
  FlatpakOperationResult,
  InstallationResult,
  InstallationStatus,
  ProfileResult,
  ProfilesResult,
  RuntimeStatusResult,
} from "../../src/api/makoApi";

const nullableResponseFixtures = {
  installation: {
    success: true,
    message: "",
    error: null,
    removed_files: null,
  } satisfies InstallationResult,
  installationStatus: {
    installed: false,
    lib_exists: false,
    json_exists: false,
    script_exists: false,
    lib_path: "/tmp/libmako-render.so",
    json_path: "/tmp/VkLayer_MAKO_render.json",
    script_path: "/tmp/mako-run",
    installed_engine_version: null,
    expected_engine_version: null,
    engine_version_known: false,
    engine_update_required: false,
    host_architecture: null,
    host_architecture_supported: false,
    error: null,
  } satisfies InstallationStatus,
  dllDetection: {
    detected: false,
    path: null,
    source: null,
    message: "",
    error: null,
  } satisfies DllDetectionResult,
  dllStats: {
    success: false,
    dll_path: null,
    dll_sha256: null,
    dll_source: null,
    error: null,
  } satisfies DllStatsResult,
  config: {
    success: false,
    config: null,
    message: "",
    error: null,
  } satisfies ConfigResult,
  configUpdate: {
    success: false,
    config: null,
    message: "",
    error: null,
  } satisfies ConfigUpdateResult,
  fileContent: {
    success: false,
    content: null,
    error: null,
  } satisfies FileContentResult,
  flatpakStatus: {
    success: true,
    message: "",
    error: null,
    installed_23_08: false,
    installed_24_08: false,
    installed_25_08: false,
  } satisfies FlatpakExtensionStatus,
  flatpakApps: {
    success: false,
    message: "",
    error: null,
    apps: [],
    total_apps: 0,
  } satisfies FlatpakAppInfo,
  flatpakOperation: {
    success: true,
    message: "",
    error: null,
  } satisfies FlatpakOperationResult,
  profiles: {
    success: false,
    profiles: null,
    current_profile: null,
    profile_details: null,
    message: "",
    error: null,
  } satisfies ProfilesResult,
  profile: {
    success: false,
    profile_name: null,
    current_profile: null,
    profile: null,
    changed: null,
    game_running: null,
    message: "",
    error: null,
  } satisfies ProfileResult,
  runtimeStatus: {
    success: true,
    phase: "inactive",
    contexts: [],
    message: "No active MAKO Renderer context",
    error: null,
  } satisfies RuntimeStatusResult,
};

describe("Decky RPC response contracts", () => {
  test("accepts the explicit null fields emitted by the Python backend", () => {
    expect(
      Object.values(nullableResponseFixtures).every((fixture) =>
        Object.values(fixture).some((value) => value === null),
      ),
    ).toBe(true);
  });
});
