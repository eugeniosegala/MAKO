import {
  Dropdown,
  Menu,
  MenuItem,
  showContextMenu,
  type SingleDropdownOption,
} from "@decky/ui";
import { useRef, type ComponentProps } from "react";

type ModelOption = SingleDropdownOption & { disabled?: boolean };
type ModelDropdownProps = Omit<ComponentProps<typeof Dropdown>, "rgOptions"> & {
  rgOptions: ModelOption[];
};

/** Steam's Dropdown has no per-option disabled API; use its menu hook for these choices. */
export function ModelDropdown({
  rgOptions,
  onChange,
  menuLabel,
  ...props
}: ModelDropdownProps) {
  const anchor = useRef<HTMLDivElement>(null);
  const select = (option: SingleDropdownOption) => {
    const choice = rgOptions.find((entry) => entry.data === option.data);
    if (choice && !choice.disabled) onChange?.(choice);
  };
  return (
    <div ref={anchor} style={{ width: "100%" }}>
      <Dropdown
        {...props}
        menuLabel={menuLabel}
        rgOptions={rgOptions}
        onChange={select}
        onMenuWillOpen={(openMenu) => {
          if (!rgOptions.some((option) => option.disabled)) return openMenu();
          showContextMenu(
            <Menu label={menuLabel ?? ""}>
              {rgOptions.map((option) => (
                <MenuItem
                  key={String(option.data)}
                  disabled={option.disabled}
                  selected={option.data === props.selectedOption}
                  onClick={() => select(option)}
                >
                  {option.label}
                </MenuItem>
              ))}
            </Menu>,
            anchor.current ?? undefined,
          );
        }}
      />
    </div>
  );
}
