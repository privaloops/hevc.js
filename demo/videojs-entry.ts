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

  return () => {
    uninstallMSEIntercept();
  };
}
