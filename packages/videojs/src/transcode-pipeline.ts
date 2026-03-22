/**
 * TranscodePipeline — Orchestrates HEVC → H.264 transcoding for MSE playback.
 *
 * Pipeline per segment:
 *   fMP4 HEVC segment → demux → HEVC NALs → WASM decode → YUV frames
 *   → VideoEncoder(H.264) → fMP4 mux → MSE SourceBuffer → <video>
 */

import { HEVCDecoder } from "@hevcjs/core";
import type { HEVCFrame } from "@hevcjs/core";
import { FMP4Demuxer } from "./fmp4-demuxer.js";
import { H264Encoder } from "./h264-encoder.js";
import type { EncodedChunk } from "./h264-encoder.js";
import { FMP4Muxer } from "./fmp4-muxer.js";
import { MSEController } from "./mse-controller.js";

export interface TranscodePipelineConfig {
  videoElement: HTMLVideoElement;
  wasmUrl?: string;
  fps?: number;
  bitrate?: number;
}

export class TranscodePipeline {
  private _config: TranscodePipelineConfig;
  private _decoder: HEVCDecoder | null = null;
  private _demuxer = new FMP4Demuxer();
  private _encoder: H264Encoder | null = null;
  private _muxer = new FMP4Muxer();
  private _mse: MSEController;
  private _initialized = false;
  private _mseInitialized = false;
  private _timescale = 90000;
  private _baseDecodeTime = 0;
  private _fps: number;

  constructor(config: TranscodePipelineConfig) {
    this._config = config;
    this._mse = new MSEController(config.videoElement);
    this._fps = config.fps ?? 25;
  }

  /** Initialize the WASM decoder */
  async init(): Promise<void> {
    this._decoder = await HEVCDecoder.create({
      wasmUrl: this._config.wasmUrl,
    });
    this._initialized = true;
  }

  /**
   * Process an fMP4 init segment (moov with HEVC codec config).
   * Extracts VPS/SPS/PPS from hvcC and feeds them to the decoder.
   */
  processInitSegment(data: Uint8Array): void {
    if (!this._initialized) throw new Error("Pipeline not initialized. Call init() first.");
    this._demuxer.parseInit(data);

    const track = this._demuxer.videoTrack;
    if (!track) throw new Error("No video track in init segment");

    this._timescale = track.timescale;
    if (track.width && track.height) {
      this._fps = this._config.fps ?? 25;
    }
  }

