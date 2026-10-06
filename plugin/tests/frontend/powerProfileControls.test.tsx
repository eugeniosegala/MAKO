import React from "react";
import {
  cleanup,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import { afterEach, beforeEach, expect, test, vi } from "vitest";

const mocks = vi.hoisted(() => ({
  setProfilePowerModes: vi.fn(),
  showErrorToast: vi.fn(),
}));
vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
  ToggleField: ({
    label,
    checked,
    disabled,
    onChange,
  }: {
    label: string;
    checked: boolean;
    disabled: boolean;
    onChange: (value: boolean) => void;
  }) => (
    <button disabled={disabled} onClick={() => onChange(!checked)}>
      {label}
    </button>
  ),
  DropdownItem: ({
    label,
    selectedOption,
    disabled,
    rgOptions,
    onChange,
  }: {
    label: string;
    selectedOption: string;
    disabled: boolean;
    rgOptions: { data: string; label: string }[];
    onChange: (option: { data: string }) => void;
  }) => (
    <select
      aria-label={label}
      disabled={disabled}
      value={selectedOption}
      onChange={(event) => onChange({ data: event.target.value })}
    >
      {rgOptions.map((option) => (
        <option key={option.data} value={option.data}>
          {option.label}
        </option>
      ))}
    </select>
  ),
}));
vi.mock("../../src/api/makoApi", () => ({
  setProfilePowerModes: mocks.setProfilePowerModes,
}));
vi.mock("../../src/utils/toastUtils", () => ({
  showErrorToast: mocks.showErrorToast,
}));
vi.mock("../../src/components/MakoInfo", () => ({
  MakoInfo: ({ children }: { children: React.ReactNode }) => (
    <div>{children}</div>
  ),
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));

import { PowerProfileControls } from "../../src/components/PowerProfileControls";

beforeEach(() => {
  vi.clearAllMocks();
  window.SP_REACT = React;
});
afterEach(cleanup);

test("waits for edits before cloning and keeps another profile's editor intact", async () => {
  let finishFlush!: () => void;
  const flushConfigChanges = vi.fn(
    () =>
      new Promise<void>((resolve) => {
        finishFlush = resolve;
      }),
  );
  const loadProfileConfig = vi.fn(async () => undefined);
  mocks.setProfilePowerModes.mockResolvedValue({ success: true });
  const { rerender } = render(
    <PowerProfileControls
      profileName="game"
      enabled={false}
      powerMode="shared"
      powerSource="docked"
      loadProfileConfig={loadProfileConfig}
      flushConfigChanges={flushConfigChanges}
    />,
  );
  fireEvent.click(screen.getByText("Separate power settings"));
  expect(flushConfigChanges).toHaveBeenCalledOnce();
  expect(mocks.setProfilePowerModes).not.toHaveBeenCalled();
  rerender(
    <PowerProfileControls
      profileName="other"
      enabled={false}
      powerMode="shared"
      powerSource="docked"
      loadProfileConfig={loadProfileConfig}
      flushConfigChanges={flushConfigChanges}
    />,
  );
  finishFlush();
  await waitFor(() =>
    expect(mocks.setProfilePowerModes).toHaveBeenCalledWith("game", true),
  );
  expect(loadProfileConfig).not.toHaveBeenCalled();
});

test("edits an explicit power set and reloads canonical settings after disabling", async () => {
  const loadProfileConfig = vi.fn(async () => undefined);
  mocks.setProfilePowerModes.mockResolvedValue({ success: true });
  render(
    <PowerProfileControls
      profileName="game"
      enabled
      powerMode="handheld"
      powerSource="docked"
      loadProfileConfig={loadProfileConfig}
    />,
  );
  expect(screen.getByText(/Current power: AC/)).toBeTruthy();
  fireEvent.change(screen.getByLabelText("Editing settings for"), {
    target: { value: "docked" },
  });
  expect(loadProfileConfig).toHaveBeenCalledWith("game", "docked");
  loadProfileConfig.mockClear();
  fireEvent.click(screen.getByText("Separate power settings"));
  expect(mocks.setProfilePowerModes).toHaveBeenCalledWith("game", false);
  await waitFor(() => expect(loadProfileConfig).toHaveBeenCalledWith("game"));
});

test("reports toggle failures and blocks controls while another set loads", async () => {
  const loadProfileConfig = vi.fn(async () => undefined);
  mocks.setProfilePowerModes.mockResolvedValue({
    success: false,
    error: "write failed",
  });
  const { rerender } = render(
    <PowerProfileControls
      profileName="game"
      enabled={false}
      powerMode="shared"
      powerSource=""
      loadProfileConfig={loadProfileConfig}
    />,
  );
  fireEvent.click(screen.getByText("Separate power settings"));
  await waitFor(() =>
    expect(mocks.showErrorToast).toHaveBeenCalledWith(
      "Failed to update profile config",
      "Error: write failed",
    ),
  );
  expect(loadProfileConfig).not.toHaveBeenCalled();
  rerender(
    <PowerProfileControls
      profileName="game"
      enabled
      disabled
      powerMode="handheld"
      powerSource="handheld"
      loadProfileConfig={loadProfileConfig}
    />,
  );
  expect(
    (screen.getByLabelText("Editing settings for") as HTMLSelectElement)
      .disabled,
  ).toBe(true);
  expect(
    (screen.getByText("Separate power settings") as HTMLButtonElement).disabled,
  ).toBe(true);
});
