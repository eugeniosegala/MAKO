import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import React from "react";
import { afterEach, beforeEach, expect, test, vi } from "vitest";
import { RemotePlaySection } from "../../src/components/RemotePlaySection";
import { InfoHiddenContext } from "../../src/components/MakoInfo";
import {
  getRemotePlayStatus,
  installRemotePlayOverride,
  removeRemotePlayOverride,
} from "../../src/api/makoApi";
vi.mock("@decky/ui", () => ({
  PanelSectionRow: ({ children }: any) => <div>{children}</div>,
  ButtonItem: ({ children, onClick, disabled }: any) => (
    <button onClick={onClick} disabled={disabled}>
      {children}
    </button>
  ),
}));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));
vi.mock("../../src/api/makoApi", () => ({
  getRemotePlayStatus: vi.fn(),
  installRemotePlayOverride: vi.fn(),
  removeRemotePlayOverride: vi.fn(),
}));
const idle = {
  success: true,
  message: "",
  error: null,
  installed: false,
  managed: false,
  available: true,
  running: false,
  frame_generation_active: false,
  profile_name: "Remote-Play",
  pids: [],
  conflict: false,
};
beforeEach(() => {
  window.SP_REACT = React;
  vi.mocked(getRemotePlayStatus).mockResolvedValue({ ...idle });
  vi.mocked(installRemotePlayOverride).mockResolvedValue({
    ...idle,
    installed: true,
    managed: true,
  });
  vi.mocked(removeRemotePlayOverride).mockResolvedValue({ ...idle });
});
afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  vi.useRealTimers();
});
test("flushes edits, installs, refreshes profile lists and restores with danger styling", async () => {
  const onPrepare = vi.fn(async () => undefined);
  const onChanged = vi.fn();
  render(<RemotePlaySection onPrepare={onPrepare} onChanged={onChanged} />);
  const install = await screen.findByRole("button", {
    name: "Override Remote Play",
  });
  await waitFor(() =>
    expect((install as HTMLButtonElement).disabled).toBe(false),
  );
  fireEvent.click(install);
  const remove = await screen.findByRole("button", {
    name: "Remove Remote Play Override",
  });
  expect(remove.parentElement?.className).toContain("Mako_BrandButton--danger");
  expect(onPrepare).toHaveBeenCalledOnce();
  expect(onChanged).toHaveBeenCalledOnce();
  fireEvent.click(remove);
  await waitFor(() => expect(removeRemotePlayOverride).toHaveBeenCalledOnce());
  await screen.findByRole("button", { name: "Override Remote Play" });
});
test("exact Renderer evidence reports active FG and blocks removal during streaming", async () => {
  vi.mocked(getRemotePlayStatus).mockResolvedValue({
    ...idle,
    managed: true,
    installed: true,
    running: true,
    frame_generation_active: true,
    pids: [42],
  });
  render(<RemotePlaySection />);
  await screen.findByText("Remote Play — frame generation active");
  expect(
    (
      screen.getByRole("button", {
        name: "Remove Remote Play Override",
      }) as HTMLButtonElement
    ).disabled,
  ).toBe(true);
});
test("R1 hides help but retains native-client availability and action errors", async () => {
  vi.mocked(getRemotePlayStatus).mockResolvedValue({
    ...idle,
    available: false,
  });
  render(
    <InfoHiddenContext.Provider value={true}>
      <RemotePlaySection />
    </InfoHiddenContext.Provider>,
  );
  await screen.findByText("Native Steam streaming client unavailable");
  expect(screen.queryByText(/Uses the Remote Play profile/)).toBeNull();
  expect(
    (
      screen.getByRole("button", {
        name: "Override Remote Play",
      }) as HTMLButtonElement
    ).disabled,
  ).toBe(true);
});
test("Steam-updated managed override can still be removed", async () => {
  vi.mocked(getRemotePlayStatus).mockResolvedValue({
    ...idle,
    managed: true,
    conflict: true,
    message: "Steam updated",
  });
  render(<RemotePlaySection />);
  const remove = await screen.findByRole("button", {
    name: "Remove Remote Play Override",
  });
  await waitFor(() =>
    expect((remove as HTMLButtonElement).disabled).toBe(false),
  );
  fireEvent.click(remove);
  await waitFor(() => expect(removeRemotePlayOverride).toHaveBeenCalledOnce());
});
test("a stale poll cannot overwrite a successful installation", async () => {
  vi.useFakeTimers();
  let resolvePoll!: (value: typeof idle) => void;
  vi.mocked(getRemotePlayStatus)
    .mockResolvedValueOnce({ ...idle })
    .mockReturnValueOnce(
      new Promise((resolve) => {
        resolvePoll = resolve;
      }),
    );
  render(<RemotePlaySection />);
  await act(async () => Promise.resolve());
  await act(async () => vi.advanceTimersByTimeAsync(2000));
  fireEvent.click(screen.getByRole("button", { name: "Override Remote Play" }));
  await act(async () => Promise.resolve());
  expect(
    screen.getByRole("button", { name: "Remove Remote Play Override" }),
  ).toBeTruthy();
  await act(async () => resolvePoll({ ...idle }));
  expect(
    screen.getByRole("button", { name: "Remove Remote Play Override" }),
  ).toBeTruthy();
});
test("failed pending save prevents override mutation", async () => {
  render(
    <RemotePlaySection
      onPrepare={async () => {
        throw new Error("save failed");
      }}
    />,
  );
  const install = await screen.findByRole("button", {
    name: "Override Remote Play",
  });
  await waitFor(() =>
    expect((install as HTMLButtonElement).disabled).toBe(false),
  );
  fireEvent.click(install);
  await screen.findByText("Error: save failed");
  expect(installRemotePlayOverride).not.toHaveBeenCalled();
});

