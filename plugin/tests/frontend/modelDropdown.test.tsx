import React from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, expect, test, vi } from "vitest";

const nativeMenu = vi.hoisted(() => vi.fn());
vi.mock("@decky/ui", () => ({
  Dropdown: ({
    onMenuWillOpen,
    onChange,
    rgOptions,
    disabled,
  }: {
    onMenuWillOpen: (show: () => void) => void;
    onChange: (option: { data: string; label: string }) => void;
    rgOptions: { data: string; label: string }[];
    disabled?: boolean;
  }) => (
    <>
      <button disabled={disabled} onClick={() => onMenuWillOpen(nativeMenu)}>
        Open models
      </button>
      <button onClick={() => onChange(rgOptions[1])}>
        Stale native selection
      </button>
    </>
  ),
  Menu: ({ children, label }: { children: React.ReactNode; label: string }) => (
    <div role="menu" aria-label={label}>
      {children}
    </div>
  ),
  MenuItem: ({
    children,
    selected,
    ...props
  }: React.ButtonHTMLAttributes<HTMLButtonElement> & {
    selected?: boolean;
  }) => (
    <button role="menuitemradio" aria-checked={selected} {...props}>
      {children}
    </button>
  ),
  showContextMenu: (menu: React.ReactNode) => render(<>{menu}</>),
}));

import { ModelDropdown } from "../../src/components/ModelDropdown";
afterEach(cleanup);

test.each([
  ["MAKO FG", "LSFG Quality", "LSFG Performance"],
  ["MAKO Scaler", "LS1 Quality", "LS1 Performance"],
])(
  "disables optional %s alternatives while keeping the built-in choice usable",
  (mako, quality, performance) => {
    window.SP_REACT = React;
    const onChange = vi.fn();
    render(
      <ModelDropdown
        menuLabel="Models"
        selectedOption="mako"
        onChange={onChange}
        rgOptions={[
          { data: "mako", label: mako },
          { data: "quality", label: quality, disabled: true },
          { data: "performance", label: performance, disabled: true },
        ]}
      />,
    );
    fireEvent.click(screen.getByText("Stale native selection"));
    expect(onChange).not.toHaveBeenCalled();
    fireEvent.click(screen.getByText("Open models"));
    expect(nativeMenu).not.toHaveBeenCalled();
    for (const name of [quality, performance]) {
      const item = screen.getByRole("menuitemradio", {
        name,
      }) as HTMLButtonElement;
      expect(item.disabled).toBe(true);
      fireEvent.click(item);
    }
    expect(onChange).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole("menuitemradio", { name: mako }));
    expect(onChange).toHaveBeenCalledWith({ data: "mako", label: mako });
  },
);

test("available Lossless Scaling keeps the native menu and selection behavior", () => {
  window.SP_REACT = React;
  const onChange = vi.fn();
  render(
    <ModelDropdown
      selectedOption="mako"
      onChange={onChange}
      rgOptions={[
        { data: "mako", label: "MAKO FG" },
        { data: "lsfg", label: "LSFG Quality" },
      ]}
    />,
  );
  fireEvent.click(screen.getByText("Open models"));
  expect(nativeMenu).toHaveBeenCalledOnce();
  fireEvent.click(screen.getByText("Stale native selection"));
  expect(onChange).toHaveBeenCalledWith({
    data: "lsfg",
    label: "LSFG Quality",
  });
});
