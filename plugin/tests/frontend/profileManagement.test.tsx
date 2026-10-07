import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, expect, test, vi } from "vitest";

const mocks = vi.hoisted(() => ({
  createSelectedProfile: vi.fn(),
  showModal: vi.fn(),
}));
function pass({ children }: any) {
  return <div>{children}</div>;
}
function button({ children, disabled, onClick }: any) {
  return (
    <button disabled={disabled} onClick={onClick}>
      {children}
    </button>
  );
}
vi.mock("@decky/ui", () => ({
  PanelSectionRow: pass,
  Field: pass,
  ModalRoot: pass,
  ConfirmModal: pass,
  ButtonItem: button,
  DialogButton: button,
  TextField: ({ value, onChange }: any) => (
    <input aria-label="Profile name" value={value} onChange={onChange} />
  ),
  Dropdown: ({ disabled }: any) => (
    <select aria-label="Saved profile" disabled={disabled} />
  ),
  showModal: mocks.showModal,
}));
vi.mock("../../src/components/MakoUi", () => ({
  MakoFocusable: pass,
  MakoSectionHeader: pass,
  MakoSectionTail: pass,
  makoDialogButtonStyle: () => ({}),
}));
vi.mock("../../src/components/MakoInfo", () => ({ MakoInfo: pass }));
vi.mock("../../src/i18n/i18n", () => ({
  default: (_key: string, fallback: string) => fallback,
}));
vi.mock("../../src/hooks/usePersistentCollapseState", () => ({
  usePersistentCollapseState: () => [false, vi.fn()],
}));
vi.mock("../../src/hooks/useProfileEditorModel", () => ({
  useProfileEditorModel: () => ({
    selectedProfile: "My profile",
    profileOptions: [],
    isLoading: false,
    createSelectedProfile: mocks.createSelectedProfile,
  }),
}));
import { ProfileManagement } from "../../src/components/ProfileManagement";

beforeEach(() => {
  window.SP_REACT = React;
  vi.clearAllMocks();
});
afterEach(cleanup);

test("a stream can create a manually named profile while selection and deletion stay locked", () => {
  render(<ProfileManagement sessionRunning />);
  expect(screen.getByRole("button", { name: "Create Profile" })).toHaveProperty(
    "disabled",
    false,
  );
  expect(screen.getByLabelText("Saved profile")).toHaveProperty(
    "disabled",
    true,
  );
  expect(screen.getByRole("button", { name: "Delete" })).toHaveProperty(
    "disabled",
    true,
  );
  fireEvent.click(screen.getByRole("button", { name: "Create Profile" }));
  render(mocks.showModal.mock.calls[0][0]);
  fireEvent.change(screen.getByLabelText("Profile name"), {
    target: { value: "  Future streams  " },
  });
  fireEvent.click(screen.getByRole("button", { name: "Create" }));
  expect(mocks.createSelectedProfile).toHaveBeenCalledWith("Future streams");
});

test("ordinary running games retain their existing profile creation lock", () => {
  render(
    <ProfileManagement
      mainRunningApp={{ appid: 123, display_name: "Game" } as any}
      sessionRunning
    />,
  );
  expect(screen.getByRole("button", { name: "Create Profile" })).toHaveProperty(
    "disabled",
    true,
  );
});
