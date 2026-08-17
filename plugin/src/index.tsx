import { staticClasses } from "@decky/ui";
import { definePlugin } from "@decky/api";
import { GiSharkFin } from "react-icons/gi";
import { Content } from "./components/Content";

export default definePlugin(() => {
  console.log("MAKO Decky initializing");

  return {
    // Keep the Decky-facing name aligned with plugin.json. MAKO Decky is the
    // official component name; the product is displayed simply as Mako in Decky.
    name: "Mako",
    titleView: <div className={staticClasses.Title}>Mako</div>,
    alwaysRender: true,
    content: <Content />,
    icon: <GiSharkFin />,
    onDismount() {
      console.log("MAKO Decky unloading");
    }
  };
});
