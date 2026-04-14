export { attachHevcSupport } from "./plugin.js";
export type { HevcDashPluginConfig } from "./plugin.js";

// Re-export shared MSE utilities from core
export { installMSEIntercept, uninstallMSEIntercept, SegmentTranscoder } from "@hevcjs/core";
export type { SegmentTranscoderConfig, TranscodedInit } from "@hevcjs/core";
