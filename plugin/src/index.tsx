import { staticClasses } from "@decky/ui";
import { definePlugin } from "@decky/api";
import { GiSharkFin } from "react-icons/gi";
import { Content } from "./components/Content";
import { registerShortcutRecording } from "./utils/frameRecording";
import { forgetPanel } from "./hooks/panelSnapshot";

export default definePlugin(() => {
  console.log("MAKO Decky initializing");
  const unregisterShortcut = registerShortcutRecording();

  return {
    name: "MAKO Decky",
    titleView: <div className={staticClasses.Title}>MAKO Decky</div>,
    alwaysRender: true,
    content: <Content />,
    icon: <GiSharkFin />,
    onDismount() {
      unregisterShortcut();
      // Decky unloads the plugin to update, reinstall, remove or reload it.
      forgetPanel();
      console.log("MAKO Decky unloading");
    },
  };
});
