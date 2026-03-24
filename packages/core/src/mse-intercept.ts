/**
 * MSE Intercept — Patches MediaSource to transparently transcode HEVC → H.264.
 *
 * Creates a proxy SourceBuffer that:
 * 1. Reports updating=true while transcoding is in progress (blocks dash.js)
 * 2. Fires proper updatestart/update/updateend events after transcoding
 * 3. Passes audio and other non-HEVC tracks through untouched
 */

import { SegmentTranscoder } from "./segment-transcoder.js";
import type { SegmentTranscoderConfig } from "./segment-transcoder.js";
import { TranscodeWorkerClient } from "./transcode-worker-client.js";

const HEVC_DETECT_RE = /hev1|hvc1/i;                // Detect HEVC in a string
const HEVC_CODEC_RE = /hev1[^"']*|hvc1[^"']*/gi;   // Match full HEVC codec string (hev1.2.4.L123.B0)
const H264_CODEC = "avc1.640033";                    // High Profile Level 5.1 — supports up to 4K

export interface MSEInterceptConfig extends SegmentTranscoderConfig {
  /** URL to the transcode worker script. When provided, transcoding runs off main thread. */
  workerUrl?: string;
}

interface InterceptState {
  active: boolean;
  originalAddSourceBuffer: typeof MediaSource.prototype.addSourceBuffer;
  originalIsTypeSupported: typeof MediaSource.isTypeSupported;
  originalDecodingInfo: ((config: MediaDecodingConfiguration) => Promise<MediaCapabilitiesDecodingInfo>) | null;
  config: MSEInterceptConfig;
}

let interceptState: InterceptState | null = null;

/**
 * Install the MSE intercept. Call before dash.js initializes.
 */
export function installMSEIntercept(config: MSEInterceptConfig = {}): void {
  if (interceptState?.active) return;

  const originalAddSourceBuffer = MediaSource.prototype.addSourceBuffer;
  const originalIsTypeSupported = MediaSource.isTypeSupported;
  let originalDecodingInfo: InterceptState["originalDecodingInfo"] = null;

  interceptState = {
    active: true,
    originalAddSourceBuffer,
    originalIsTypeSupported,
    originalDecodingInfo: null,
    config,
  };

  // Patch isTypeSupported
  MediaSource.isTypeSupported = function (mimeType: string): boolean {
    if (HEVC_DETECT_RE.test(mimeType)) {
      const h264Mime = mimeType.replace(HEVC_CODEC_RE, H264_CODEC);
      const result = originalIsTypeSupported.call(MediaSource, h264Mime);
      console.log(`[hevc.js] isTypeSupported("${mimeType}") → "${h264Mime}" → ${result}`);
      return result;
    }
    return originalIsTypeSupported.call(MediaSource, mimeType);
  };

  // Patch navigator.mediaCapabilities.decodingInfo
  if (typeof navigator !== "undefined" && navigator.mediaCapabilities) {
    originalDecodingInfo = navigator.mediaCapabilities.decodingInfo.bind(navigator.mediaCapabilities);
    interceptState.originalDecodingInfo = originalDecodingInfo;
    navigator.mediaCapabilities.decodingInfo = async function (cfg: MediaDecodingConfiguration) {
      if (cfg.video?.contentType && HEVC_DETECT_RE.test(cfg.video.contentType)) {
        const h264Type = cfg.video.contentType.replace(HEVC_CODEC_RE, H264_CODEC);
        const h264Config = { ...cfg, video: { ...cfg.video, contentType: h264Type } };
        return originalDecodingInfo!(h264Config);
      }
      return originalDecodingInfo!(cfg);
    };
  }

  // Patch addSourceBuffer — return a proxy that handles transcoding
  MediaSource.prototype.addSourceBuffer = function (mimeType: string): SourceBuffer {
    if (!HEVC_DETECT_RE.test(mimeType)) {
      return originalAddSourceBuffer.call(this, mimeType);
    }

    console.log(`[hevc.js] addSourceBuffer("${mimeType}") → creating H.264 proxy`);
    const h264Mime = `video/mp4; codecs="${H264_CODEC}"`;
    const realSB = originalAddSourceBuffer.call(this, h264Mime);

    return createTranscodingProxy(realSB, interceptState!.config);
  };
}

/**
 * Remove the MSE intercept and restore original methods.
 */
export function uninstallMSEIntercept(): void {
  if (!interceptState?.active) return;

  MediaSource.prototype.addSourceBuffer = interceptState.originalAddSourceBuffer;
  MediaSource.isTypeSupported = interceptState.originalIsTypeSupported;
  if (interceptState.originalDecodingInfo && navigator.mediaCapabilities) {
    navigator.mediaCapabilities.decodingInfo = interceptState.originalDecodingInfo;
  }

  interceptState.active = false;
  interceptState = null;
}

/**
 * Create a proxy SourceBuffer that transcodes HEVC→H.264.
 *
 * Key behavior:
 * - appendBuffer() queues data and sets _updating=true immediately
 * - Fires updatestart → (transcode) → update → updateend like a real SB
 * - dash.js sees updating=true and waits, preventing segment flooding
 * - The real SourceBuffer receives H.264 data after transcoding
 */
function createTranscodingProxy(
  realSB: SourceBuffer,
  config: MSEInterceptConfig,
): SourceBuffer {
  // Use Worker when workerUrl is provided, otherwise fall back to main thread
  const useWorker = !!config.workerUrl;
  let workerClient: TranscodeWorkerClient | null = null;
  let transcoder: SegmentTranscoder | null = null;

  let initParsed = false;
  let initAppended = false;
  let fakeUpdating = false;
  const queue: Uint8Array[] = [];
  let processing = false;

  if (useWorker) {
    workerClient = new TranscodeWorkerClient({
      workerUrl: config.workerUrl!,
      wasmUrl: config.wasmUrl,
      fps: config.fps,
      bitrate: config.bitrate,
    });
    workerClient.waitReady().then(() => {
      console.log("[hevc.js] Worker transcoder ready");
    }).catch((err) => {
      console.error("[hevc.js] Worker init failed:", (err as Error)?.message ?? err);
    });
  } else {
    transcoder = new SegmentTranscoder(config);
    transcoder.init().catch((err) => {
      console.error("[hevc.js] Main-thread transcoder init failed:",
        (err as Error)?.message ?? err, (err as Error)?.stack);
    });
  }

  // Event listeners registered by dash.js
  const listeners = new Map<string, Set<EventListenerOrEventListenerObject>>();

  function dispatchOnProxy(type: string): void {
    const set = listeners.get(type);
    if (!set) return;
    const evt = new Event(type);
    for (const fn of set) {
      if (typeof fn === "function") fn(evt);
      else fn.handleEvent(evt);
    }
  }

  // Create the proxy
  const proxy = new Proxy(realSB, {
    set(target, prop, value) {
      // Forward property sets (appendWindowStart, timestampOffset, etc.) to real SB
      (target as unknown as Record<string | symbol, unknown>)[prop] = value;
      return true;
    },
    get(target, prop, receiver) {
      // Override updating to include our fake state
      if (prop === "updating") {
        return fakeUpdating || target.updating;
      }

      // Override abort — cancel pending transcoding + reset state for seek
      if (prop === "abort") {
        return function (): void {
          queue.length = 0;
          processing = false;
          fakeUpdating = false;
          initParsed = false;   // Next append will be a new init segment
          initAppended = false;
          if (workerClient) workerClient.abort();
          console.log("[hevc.js] abort() — queue + transcoder reset");
          target.abort();
        };
      }

      // Override appendBuffer
      if (prop === "appendBuffer") {
        return function (data: BufferSource): void {
          const bytes = toUint8Array(data);
          queue.push(bytes);

          // Signal updating=true immediately so dash.js waits
          fakeUpdating = true;
          dispatchOnProxy("updatestart");

          processNext(target);
        };
      }

      // Override addEventListener to capture dash.js listeners
      if (prop === "addEventListener") {
        return function (type: string, fn: EventListenerOrEventListenerObject, options?: boolean | AddEventListenerOptions): void {
          // Register on proxy for our synthetic events
          if (!listeners.has(type)) listeners.set(type, new Set());
          listeners.get(type)!.add(fn);
          // Also register on real SB for real events (needed for non-intercepted operations)
          target.addEventListener(type, fn, options);
        };
      }

      if (prop === "removeEventListener") {
        return function (type: string, fn: EventListenerOrEventListenerObject, options?: boolean | EventListenerOptions): void {
          listeners.get(type)?.delete(fn);
          target.removeEventListener(type, fn, options);
        };
      }

      // Everything else: forward to real SB with correct `this`
      // Native SourceBuffer getters (buffered, appendWindowStart, etc.)
      // throw "Illegal invocation" if `this` is not the real SourceBuffer.
      const value = Reflect.get(target, prop, target);
      if (typeof value === "function") {
        return value.bind(target);
      }
      return value;
    },
  });

  const realAppend = realSB.appendBuffer.bind(realSB);

  // Unified transcode interface — works with both Worker and main-thread
  async function transcodeInit(segment: Uint8Array): Promise<void> {
    if (workerClient) {
      await workerClient.waitReady();
      await workerClient.processInitSegment(segment);
    } else {
      while (!transcoder!.isInitialized) await new Promise(r => setTimeout(r, 10));
      await transcoder!.processInitSegment(segment);
    }
  }

  async function transcodeMedia(segment: Uint8Array): Promise<Uint8Array | null> {
    if (workerClient) {
      return workerClient.processMediaSegment(segment);
    }
    return transcoder!.processMediaSegment(segment);
  }

  function getInitResult(): { initSegment: Uint8Array; codec: string } | null {
    if (workerClient) return workerClient.initResult;
    return transcoder!.initResult;
  }

  async function processNext(target: SourceBuffer): Promise<void> {
    if (processing) return;
    processing = true;

    try {
      while (queue.length > 0) {
        const segment = queue.shift()!;

        // Wait for real SB to be ready
        if (target.updating) {
          await waitForUpdateEnd(target);
        }

        if (!initParsed) {
          // Init segment: parse hvcC, extract params
          await transcodeInit(segment);
          initParsed = true;
          console.log("[hevc.js] Init segment parsed");

          // Signal completion for this "append"
          fakeUpdating = false;
          dispatchOnProxy("update");
          dispatchOnProxy("updateend");

          if (queue.length > 0) {
            fakeUpdating = true;
            dispatchOnProxy("updatestart");
          }
          continue;
        }

        // Media segment: transcode (in Worker or main thread)
        console.log(`[hevc.js] Transcoding segment (${segment.byteLength}B)...`);
        const h264Segment = await transcodeMedia(segment);

        // Append H.264 init segment on first successful transcode
        const initResult = getInitResult();
        if (!initAppended && initResult) {
          initAppended = true;
          if (target.updating) await waitForUpdateEnd(target);
          realAppend(initResult.initSegment.buffer as ArrayBuffer);
          await waitForUpdateEnd(target);
          console.log("[hevc.js] H.264 init segment appended");
        }

        // Append transcoded media segment
        if (h264Segment) {
          if (target.updating) await waitForUpdateEnd(target);
          fakeUpdating = false;
          realAppend(h264Segment.buffer as ArrayBuffer);
          await waitForUpdateEnd(target);

          const buffered = target.buffered;
          const end = buffered.length ? buffered.end(buffered.length - 1).toFixed(2) : "0";
          console.log(`[hevc.js] H.264 segment appended, buffered: ${end}s`);
        } else {
          fakeUpdating = false;
          dispatchOnProxy("update");
          dispatchOnProxy("updateend");
        }

        if (queue.length > 0) {
          fakeUpdating = true;
        }
      }
    } catch (err) {
      console.error("[hevc.js/dash] Transcoding error:",
        (err as Error)?.message ?? err, (err as Error)?.stack);
      fakeUpdating = false;
      dispatchOnProxy("error");
    } finally {
      processing = false;
    }
  }

  return proxy as unknown as SourceBuffer;
}

function waitForUpdateEnd(sb: SourceBuffer): Promise<void> {
  return new Promise<void>((resolve) => {
    if (!sb.updating) { resolve(); return; }
    sb.addEventListener("updateend", () => resolve(), { once: true });
  });
}

function toUint8Array(data: BufferSource): Uint8Array {
  if (data instanceof Uint8Array) return data;
  if (data instanceof ArrayBuffer) return new Uint8Array(data);
  if (ArrayBuffer.isView(data)) {
    return new Uint8Array(data.buffer as ArrayBuffer, data.byteOffset, data.byteLength);
  }
  return new Uint8Array(data as ArrayBuffer);
}
