import { staticClasses } from "@decky/ui";
import { definePlugin } from "@decky/api";
import { GiSharkFin } from "react-icons/gi";
import { Content } from "./components/Content";

export default definePlugin(() => {
  console.log("MAKO Decky initializing");

  return {
    // Use the short brand label in Decky's plugin list and the descriptive
    // product title inside the plugin UI.
    name: "MAKO",
    titleView: <div className={staticClasses.Title}>MAKO - Frame Generation</div>,
    alwaysRender: true,
    content: <Content />,
    icon: <GiSharkFin />,
    onDismount() {
      console.log("MAKO Decky unloading");
    }
  };
});
