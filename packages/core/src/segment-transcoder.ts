/**
 * SegmentTranscoder — Transcodes HEVC fMP4 segments to H.264 fMP4 segments.
 *
 * Used by the dash.js plugin to transparently convert HEVC segments
 * before they reach MSE SourceBuffer.
 *
 * Pipeline:
 *   HEVC init segment (moov) → extract hvcC, init decoder/encoder
 *   HEVC media segment (moof+mdat) → demux → decode → encode → mux → H.264 fMP4
 */

import { HEVCDecoder } from "./decoder.js";
import { FMP4Demuxer } from "./fmp4-demuxer.js";
import { H264Encoder } from "./h264-encoder.js";
import type { EncodedChunk } from "./h264-encoder.js";
import { FMP4Muxer } from "./fmp4-muxer.js";
import type { HEVCFrame } from "./types.js";
import type { DecoderOptions } from "./types.js";

export interface SegmentTranscoderConfig {
  wasmUrl?: string;
  fps?: number;
  bitrate?: number;
}

export interface TranscodedInit {
  /** H.264 fMP4 init segment (ftyp + moov with avcC) */
  initSegment: Uint8Array;
  /** H.264 codec string for addSourceBuffer() */
  codec: string;
}

export class SegmentTranscoder {
  private _config: SegmentTranscoderConfig;
  private _decoder: HEVCDecoder | null = null;
  private _demuxer: FMP4Demuxer | null = null;
  private _encoder: H264Encoder | null = null;
  private _muxer = new FMP4Muxer();
  private _initialized = false;
  private _initResult: TranscodedInit | null = null;
  private _timescale = 90000;
  private _baseDecodeTime = 0;
  private _fps: number;
  private _width = 0;
  private _height = 0;
  private _paramSetsFed = false;
  private _paramSetsBuffer: Uint8Array | null = null;

  constructor(config: SegmentTranscoderConfig = {}) {
    this._config = config;
    this._fps = config.fps ?? 25;
  }

  /** Whether the transcoder is ready to process segments */
  get isInitialized(): boolean {
    return this._initialized;
  }

  /** The H.264 init segment result (available after processInitSegment) */
  get initResult(): TranscodedInit | null {
    return this._initResult;
  }

  /** Initialize the WASM decoder */
  async init(): Promise<void> {
    const decoderOpts: DecoderOptions = {};
    if (this._config.wasmUrl) decoderOpts.wasmUrl = this._config.wasmUrl;
    this._decoder = await HEVCDecoder.create(decoderOpts);
    this._initialized = true;
  }

  /**
   * Process an HEVC init segment (ftyp + moov).
   * Parses track info, then returns the H.264 init segment to use instead.
   *
   * The H.264 init segment is generated lazily — on the first media segment —
   * because we need the encoder's avcC descriptor which requires encoding at least
   * one frame. Returns null here; use getH264InitSegment() after first media segment.
   */
  async processInitSegment(data: Uint8Array): Promise<void> {
    this._demuxer = new FMP4Demuxer();
    await this._demuxer.parseInit(data);

    const track = this._demuxer.videoTrack;
    if (track) {
      this._timescale = track.timescale;
      this._width = track.width;
      this._height = track.height;
    }

    // Extract VPS/SPS/PPS from hvcC in the init segment
    // These must be fed to the WASM decoder before any media NALs
    const paramSets = extractParameterSetsFromInit(data);
    if (paramSets.length > 0) {
      const psSize = paramSets.reduce((s, n) => s + 4 + n.byteLength, 0);
      this._paramSetsBuffer = new Uint8Array(psSize);
      let off = 0;
      for (const ps of paramSets) {
        this._paramSetsBuffer[off++] = 0;
        this._paramSetsBuffer[off++] = 0;
        this._paramSetsBuffer[off++] = 0;
        this._paramSetsBuffer[off++] = 1;
        this._paramSetsBuffer.set(ps, off);
        off += ps.byteLength;
      }
    }
  }

