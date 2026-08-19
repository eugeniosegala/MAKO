import { defineConfig } from "vitest/config";

export default defineConfig({
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
        lines: 40
      },
      include: [
        "src/api/makoApi.ts",
        "src/hooks/useInstallationActions.ts",
        "src/hooks/useMakoHooks.ts",
        "src/hooks/useProfileManagement.ts"
      ]
    }
  }
});
