/**
 * Demo entry point — shows how to use @hevcjs/dashjs-plugin with dash.js.
 * Bundled by esbuild into dash-bundle.js for the demo page.
 *
 * In a real project with a bundler (Vite, Webpack), you'd just:
 *   import { attachHevcSupport } from 'hevc.js/dashjs-plugin';
 */
import { attachHevcSupport } from "../packages/dashjs-plugin/src/plugin.js";
import type { HevcDashPluginConfig } from "../packages/dashjs-plugin/src/plugin.js";

// Re-export for the demo HTML
export { attachHevcSupport };
export type { HevcDashPluginConfig };
