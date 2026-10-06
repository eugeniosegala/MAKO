import { defineConfig } from "vitest/config";

export default defineConfig({
  plugins: [
    {
      name: "mako-release-info-test-fixture",
      resolveId: (id) =>
        id === "virtual:mako-release-info"
          ? "\0virtual:mako-release-info"
          : null,
      load: (id) =>
        id === "\0virtual:mako-release-info"
          ? 'export const currentRelease = { version: "test", codename: "test" };'
          : null,
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
