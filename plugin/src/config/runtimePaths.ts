import { MAKO_WRAPPER_RELATIVE_PATH } from "./configSchema";

// The backend RPC replaces this pre-load fallback with the actual Decky home.
export const DEFAULT_MAKO_WRAPPER_PATH = `/home/deck/${MAKO_WRAPPER_RELATIVE_PATH}`;
export const DEFAULT_STEAM_LAUNCH_OPTION = `${DEFAULT_MAKO_WRAPPER_PATH} %command%`;
