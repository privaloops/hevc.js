/**
 * Main-thread client for the TranscodeWorker.
 *
 * Provides the same interface as SegmentTranscoder but delegates all
 * heavy work to a Web Worker via postMessage.
 */

import type { SegmentTranscoderConfig, TranscodedInit } from "./segment-transcoder.js";

export interface TranscodeWorkerClientConfig extends SegmentTranscoderConfig {
  /** URL to the worker script (transcode-worker.js). */
  workerUrl: string;
}

export class TranscodeWorkerClient {
  private _worker: Worker;
  private _ready = false;
  private _initParsed = false;
  private _initResult: TranscodedInit | null = null;
  private _segmentId = 0;
  private _pendingResolves = new Map<number, { resolve: (data: Uint8Array | null) => void; reject: (err: Error) => void }>();
  private _readyPromise: Promise<void>;
  private _readyResolve!: () => void;
  private _initParsedPromise!: Promise<void>;
  private _initParsedResolve!: () => void;

  constructor(config: TranscodeWorkerClientConfig) {
    // Classic worker (not module) — needed for importScripts() to load WASM glue
    this._worker = new Worker(config.workerUrl);

    this._readyPromise = new Promise<void>((resolve) => { this._readyResolve = resolve; });
    this._initParsedPromise = new Promise<void>((resolve) => { this._initParsedResolve = resolve; });

    this._worker.onmessage = (e: MessageEvent) => this._onMessage(e.data);
    this._worker.onerror = (e: ErrorEvent) => {
      console.error("[hevc.js/worker] Worker error:", e.message);
    };

    // Init the transcoder inside the worker
    const { workerUrl: _, ...transcoderConfig } = config;
    this._worker.postMessage({ type: "init", config: transcoderConfig });
  }

  get isInitialized(): boolean { return this._ready; }
  get isInitParsed(): boolean { return this._initParsed; }
  get initResult(): TranscodedInit | null { return this._initResult; }

  /** Wait for the WASM decoder to be ready inside the worker */
  async waitReady(): Promise<void> {
    return this._readyPromise;
  }

  /** Send an init segment (ftyp + moov) to the worker for parsing */
  async processInitSegment(data: Uint8Array): Promise<void> {
    this._worker.postMessage(
      { type: "initSegment", data: data.buffer },
      [data.buffer],
    );
    return this._initParsedPromise;
  }

  /** Send a media segment to the worker for transcoding */
  async processMediaSegment(data: Uint8Array): Promise<Uint8Array | null> {
    const id = this._segmentId++;
    return new Promise<Uint8Array | null>((resolve, reject) => {
      this._pendingResolves.set(id, { resolve, reject });
      this._worker.postMessage(
        { type: "mediaSegment", data: data.buffer, id },
        [data.buffer],
      );
    });
  }

  /** Abort current transcoding and clear the queue */
  abort(): void {
    // Reject all pending
    for (const [id, { reject }] of this._pendingResolves) {
      reject(new Error("Aborted"));
    }
    this._pendingResolves.clear();
    this._worker.postMessage({ type: "abort" });
  }

  /** Destroy the worker */
  destroy(): void {
    this._pendingResolves.clear();
    this._worker.postMessage({ type: "destroy" });
    this._worker.terminate();
  }

  private _onMessage(msg: Record<string, unknown>): void {
    switch (msg.type) {
      case "ready":
        this._ready = true;
        this._readyResolve();
        break;

      case "initParsed":
        this._initParsed = true;
        this._initParsedResolve();
        break;

      case "transcoded": {
        const id = msg.id as number;
        const pending = this._pendingResolves.get(id);
        if (!pending) break;
        this._pendingResolves.delete(id);

        // Capture init result from first transcode
        if (msg.initSegment && !this._initResult) {
          this._initResult = {
            initSegment: new Uint8Array(msg.initSegment as ArrayBuffer),
            codec: (msg.codec as string) || "avc1.640033",
          };
        }

        const h264 = msg.h264 ? new Uint8Array(msg.h264 as ArrayBuffer) : null;
        pending.resolve(h264);
        break;
      }

      case "error": {
        const id = msg.id as number;
        const pending = this._pendingResolves.get(id);
        if (pending) {
          this._pendingResolves.delete(id);
          pending.reject(new Error(msg.message as string));
        } else {
          console.error("[hevc.js/worker]", msg.message);
        }
        break;
      }

      case "aborted":
        // Worker cleared its state — re-init if needed
        break;
    }
  }
}
