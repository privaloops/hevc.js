/**
 * Video.js HEVC entry point — installs MSE intercept for use with VHS.
 *
 * Unlike the dash.js/hls.js plugins which hook into player-specific APIs,
 * this simply patches MediaSource globally so VHS's HEVC SourceBuffers
 * get transparently transcoded to H.264.
 */
import { H264Encoder, installMSEIntercept, uninstallMSEIntercept } from "@hevcjs/core";
import type { MSEInterceptConfig } from "@hevcjs/core";

export interface HevcVideojsConfig extends MSEInterceptConfig {
  forceTranscode?: boolean;
  /**
   * When true, block AVC video renditions so VHS only selects HEVC.
   * Useful for streams with both AVC and HEVC variants.
   * Default: same as forceTranscode.
   */
  forceHevcRenditions?: boolean;
}

function hasNativeHevcSupport(): boolean {
  try {
    return MediaSource.isTypeSupported('video/mp4; codecs="hev1.1.6.L93.B0"');
  } catch {
    return false;
  }
}

export async function attachHevcSupport(
  config: HevcVideojsConfig = {},
): Promise<(() => void) | null> {
  if (!config.forceTranscode && hasNativeHevcSupport()) {
    console.log("[hevc.js/videojs] Native HEVC support detected — transcoding not needed");
    return null;
  }

  if (!H264Encoder.isSupported()) {
    console.warn("[hevc.js/videojs] WebCodecs VideoEncoder not available.");
    return null;
  }

  const canEncode = await H264Encoder.checkSupport();
  if (!canEncode) {
    console.warn("[hevc.js/videojs] H.264 encoding is not supported in this browser.");
    return null;
  }

  console.log("[hevc.js/videojs] No native HEVC support — installing WASM transcoder");

  installMSEIntercept({
    wasmUrl: config.wasmUrl,
    fps: config.fps,
    bitrate: config.bitrate,
    workerUrl: config.workerUrl,
  });

  // Block AVC video renditions AFTER MSE intercept is installed.
  // Order matters: MSE intercept swaps hev1→avc1 internally — our blocker
  // must sit on top so it only blocks real AVC calls from VHS, not swapped ones.
  const forceHevc = config.forceHevcRenditions ?? config.forceTranscode ?? false;
  let savedIsTypeSupported: typeof MediaSource.isTypeSupported | null = null;
  if (forceHevc) {
    const mseInterceptedITS = MediaSource.isTypeSupported;
    savedIsTypeSupported = mseInterceptedITS;
    MediaSource.isTypeSupported = function (mimeType: string): boolean {
      if (/video\/mp4/.test(mimeType) && /avc1/i.test(mimeType) && !/hev1|hvc1/i.test(mimeType)) {
        console.log(`[hevc.js/videojs] Blocking AVC: ${mimeType}`);
        return false;
      }
      return mseInterceptedITS.call(MediaSource, mimeType);
    };
  }

  return () => {
    uninstallMSEIntercept();
    if (savedIsTypeSupported) {
      MediaSource.isTypeSupported = savedIsTypeSupported;
    }
  };
}
