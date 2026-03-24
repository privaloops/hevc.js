/**
 * Demo entry point — shows how to use @hevcjs/dash with dash.js.
 * Bundled by esbuild into dash-bundle.js for the demo page.
 *
 * In a real project with a bundler (Vite, Webpack), you'd just:
 *   import { attachHevcSupport } from 'hevc.js/dash';
 */
import { attachHevcSupport } from "../packages/dashjs/src/plugin.js";
import type { HevcDashPluginConfig } from "../packages/dashjs/src/plugin.js";

// Re-export for the demo HTML
export { attachHevcSupport };
export type { HevcDashPluginConfig };
