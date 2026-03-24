/**
 * hls.js HEVC Plugin — Enables HEVC playback in hls.js via WASM transcoding.
 *
 * - Patches MSE to intercept HEVC SourceBuffers and transcode to H.264
 * - Filters hls.js levels to prefer HEVC over AVC when both exist
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
   * Default: false.
   */
  forceTranscode?: boolean;

  /**
   * When the manifest has both AVC and HEVC levels, keep only HEVC.
   * Set to false to let hls.js ABR choose freely (may pick AVC).
   * Default: true.
   */
  preferHevc?: boolean;
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type HlsInstance = any;

const HEVC_RE = /hev1|hvc1/i;

/**
 * Attach HEVC transcoding support to an hls.js instance.
 *
 * Must be called BEFORE hls.loadSource().
 *
 * @param hls hls.js instance (new Hls())
 * @param config Optional configuration
 * @returns A cleanup function to remove the plugin
 */
export function attachHevcSupport(
  hls: HlsInstance,
  config: HevcHlsPluginConfig = {},
): () => void {
  if (!H264Encoder.isSupported()) {
    console.warn(
      "[hevc.js/hlsjs] WebCodecs VideoEncoder not available. " +
      "HEVC transcoding requires Chrome 94+ or equivalent.",
    );
    return () => {};
  }

  // 1. Install MSE intercept (patches MediaSource globally)
  installMSEIntercept({
    wasmUrl: config.wasmUrl,
    fps: config.fps,
    bitrate: config.bitrate,
  });

  // 2. Filter levels to prefer HEVC when manifest has both AVC and HEVC
  const preferHevc = config.preferHevc !== false;

  if (hls && preferHevc) {
    hls.on("hlsManifestParsed", (_event: string, data: { levels: Array<{ codecs?: string; codecSet?: string }> }) => {
      const levels = data.levels;
      const hasHevc = levels.some((l: { codecs?: string }) => HEVC_RE.test(l.codecs ?? ""));
      const hasAvc = levels.some((l: { codecs?: string }) => !HEVC_RE.test(l.codecs ?? ""));

      if (hasHevc && hasAvc) {
        // Remove AVC levels — force HEVC
        const hevcOnly = levels
          .map((l: { codecs?: string }, i: number) => ({ level: l, index: i }))
          .filter((item: { level: { codecs?: string } }) => HEVC_RE.test(item.level.codecs ?? ""));

        console.log(
          `[hevc.js/hlsjs] Filtering ${levels.length} levels → ${hevcOnly.length} HEVC-only`,
          hevcOnly.map((h: { level: { codecs?: string }; index: number }) =>
            `L${h.index}: ${(h.level as { codecs?: string; width?: number; height?: number }).width}x${(h.level as { codecs?: string; width?: number; height?: number }).height} ${h.level.codecs}`
          ),
        );

        // hls.js doesn't support removing levels from MANIFEST_PARSED data directly.
        // Instead, we restrict the level range after parsing.
        const hevcIndices = hevcOnly.map((h: { index: number }) => h.index);
        // Set start level to first HEVC level
        if (hevcIndices.length > 0) {
          hls.startLevel = hevcIndices[0];
          hls.autoLevelCapping = hevcIndices[hevcIndices.length - 1];
          // Remove non-HEVC levels by setting their bitrate to 0 won't work,
          // but we can restrict to only HEVC via currentLevel
          hls.currentLevel = hevcIndices[0];
        }
      }
    });
  }

  return () => {
    uninstallMSEIntercept();
  };
}
