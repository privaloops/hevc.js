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
  /** Called when video transcoding starts — use to pause player buffering. */
  onTranscodeStart?: () => void;
  /** Called when video transcoding ends — use to resume player buffering. */
  onTranscodeEnd?: () => void;
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
  if (interceptState?.active) {
    // Already installed — update config (allows late-binding of callbacks)
    Object.assign(interceptState.config, config);
    return;
  }

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
  let lastInitSegment: Uint8Array | null = null; // tracks current H.264 init to detect changes
  let fakeUpdating = false;
  const queue: Uint8Array[] = [];
  let processing = false;
  let abortGeneration = 0; // incremented on abort — lets processNext detect stale runs
  let cachedInitData: Uint8Array | null = null; // cached for re-send after no-abort seek

  // Pipeline overlap: max segments queued before backpressure blocks dash.js.
  // At 2, dash.js can prefetch the next segment while we transcode the current one.
  // Beyond 2, we block to avoid unbounded memory growth (especially 4K).
  const MAX_QUEUE_BEFORE_BACKPRESSURE = 2;

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
      // Detect seek: hls.js changes timestampOffset before appending new segments.
      // Flush stale segments from the queue to avoid transcoding old data.
      if (prop === "timestampOffset" && value !== target.timestampOffset) {
        if (queue.length > 0 || processing) {
          console.log(`[hevc.js] timestampOffset changed (${target.timestampOffset} → ${value}) — flushing queue + resetting transcoder`);
          abortGeneration++;
          queue.length = 0;
          processing = false;
          initParsed = false;
          initAppended = false;
          if (workerClient) workerClient.abort();
          if (fakeUpdating) {
            fakeUpdating = false;
            dispatchOnProxy("update");
            dispatchOnProxy("updateend");
          }
        }
      }
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
          abortGeneration++;  // signal stale processNext to exit
          queue.length = 0;
          processing = false;
          fakeUpdating = false;
          initParsed = false;   // Next append will be a new init segment
          initAppended = false;
          if (workerClient) workerClient.abort();
          console.log("[hevc.js] abort() — queue + transcoder reset (gen=" + abortGeneration + ")");
          target.abort();
        };
      }

      // Override appendBuffer
      if (prop === "appendBuffer") {
        return function (data: BufferSource): void {
          const bytes = toUint8Array(data);
          queue.push(bytes);

          fakeUpdating = true;
          dispatchOnProxy("updatestart");

          // Release backpressure immediately if queue is shallow enough.
          // This lets the player create audio SourceBuffers and continue
          // buffering while video transcoding runs in the background.
          if (queue.length < MAX_QUEUE_BEFORE_BACKPRESSURE) {
            fakeUpdating = false;
            dispatchOnProxy("update");
            dispatchOnProxy("updateend");
          }

          processNext(target);
        };
      }

      // Override addEventListener — register ONLY on proxy map, NOT on real SB.
      // This prevents hls.js from receiving real SB events directly (which
      // would double-fire updateend and crash hls.js's buffer controller).
      if (prop === "addEventListener") {
        return function (type: string, fn: EventListenerOrEventListenerObject, options?: boolean | AddEventListenerOptions): void {
          if (!listeners.has(type)) listeners.set(type, new Set());
          listeners.get(type)!.add(fn);
        };
      }

      if (prop === "removeEventListener") {
        return function (type: string, fn: EventListenerOrEventListenerObject, options?: boolean | EventListenerOptions): void {
          listeners.get(type)?.delete(fn);
        };
      }

      // Override remove — forward to real SB and relay events to proxy listeners
      if (prop === "remove") {
        return function (start: number, end: number): void {
          dispatchOnProxy("updatestart");
          target.remove(start, end);
          target.addEventListener("updateend", () => {
            dispatchOnProxy("update");
            dispatchOnProxy("updateend");
          }, { once: true });
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
    const myGeneration = abortGeneration;

    try {
      while (queue.length > 0) {
        if (abortGeneration !== myGeneration) return; // aborted — exit silently

        const segment = queue.shift()!;

        // Wait for real SB to be ready
        if (target.updating) {
          await waitForUpdateEnd(target);
          if (abortGeneration !== myGeneration) return;
        }

        // Detect init segment by ftyp/moov box signature.
        // Handles both first-time init AND re-sent init after seek (no-abort case).
        const isInit = isInitSegment(segment);

        if (isInit || !initParsed) {
          const initData = isInit ? segment : cachedInitData;
          if (!initData) {
            console.error("[hevc.js] No init segment available — cannot process media");
            continue;
          }

          // Cache init data before transcodeInit (which transfers/detaches the buffer)
          cachedInitData = new Uint8Array(initData);

          // (Re-)parse init segment in worker/transcoder
          await transcodeInit(initData);
          if (abortGeneration !== myGeneration) return;
          initParsed = true;
          // Reset initAppended — after a new init, we need a fresh H.264 init
          initAppended = false;
          console.log("[hevc.js] Init segment parsed");

          if (isInit) {
            // Release backpressure — init-only append, no media to transcode
            if (fakeUpdating) {
              fakeUpdating = false;
              dispatchOnProxy("update");
              dispatchOnProxy("updateend");
            }
            continue;
          }
          // Fall through: initParsed is now true, process this segment as media below
        }

        // Media segment: transcode (in Worker or main thread)
        console.log(`[hevc.js] Transcoding segment (${segment.byteLength}B)...`);
        config.onTranscodeStart?.();
        const h264Segment = await transcodeMedia(segment);
        config.onTranscodeEnd?.();
        if (abortGeneration !== myGeneration) return; // aborted during transcode

        // Append H.264 init segment (on first transcode or after resolution change)
        const initResult = getInitResult();
        const initChanged = initResult && initResult.initSegment !== lastInitSegment;
        if (initResult && (!initAppended || initChanged)) {
          initAppended = true;
          lastInitSegment = initResult.initSegment;
          if (target.updating) await waitForUpdateEnd(target);
          if (abortGeneration !== myGeneration) return;
          realAppend(initResult.initSegment.buffer as ArrayBuffer);
          await waitForUpdateEnd(target);
          if (abortGeneration !== myGeneration) return;
          console.log(`[hevc.js] H.264 init segment appended${initChanged && lastInitSegment ? " (resolution change)" : ""}`);
        }

        // Append transcoded media segment
        if (h264Segment) {
          if (target.updating) await waitForUpdateEnd(target);
          if (abortGeneration !== myGeneration) return;
          realAppend(h264Segment.buffer as ArrayBuffer);
          await waitForUpdateEnd(target);
          if (abortGeneration !== myGeneration) return;

          const buffered = target.buffered;
          const end = buffered.length ? buffered.end(buffered.length - 1).toFixed(2) : "0";
          console.log(`[hevc.js] H.264 segment appended, buffered: ${end}s`);
        }

        // Release backpressure if queue dropped below threshold.
        // Guard: fakeUpdating may already be false (released in appendBuffer).
        if (fakeUpdating && queue.length < MAX_QUEUE_BEFORE_BACKPRESSURE) {
          fakeUpdating = false;
          dispatchOnProxy("update");
          dispatchOnProxy("updateend");
        }
      }
    } catch (err) {
      if (abortGeneration !== myGeneration) return; // aborted — swallow error
      console.error("[hevc.js/dash] Transcoding error:",
        (err as Error)?.message ?? err, (err as Error)?.stack);
      fakeUpdating = false;
      dispatchOnProxy("error");
    } finally {
      if (abortGeneration === myGeneration) {
        processing = false;
        // Ensure player is unblocked when queue drains
        if (fakeUpdating) {
          fakeUpdating = false;
          dispatchOnProxy("update");
          dispatchOnProxy("updateend");
        }
      }
    }
  }

  return proxy as unknown as SourceBuffer;
}

/**
 * Detect fMP4 init segment by scanning for 'ftyp' or 'moov' box type
 * in the first 8 bytes (box header: 4-byte size + 4-byte type).
 */
function isInitSegment(data: Uint8Array): boolean {
  if (data.byteLength < 8) return false;
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const boxType = view.getUint32(4);
  // 'ftyp' = 0x66747970, 'moov' = 0x6D6F6F76
  return boxType === 0x66747970 || boxType === 0x6D6F6F76;
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
