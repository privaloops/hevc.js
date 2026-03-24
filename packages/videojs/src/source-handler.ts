/**
 * Video.js SourceHandler for HEVC/H.265 — transcodes HEVC to H.264 via MSE.
 *
 * Registers on the Html5 tech with high priority. When the browser can't play
 * HEVC natively, this handler intercepts the source, fetches segments,
 * transcodes HEVC → H.264 via WASM + VideoEncoder, and feeds MSE.
 *
 * Usage:
 * ```ts
 * import 'hevc.js/videojs';
 * // That's it — the SourceHandler auto-registers.
 * // Video.js will use it automatically for HEVC sources.
 *
 * const player = videojs('my-video', {
 *   sources: [{ src: 'video.265', type: 'video/hevc' }]
 * });
 * ```
 */

import videojs from "video.js";
import { TranscodePipeline, H264Encoder } from "@hevcjs/core";

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type VjsStatic = any;

const HEVC_TYPES = [
  "video/hevc",
  "application/x-hevc",
  "video/mp4; codecs=\"hev1\"",
  "video/mp4; codecs=\"hvc1\"",
];

/** Check if the browser supports HEVC natively via MSE */
function hasNativeHevcSupport(): boolean {
  if (typeof MediaSource === "undefined") return false;
  return (
    MediaSource.isTypeSupported('video/mp4; codecs="hev1.1.6.L93.B0"') ||
    MediaSource.isTypeSupported('video/mp4; codecs="hvc1.1.6.L93.B0"')
  );
}

/** Check if WebCodecs VideoEncoder is available */
function hasVideoEncoder(): boolean {
  return H264Encoder.isSupported();
}

function isHevcSource(source: { type?: string; src?: string }): boolean {
  const type = (source.type ?? "").toLowerCase();
  if (HEVC_TYPES.some((t) => type.includes(t))) return true;
  const src = (source.src ?? "").toLowerCase();
  return src.endsWith(".265") || src.endsWith(".hevc") || src.endsWith(".h265");
}

/**
 * SourceHandler implementation.
 * Manages the TranscodePipeline lifecycle for a given source.
 */
class HEVCSourceHandler {
  private _pipeline: TranscodePipeline | null = null;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private _tech: any;
  private _disposed = false;

  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  constructor(source: { src: string; type?: string }, tech: any) {
    this._tech = tech;
    this._init(source);
  }

  private async _init(source: { src: string }): Promise<void> {
    try {
      const videoEl = this._tech.el() as HTMLVideoElement;

      this._pipeline = new TranscodePipeline({
        videoElement: videoEl,
      });

      await this._pipeline.init();

      // Fetch the source
      const response = await fetch(source.src);
      const data = new Uint8Array(await response.arrayBuffer());

      if (this._disposed) return;

      // Detect format and process
      if (isFMP4(data)) {
        await this._pipeline.processInitSegment(data);
        await this._pipeline.processMediaSegment(data);
      } else {
        await this._pipeline.processRawBitstream(data);
      }

      await this._pipeline.flush();

      // Trigger video.js events
      this._tech.trigger("loadedmetadata");
      this._tech.trigger("loadeddata");
      this._tech.trigger("canplay");
    } catch (err) {
      console.error("[hevc.js] SourceHandler error:", err);
      this._tech.trigger("error");
    }
  }

  dispose(): void {
    this._disposed = true;
    this._pipeline?.destroy();
    this._pipeline = null;
  }
}

/** Detect fMP4 by checking for 'ftyp', 'moov', or 'styp' box at start */
function isFMP4(data: Uint8Array): boolean {
  if (data.length < 8) return false;
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const type = view.getUint32(4);
  return type === 0x66747970 || type === 0x6D6F6F76 || type === 0x73747970;
}

// ---- Register the SourceHandler on Video.js Html5 tech ----

const vjs = videojs as VjsStatic;

if (vjs?.getTech) {
  const Html5 = vjs.getTech("Html5");

  if (Html5?.registerSourceHandler) {
    Html5.registerSourceHandler(
      {
        canPlayType(type: string): string {
          if (hasNativeHevcSupport() || !hasVideoEncoder()) return "";

          const lower = type.toLowerCase();
          if (HEVC_TYPES.some((t) => lower.includes(t))) return "maybe";
          return "";
        },

        canHandleSource(
          source: { type?: string; src?: string },
          _options?: Record<string, unknown>,
        ): string {
          if (hasNativeHevcSupport() || !hasVideoEncoder()) return "";
          return isHevcSource(source) ? "maybe" : "";
        },

        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        handleSource(
          source: { src: string; type?: string },
          tech: unknown,
          _options?: Record<string, unknown>,
        ): { dispose: () => void } {
          const handler = new HEVCSourceHandler(source, tech);
          return {
            dispose() {
              handler.dispose();
            },
          };
        },
      },
      0, // priority: higher than VHS
    );
  }
}

export { HEVCSourceHandler };
