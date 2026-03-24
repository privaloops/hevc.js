/**
 * hls.js HEVC Plugin — Enables HEVC playback in hls.js via WASM transcoding.
 *
 * Patches MSE so hls.js can play HEVC HLS streams on browsers without
 * native HEVC support. The same MSE intercept as dashjs — hls.js and
 * dash.js both use MediaSource/SourceBuffer identically.
 *
 * Usage:
 * ```ts
 * import Hls from 'hls.js';
 * import { attachHevcSupport } from 'hevc.js/hlsjs';
 *
 * const hls = new Hls();
 * attachHevcSupport(hls);
 * hls.attachMedia(videoElement);
 * hls.loadSource('https://example.com/stream.m3u8');
 * ```
 */

import { H264Encoder, installMSEIntercept, uninstallMSEIntercept } from "@hevcjs/core";
import type { SegmentTranscoderConfig } from "@hevcjs/core";

export interface HevcHlsPluginConfig extends SegmentTranscoderConfig {
  /**
   * Force HEVC transcoding even if the browser supports HEVC natively.
   * Useful for testing. Default: false.
   */
  forceTranscode?: boolean;
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type HlsInstance = any;

/**
 * Attach HEVC transcoding support to an hls.js instance.
 *
 * Must be called BEFORE hls.loadSource().
 * Patches MediaSource globally to intercept HEVC segments and transcode
 * them to H.264 transparently.
 *
 * @param hls hls.js instance (new Hls())
 * @param config Optional transcoder configuration
 * @returns A cleanup function to remove the plugin
 */
export function attachHevcSupport(
  _hls: HlsInstance,
  config: HevcHlsPluginConfig = {},
): () => void {
  if (!H264Encoder.isSupported()) {
    console.warn(
      "[hevc.js/hlsjs] WebCodecs VideoEncoder not available. " +
      "HEVC transcoding requires Chrome 94+ or equivalent.",
    );
    return () => {};
  }

  // Install MSE intercept — same as dashjs, patches MediaSource globally
  installMSEIntercept({
    wasmUrl: config.wasmUrl,
    fps: config.fps,
    bitrate: config.bitrate,
  });

  return () => {
    uninstallMSEIntercept();
  };
}
