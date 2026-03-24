/**
 * Demo entry point for @hevcjs/hlsjs with hls.js.
 * Bundled by esbuild into hls-bundle.js.
 */
import { attachHevcSupport } from "../packages/hlsjs/src/plugin.js";
import type { HevcHlsPluginConfig } from "../packages/hlsjs/src/plugin.js";

export { attachHevcSupport };
export type { HevcHlsPluginConfig };