test("loading status does not report the override as disabled before the first response", async () => {
  vi.mocked(getRemotePlayStatus).mockReturnValue(new Promise(() => {}));
  render(<RemotePlaySection />);
  expect(screen.getByRole("status").textContent).toContain(
    "Checking Remote Play…",
  );
  expect(screen.queryByText("Remote Play override not installed")).toBeNull();
});

test.each([
  [{ ...idle }, "Remote Play override not installed"],
  [
    { ...idle, managed: true, installed: true },
    "Override installed — waiting for a stream",
  ],
  [
    { ...idle, managed: true, installed: true, running: true },
    "Remote Play running — frame generation inactive",
  ],
])(
  "override state remains in the status panel when info is hidden: %s",
  async (result, label) => {
    vi.mocked(getRemotePlayStatus).mockResolvedValue(result);
    render(
      <InfoHiddenContext.Provider value={true}>
        <RemotePlaySection />
      </InfoHiddenContext.Provider>,
    );
    await waitFor(() =>
      expect(screen.getByRole("status").textContent).toContain(label),
    );
    expect(screen.queryByText(/Uses the Remote Play profile/)).toBeNull();
  },
);

test("a conflicting runtime cannot show a healthy active status and keeps its recovery message visible", async () => {
  vi.mocked(getRemotePlayStatus).mockResolvedValue({
    ...idle,
    installed: true,
    managed: true,
    running: true,
    frame_generation_active: true,
    conflict: true,
    message: "Remote Play uses another configuration. Edit it in Qt.",
  });
  render(
    <InfoHiddenContext.Provider value={true}>
      <RemotePlaySection />
    </InfoHiddenContext.Provider>,
  );
  await waitFor(() =>
    expect(screen.getByRole("status").textContent).toContain("Needs attention"),
  );
  expect(
    screen.queryByText("Remote Play — frame generation active"),
  ).toBeNull();
  expect(screen.getByRole("alert").textContent).toContain("Edit it in Qt.");
});
