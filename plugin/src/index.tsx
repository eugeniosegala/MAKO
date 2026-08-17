import { staticClasses } from "@decky/ui";
import { definePlugin } from "@decky/api";
import { GiSharkFin } from "react-icons/gi";
import { Content } from "./components/Content";

export default definePlugin(() => {
  console.log("MAKO Decky initializing");

  return {
    // Keep the plugin identity aligned with plugin.json while using a more
    // descriptive title inside the Decky UI.
    name: "Mako",
    titleView: <div className={staticClasses.Title}>MAKO - Frame Generation</div>,
    alwaysRender: true,
    content: <Content />,
    icon: <GiSharkFin />,
    onDismount() {
      console.log("MAKO Decky unloading");
    }
  };
});
