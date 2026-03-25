/**
 * dash.js HEVC Plugin — Enables HEVC playback in dash.js via WASM transcoding.
 *
 * Patches MSE and registers a capabilities filter so dash.js accepts HEVC
 * representations and transparently transcodes them to H.264.
 *
 * Usage:
 * ```ts
 * import dashjs from 'dashjs';
 * import { attachHevcSupport } from 'hevc.js/dash';
 *
 * const player = dashjs.MediaPlayer().create();
 * attachHevcSupport(player, { wasmUrl: '/hevc-decode.js' });
 * player.initialize(videoElement, mpdUrl, true);
 * ```
 */

import { H264Encoder, installMSEIntercept, uninstallMSEIntercept } from "@hevcjs/core";
import type { MSEInterceptConfig } from "@hevcjs/core";

export interface HevcDashPluginConfig extends MSEInterceptConfig {
  /**
   * Force HEVC transcoding even if the browser supports HEVC natively.
   * Useful for testing. Default: false.
   */
  forceTranscode?: boolean;
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type DashMediaPlayer = any;

/**
 * Attach HEVC transcoding support to a dash.js MediaPlayer instance.
 *
 * Must be called BEFORE player.initialize().
 * Always installs the MSE intercept to handle incomplete codec strings
 * (e.g. "hev1" without profile/level) that browsers reject even with
 * native HEVC support.
 *
 * @param player dash.js MediaPlayer instance
 * @param config Optional transcoder configuration
 * @returns A cleanup function to remove the plugin
 */
export function attachHevcSupport(
  player: DashMediaPlayer,
  config: HevcDashPluginConfig = {},
): () => void {
  if (!H264Encoder.isSupported()) {
    console.warn(
      "[hevc.js/dash] WebCodecs VideoEncoder not available. " +
      "HEVC transcoding requires Chrome 94+ or equivalent.",
    );
    return () => {};
  }

  // Async capability probe — detects browsers that expose VideoEncoder
  // but can't actually encode H.264 (e.g. Firefox 133 on Windows 10).
  H264Encoder.checkSupport().then((ok) => {
    if (!ok) {
      console.warn(
        "[hevc.js/dash] VideoEncoder exists but H.264 encoding is not supported. " +
        "Falling back to native playback.",
      );
      uninstallMSEIntercept();
    }
  });

  // 1. Install MSE intercept ALWAYS — patches isTypeSupported + addSourceBuffer.
  // Even on browsers with native HEVC, the MPD may have incomplete codec strings
  // like "hev1" (without profile/level) which MediaSource.isTypeSupported rejects.
  installMSEIntercept({
    wasmUrl: config.wasmUrl,
    fps: config.fps,
    bitrate: config.bitrate,
    workerUrl: config.workerUrl,
  });

  // 2. Register capabilities filter — tell dash.js to accept HEVC representations.
  // This runs AFTER dash.js's internal check, so we also need isTypeSupported patched.
  if (player.registerCustomCapabilitiesFilter) {
    player.registerCustomCapabilitiesFilter(
      (representation: { mimeType?: string; codecs?: string }) => {
        const codecs = (representation.codecs ?? "").toLowerCase();
        const mime = (representation.mimeType ?? "").toLowerCase();
        if (/hev1|hvc1/.test(codecs) || /hev1|hvc1/.test(mime)) {
          return true;
        }
        return true;
      },
    );
  }

  // Return cleanup function
  return () => {
    uninstallMSEIntercept();
  };
}