  /**
   * Transcode an HEVC media segment to H.264.
   * Returns the H.264 fMP4 segment (moof + mdat).
   * On the first call, also generates the H.264 init segment.
   */
  /** Perf stats from last processMediaSegment call */
  lastPerfStats: { demuxMs: number; decodeMs: number; encodeMs: number; frames: number } | null = null;

  async processMediaSegment(data: Uint8Array): Promise<Uint8Array | null> {
    if (!this._decoder || !this._demuxer) {
      throw new Error("Transcoder not initialized. Call init() and processInitSegment() first.");
    }

    // 1. Demux → samples with NAL units
    const tDemux0 = performance.now();
    const samples = this._demuxer.parseSegment(data);
    if (samples.length === 0) return null;
    const tDemuxEnd = performance.now();

    // Extract the absolute base decode time directly from the tfdt box
    // in the raw fMP4 data. mp4box.js returns sequential DTS which breaks
    // after seek (fragments arrive out of order).
    // Extract the absolute base decode time directly from the tfdt box
    // in the raw fMP4 data. mp4box.js returns sequential DTS which breaks
    // after seek (fragments arrive out of order).
    const segmentBaseTime = extractTfdt(data) ?? samples[0]!.dts;

    // 2. Feed VPS/SPS/PPS on first segment (from hvcC in init segment)
    if (!this._paramSetsFed && this._paramSetsBuffer) {
      this._decoder.feed(this._paramSetsBuffer);
      this._paramSetsFed = true;
    }

    // 3. Feed NAL units to WASM decoder (Annex B format)
    for (const sample of samples) {
      const totalSize = sample.nalUnits.reduce((sum, n) => sum + 4 + n.length, 0);
      const nalBuffer = new Uint8Array(totalSize);
      let offset = 0;
      for (const nal of sample.nalUnits) {
        nalBuffer[offset++] = 0;
        nalBuffer[offset++] = 0;
        nalBuffer[offset++] = 0;
        nalBuffer[offset++] = 1;
        nalBuffer.set(nal, offset);
        offset += nal.length;
      }
      this._decoder.feed(nalBuffer);
    }

    // 3. Drain decoded YUV frames
    const frames = this._decoder.drain();
    const tDecodeEnd = performance.now();
    if (frames.length === 0) {
      this.lastPerfStats = null;
      return null;
    }

    // 4. Encode to H.264
    if (!this._encoder) {
      this._encoder = new H264Encoder({
        width: frames[0]!.width,
        height: frames[0]!.height,
        fps: this._fps,
        bitrate: this._config.bitrate,
      });
      this._width = frames[0]!.width;
      this._height = frames[0]!.height;
    }

    const chunks: EncodedChunk[] = [];
    this._encoder.onChunk = (chunk) => chunks.push(chunk);

    for (let i = 0; i < frames.length; i++) {
      const timestampUs = Math.round((segmentBaseTime / this._timescale) * 1_000_000)
        + Math.round((i / this._fps) * 1_000_000);
      // First frame of each segment should be a keyframe (ABR switch support)
      this._encoder.encode(frames[i]!, timestampUs, i === 0);
    }

    await this._encoder.flush();
    if (chunks.length === 0) return null;

    // 5. Generate H.264 init segment on first successful encode
    if (!this._initResult) {
      const avcC = this._encoder.codecDescription;
      if (!avcC) throw new Error("No avcC description from encoder");

      const initSegment = this._muxer.generateInit({
        width: this._width,
        height: this._height,
        timescale: this._timescale,
        avcC,
      });

      this._initResult = {
        initSegment,
        codec: "avc1.42001f",
      };
    }

    // 6. Mux H.264 chunks into fMP4 media segment
    const muxerSamples = chunks.map((c) => ({
      data: c.data,
      duration: Math.round(c.duration * this._timescale / 1_000_000),
      isKeyframe: c.isKeyframe,
      compositionTimeOffset: 0,
    }));

    const mediaSegment = this._muxer.muxSegment(muxerSamples, segmentBaseTime);

    const tEncodeEnd = performance.now();
    this.lastPerfStats = {
      demuxMs: tDemuxEnd - tDemux0,
      decodeMs: tDecodeEnd - tDemuxEnd,
      encodeMs: tEncodeEnd - tDecodeEnd,
      frames: frames.length,
    };

    return mediaSegment;
  }

