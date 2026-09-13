import type { PanelSectionRow } from "@decky/ui";
import { createContext, useContext, type HTMLAttributes } from "react";

export const InfoHiddenContext = createContext(false);

/** Unmount informational rows and controls so Steam removes their focus targets. */
export function MakoInfo({
  as: Component = "div",
  ...props
}: HTMLAttributes<HTMLElement> & {
  as?: "div" | "span" | typeof PanelSectionRow;
}) {
  return useContext(InfoHiddenContext) ? null : <Component {...props} />;
}
