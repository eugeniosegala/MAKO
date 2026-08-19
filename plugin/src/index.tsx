import { staticClasses } from "@decky/ui";
import { definePlugin } from "@decky/api";
import { GiSharkFin } from "react-icons/gi";
import { Content } from "./components/Content";

export default definePlugin(() => {
  console.log("MAKO Decky initializing");

  return {
    name: "MAKO Decky",
    titleView: <div className={staticClasses.Title}>MAKO Decky</div>,
    alwaysRender: true,
    content: <Content />,
    icon: <GiSharkFin />,
    onDismount() {
      console.log("MAKO Decky unloading");
    }
  };
});