  /**
   * Flush remaining frames from the decoder.
   * Returns the final H.264 media segment, or null if no frames remain.
   */
  async flush(): Promise<Uint8Array | null> {
    if (!this._decoder) return null;

    const remaining = this._decoder.flush();
    if (remaining.length === 0) return null;

    return this._encodeFrames(remaining);
  }

  /** Release all resources */
  destroy(): void {
    this._encoder?.close();
    this._decoder?.destroy();
    this._decoder = null;
    this._encoder = null;
    this._demuxer = null;
    this._initResult = null;
  }

  private async _encodeFrames(frames: HEVCFrame[]): Promise<Uint8Array | null> {
    if (!this._encoder || frames.length === 0) return null;

    const chunks: EncodedChunk[] = [];
    this._encoder.onChunk = (chunk) => chunks.push(chunk);

    for (let i = 0; i < frames.length; i++) {
      const timestampUs = Math.round((this._baseDecodeTime / this._timescale) * 1_000_000)
        + Math.round((i / this._fps) * 1_000_000);
      this._encoder.encode(frames[i]!, timestampUs, i === 0);
    }

    await this._encoder.flush();
    if (chunks.length === 0) return null;

    const muxerSamples = chunks.map((c) => ({
      data: c.data,
      duration: Math.round(c.duration * this._timescale / 1_000_000),
      isKeyframe: c.isKeyframe,
      compositionTimeOffset: 0,
    }));

    const segment = this._muxer.muxSegment(muxerSamples, this._baseDecodeTime);
    this._baseDecodeTime += muxerSamples.reduce((sum, s) => sum + s.duration, 0);
    return segment;
  }
}

/**
 * Extract VPS/SPS/PPS NAL units from hvcC box in an fMP4 init segment.
 * Scans for the 'hvcC' fourcc and parses the HEVC decoder configuration record.
 */
function extractParameterSetsFromInit(data: Uint8Array): Uint8Array[] {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const sets: Uint8Array[] = [];

  // Scan for 'hvcC' (0x68766343)
  for (let i = 0; i < data.byteLength - 4; i++) {
    if (view.getUint32(i) !== 0x68766343) continue;

    // hvcC payload starts after the fourcc (box header is 4 bytes before)
    const payload = i + 4;
    if (payload + 23 > data.byteLength) break;

    // Byte 22 of hvcC payload = numOfArrays
    const numArrays = data[payload + 22]!;
    let off = payload + 23;

    for (let a = 0; a < numArrays && off + 3 <= data.byteLength; a++) {
      off++; // array_completeness(1) + NAL_unit_type(5) packed in 1 byte
      const numNalus = view.getUint16(off);
      off += 2;
      for (let n = 0; n < numNalus && off + 2 <= data.byteLength; n++) {
        const naluLen = view.getUint16(off);
        off += 2;
        if (off + naluLen <= data.byteLength) {
          sets.push(data.slice(off, off + naluLen));
        }
        off += naluLen;
      }
    }
    break;
  }

  return sets;
}

/**
 * Extract baseMediaDecodeTime from the tfdt box in an fMP4 media segment.
 * Parses: moof → traf → tfdt → baseMediaDecodeTime.
 * Returns the absolute decode time in timescale units, or null if not found.
 */
function extractTfdt(data: Uint8Array): number | null {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const len = data.byteLength;

  // Scan for 'tfdt' box type (0x74666474)
  for (let i = 0; i + 8 <= len; i++) {
    if (view.getUint32(i + 4) !== 0x74666474) continue;
    // Found tfdt — it's a FullBox: version(1) + flags(3) + payload
    const version = data[i + 8];
    if (version === 1 && i + 20 <= len) {
      // 64-bit baseMediaDecodeTime
      const hi = view.getUint32(i + 12);
      const lo = view.getUint32(i + 16);
      return hi * 0x100000000 + lo;
    }
    if (i + 16 <= len) {
      // 32-bit baseMediaDecodeTime
      return view.getUint32(i + 12);
    }
  }
  return null;
}
