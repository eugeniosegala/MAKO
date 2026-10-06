import { defineConfig } from "vitest/config";

const buildInfoFixtures = new Map([
  [
    "\0virtual:mako-release-info",
    'export const currentRelease = { version: "test", codename: "test" };',
  ],
  [
    "\0mako-dev-build-info-test-fixture",
    "export const localDevelopmentBuildInfo = null;",
  ],
]);

export default defineConfig({
  plugins: [
    {
      name: "mako-build-info-test-fixtures",
      resolveId: (id) => {
        if (id === "virtual:mako-release-info")
          return "\0virtual:mako-release-info";
        if (/\/devBuildInfo\.generated(?:\.ts)?$/.test(id))
          return "\0mako-dev-build-info-test-fixture";
        return null;
      },
      load: (id) => buildInfoFixtures.get(id) ?? null,
    },
  ],
  test: {
    environment: "jsdom",
    include: ["tests/frontend/**/*.test.{ts,tsx}"],
    clearMocks: true,
    restoreMocks: true,
    coverage: {
      provider: "v8",
      reporter: ["text", "json-summary", "html"],
      reportsDirectory: "coverage/frontend",
      thresholds: {
        statements: 40,
        branches: 30,
        functions: 50,
        lines: 40,
      },
      include: [
        "src/api/makoApi.ts",
        "src/hooks/useClipboardFeedback.ts",
        "src/hooks/useInstallationActions.ts",
        "src/hooks/useMakoHooks.ts",
        "src/hooks/usePersistentCollapseState.ts",
        "src/hooks/useProfileEditorModel.ts",
        "src/hooks/useProfileManagement.ts",
        "src/hooks/useProfileSession.ts",
        "src/hooks/useModelStatus.ts",
        "src/components/ModelWarning.tsx",
        "src/components/InfoVisibility.tsx",
        "src/components/MakoInfo.tsx",
      ],
    },
  },
});
