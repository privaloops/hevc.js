// Auto-registers HEVC SourceHandler on Video.js Html5 tech at import time
import "./source-handler.js";

export { HevcWasmTech } from "./tech.js";
export { HEVCSourceHandler } from "./source-handler.js";

// Re-export shared modules from core
export { FMP4Demuxer } from "@hevcjs/core";
export type { DemuxedSample } from "@hevcjs/core";
export { FrameRenderer } from "@hevcjs/core";
export { H264Encoder } from "@hevcjs/core";
export type { H264EncoderConfig, EncodedChunk } from "@hevcjs/core";
export { FMP4Muxer } from "@hevcjs/core";
export type { MuxerInitConfig, MuxerSample } from "@hevcjs/core";
export { MSEController } from "@hevcjs/core";
export { TranscodePipeline } from "@hevcjs/core";
export type { TranscodePipelineConfig } from "@hevcjs/core";
