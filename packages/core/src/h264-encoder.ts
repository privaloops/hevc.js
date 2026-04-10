/**
 * H264Encoder — Wraps WebCodecs VideoEncoder to transcode YUV frames to H.264.
 *
 * Pipeline: HEVCFrame (Uint16Array YUV planes) → VideoFrame(I420) → VideoEncoder → EncodedVideoChunk
 */

import type { HEVCFrame } from "./types.js";

export interface H264EncoderConfig {
  width: number;
  height: number;
  fps?: number;
  bitrate?: number;
}

export interface EncodedChunk {
  data: Uint8Array;
  timestamp: number;
  duration: number;
  isKeyframe: boolean;
}

/**
 * Pick an appropriate H.264 codec string based on resolution.
 * Baseline L3.1 caps at 720p — higher resolutions need higher levels.
 */
function pickH264Codec(w: number, h: number): string {
  const pixels = w * h;
  if (pixels > 2073600) return "avc1.640033"; // > 1080p → High 5.1 (4K)
  if (pixels > 921600)  return "avc1.64002a"; // > 720p  → High 4.2 (1080p)
  return "avc1.640028";                        // ≤ 720p  → High 4.0
}

export class H264Encoder {
  private _encoder: VideoEncoder;
  private _width: number;
  private _height: number;
  private _fps: number;
  private _codecDescription: Uint8Array | null = null;

  /** Callback invoked for each encoded chunk */
  onChunk: ((chunk: EncodedChunk) => void) | null = null;

  /** Callback invoked when codec description (avcC) is available */
  onCodecDescription: ((desc: Uint8Array) => void) | null = null;

  constructor(config: H264EncoderConfig) {
    this._width = config.width;
    this._height = config.height;
    this._fps = config.fps ?? 25;

    this._encoder = new VideoEncoder({
      output: (chunk, metadata) => this._handleOutput(chunk, metadata),
      error: (e) => { throw new Error(`VideoEncoder error: ${e.message}`); },
    });

    this._encoder.configure({
      codec: pickH264Codec(this._width, this._height),
      width: this._width,
      height: this._height,
      bitrate: config.bitrate ?? this._width * this._height * this._fps * 0.1, // ~0.1 bpp
      framerate: this._fps,
      hardwareAcceleration: "no-preference",
      avc: { format: "avc" },
    });
  }

  /** Get the H.264 codec string used by this encoder */
  get codec(): string {
    return pickH264Codec(this._width, this._height);
  }

  /** Get the avcC codec description (available after first keyframe) */
  get codecDescription(): Uint8Array | null {
    return this._codecDescription;
  }

  /**
   * Encode a decoded HEVC frame.
   * Converts Uint16Array YUV planes to I420 Uint8Array, creates VideoFrame, encodes.
   */
  encode(frame: HEVCFrame, timestampUs: number, keyFrame = false): void {
    const w = frame.width;
    const h = frame.height;
    const cw = frame.chromaWidth;
    const ch = frame.chromaHeight;
    const shift = frame.bitDepth > 8 ? frame.bitDepth - 8 : 0;

    // Build I420 buffer: Y plane + U (Cb) plane + V (Cr) plane
    const ySize = w * h;
    const cSize = cw * ch;
    const i420 = new Uint8Array(ySize + cSize * 2);

    // Y plane
    for (let i = 0; i < ySize; i++) {
      const v = frame.y[i]! >> shift;
      i420[i] = v > 255 ? 255 : v;
    }
    // U (Cb) plane
    for (let i = 0; i < cSize; i++) {
      const v = frame.cb[i]! >> shift;
      i420[ySize + i] = v > 255 ? 255 : v;
    }
    // V (Cr) plane
    for (let i = 0; i < cSize; i++) {
      const v = frame.cr[i]! >> shift;
      i420[ySize + cSize + i] = v > 255 ? 255 : v;
    }

    const videoFrame = new VideoFrame(i420, {
      format: "I420",
      codedWidth: w,
      codedHeight: h,
      timestamp: timestampUs,
      duration: Math.round(1_000_000 / this._fps),
      colorSpace: { primaries: "bt709", transfer: "bt709", matrix: "bt709" },
    });

    this._encoder.encode(videoFrame, { keyFrame });
    videoFrame.close();
  }

  /** Flush the encoder — waits for all pending chunks to be output */
  async flush(): Promise<void> {
    await this._encoder.flush();
  }

  /** Close the encoder and release resources */
  close(): void {
    this._encoder.close();
  }

  /** Check if VideoEncoder is supported in the current environment */
  static isSupported(): boolean {
    return typeof VideoEncoder !== "undefined";
  }

  /**
   * Async capability probe — checks that VideoEncoder can actually encode H.264.
   * Firefox exposes VideoEncoder but may not support H.264 encoding.
   * Returns false if the API is missing or the config is not supported.
   */
  static async checkSupport(): Promise<boolean> {
    if (typeof VideoEncoder === "undefined") return false;
    if (typeof VideoEncoder.isConfigSupported !== "function") return false;
    try {
      const result = await VideoEncoder.isConfigSupported({
        codec: "avc1.640028",
        width: 640,
        height: 480,
        bitrate: 1_000_000,
        framerate: 25,
      });
      return result.supported === true;
    } catch {
      return false;
    }
  }

  private _handleOutput(chunk: EncodedVideoChunk, metadata?: EncodedVideoChunkMetadata): void {
    // Capture codec description (avcC) from first chunk with decoderConfig
    if (metadata?.decoderConfig?.description && !this._codecDescription) {
      const desc = metadata.decoderConfig.description;
      if (desc instanceof ArrayBuffer) {
        this._codecDescription = new Uint8Array(desc);
      } else if (desc instanceof Uint8Array) {
        this._codecDescription = new Uint8Array(desc);
      } else if (ArrayBuffer.isView(desc)) {
        this._codecDescription = new Uint8Array((desc as ArrayBufferView).buffer);
      }
      this.onCodecDescription?.(this._codecDescription!);
    }

    // Extract chunk data
    const data = new Uint8Array(chunk.byteLength);
    chunk.copyTo(data);

    const encoded: EncodedChunk = {
      data,
      timestamp: chunk.timestamp,
      duration: chunk.duration ?? Math.round(1_000_000 / this._fps),
      isKeyframe: chunk.type === "key",
    };

    this.onChunk?.(encoded);
  }
}
