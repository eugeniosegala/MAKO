import { ButtonItem, PanelSectionRow } from "@decky/ui";
import { RiArrowDownSFill, RiArrowUpSFill } from "react-icons/ri";

export function CollapseControl({
  containerClassName,
  collapsed,
  onToggle,
}: {
  containerClassName: string;
  collapsed: boolean;
  onToggle: () => void;
}) {
  return (
    <PanelSectionRow>
      <div
        className={containerClassName}
        style={{ marginTop: "2px", marginBottom: "4px" }}
      >
        <ButtonItem layout="below" bottomSeparator="none" onClick={onToggle}>
          {collapsed ? (
            <RiArrowDownSFill
              style={{ transform: "translate(0, -13px)", fontSize: "1.5em" }}
            />
          ) : (
            <RiArrowUpSFill
              style={{ transform: "translate(0, -12px)", fontSize: "1.5em" }}
            />
          )}
        </ButtonItem>
      </div>
    </PanelSectionRow>
  );
}