  /**
   * Process an fMP4 media segment containing HEVC video.
   * Demuxes → decodes → re-encodes → muxes → appends to MSE.
   */
  async processMediaSegment(data: Uint8Array): Promise<void> {
    if (!this._decoder) throw new Error("Pipeline not initialized");

    // 1. Demux fMP4 → samples with NAL units
    const samples = this._demuxer.parseSegment(data);
    if (samples.length === 0) return;

    // 2. Feed NAL units to WASM decoder
    for (const sample of samples) {
      // Reassemble with Annex B start codes
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

    // 3. Drain decoded frames
    const frames = this._decoder.drain();
    if (frames.length === 0) return;

    // 4. Ensure encoder is created with correct dimensions
    if (!this._encoder) {
      this._encoder = new H264Encoder({
        width: frames[0]!.width,
        height: frames[0]!.height,
        fps: this._fps,
        bitrate: this._config.bitrate,
      });
    }

    // 5. Encode frames → collect chunks
    const chunks: EncodedChunk[] = [];
    this._encoder.onChunk = (chunk) => chunks.push(chunk);

    for (let i = 0; i < frames.length; i++) {
      const frame = frames[i]!;
      const timestampUs = Math.round((this._baseDecodeTime / this._timescale) * 1_000_000)
        + Math.round((i / this._fps) * 1_000_000);
      const isKeyframe = i === 0 && this._baseDecodeTime === 0; // First frame of first segment
      this._encoder.encode(frame, timestampUs, isKeyframe);
    }

    await this._encoder.flush();

    if (chunks.length === 0) return;

    // 6. Initialize MSE with first encoded segment (need avcC)
    if (!this._mseInitialized) {
      const avcC = this._encoder.codecDescription;
      if (!avcC) throw new Error("No avcC description from encoder");

      const initSegment = this._muxer.generateInit({
        width: frames[0]!.width,
        height: frames[0]!.height,
        timescale: this._timescale,
        avcC,
      });

      await this._mse.init(initSegment);
      this._mseInitialized = true;
    }

    // 7. Mux encoded chunks into fMP4 media segment
    const muxerSamples = chunks.map((c) => ({
      data: c.data,
      duration: Math.round(c.duration * this._timescale / 1_000_000),
      isKeyframe: c.isKeyframe,
      compositionTimeOffset: 0,
    }));

    const mediaSegment = this._muxer.muxSegment(muxerSamples, this._baseDecodeTime);

    // 8. Append to MSE
    await this._mse.appendSegment(mediaSegment);

    // Update base decode time for next segment
    this._baseDecodeTime += muxerSamples.reduce((sum, s) => sum + s.duration, 0);
  }

  /**
   * Process a raw HEVC bitstream (Annex B format, .265 file).
   * Feeds the entire bitstream, drains, encodes, and appends to MSE.
   */
  async processRawBitstream(data: Uint8Array): Promise<void> {
    if (!this._decoder) throw new Error("Pipeline not initialized");

    this._decoder.feed(data);
    const frames = this._decoder.drain();
    const flushed = this._decoder.flush();
    const allFrames = [...frames, ...flushed];

    if (allFrames.length === 0) return;
    await this._encodeAndAppend(allFrames);
  }

  /** Flush remaining frames at end of stream */
  async flush(): Promise<void> {
    if (!this._decoder) return;

    const remaining = this._decoder.flush();
    if (remaining.length > 0) {
      await this._encodeAndAppend(remaining);
    }

    this._mse.endOfStream();
  }

  /** Clean up all resources */
  destroy(): void {
    this._encoder?.close();
    this._decoder?.destroy();
    this._mse.destroy();
    this._decoder = null;
    this._encoder = null;
  }

  private async _encodeAndAppend(frames: HEVCFrame[]): Promise<void> {
    if (!this._encoder) {
      this._encoder = new H264Encoder({
        width: frames[0]!.width,
        height: frames[0]!.height,
        fps: this._fps,
        bitrate: this._config.bitrate,
      });
    }

    const chunks: EncodedChunk[] = [];
    this._encoder.onChunk = (chunk) => chunks.push(chunk);

    for (let i = 0; i < frames.length; i++) {
      const timestampUs = Math.round((this._baseDecodeTime / this._timescale) * 1_000_000)
        + Math.round((i / this._fps) * 1_000_000);
      this._encoder.encode(frames[i]!, timestampUs, i === 0);
    }

    await this._encoder.flush();
    if (chunks.length === 0) return;

    if (!this._mseInitialized) {
      const avcC = this._encoder.codecDescription;
      if (!avcC) throw new Error("No avcC description from encoder");

      const initSegment = this._muxer.generateInit({
        width: frames[0]!.width,
        height: frames[0]!.height,
        timescale: this._timescale,
        avcC,
      });
      await this._mse.init(initSegment);
      this._mseInitialized = true;
    }

    const muxerSamples = chunks.map((c) => ({
      data: c.data,
      duration: Math.round(c.duration * this._timescale / 1_000_000),
      isKeyframe: c.isKeyframe,
      compositionTimeOffset: 0,
    }));

    const mediaSegment = this._muxer.muxSegment(muxerSamples, this._baseDecodeTime);
    await this._mse.appendSegment(mediaSegment);
    this._baseDecodeTime += muxerSamples.reduce((sum, s) => sum + s.duration, 0);
  }
}
