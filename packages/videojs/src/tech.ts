/**
 * Video.js Tech for HEVC/H.265 WASM decoding.
 *
 * Registers as 'HevcWasm' tech. Auto-activates when:
 * - The source type is 'video/mp4; codecs="hev1..."' or 'application/x-hevc'
 * - The browser lacks native HEVC support
 *
 * Usage:
 * ```ts
 * import 'hevc.js/videojs';
 * const player = videojs('my-video', {
 *   techOrder: ['html5', 'HevcWasm']
 * });
 * ```
 */

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type VideoJsStatic = any;

import videojs from "video.js";
import { HEVCDecoder, FMP4Demuxer, FrameRenderer } from "@hevcjs/core";
import type { HEVCFrame } from "@hevcjs/core";

interface HevcWasmOptions {
  wasmUrl?: string;
}

interface SourceObject {
  src: string;
  type?: string;
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
const BaseTech: any = (videojs as VideoJsStatic)?.getComponent?.("Tech") ?? class {};

class HevcWasmTech extends BaseTech {
  private _decoder: HEVCDecoder | null = null;
  private _demuxer: FMP4Demuxer = new FMP4Demuxer();
  private _renderer: FrameRenderer = new FrameRenderer();
  private _frames: HEVCFrame[] = [];
  private _currentFrame: number = 0;
  private _playing: boolean = false;
  private _duration: number = 0;
  private _currentTime: number = 0;
  private _playTimer: ReturnType<typeof setInterval> | null = null;
  private _fps: number = 25;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private _videoEl: HTMLVideoElement | null = null;
  private _canvasEl: HTMLCanvasElement | null = null;

  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  constructor(player: any, options: HevcWasmOptions) {
    super(player, options);
    this._initDecoder(options);
  }

  private async _initDecoder(options: HevcWasmOptions): Promise<void> {
    try {
      this._decoder = await HEVCDecoder.create({
        wasmUrl: options.wasmUrl,
      });

      // Setup rendering surface
      if (FrameRenderer.supportsMediaStream) {
        const stream = this._renderer.getMediaStream();
        if (stream) {
          this._videoEl = document.createElement("video");
          this._videoEl.srcObject = stream;
          this._videoEl.style.width = "100%";
          this._videoEl.style.height = "100%";
          this.el().appendChild(this._videoEl);
        }
      } else {
        this._canvasEl = document.createElement("canvas");
        this._canvasEl.style.width = "100%";
        this._canvasEl.style.height = "100%";
        this.el().appendChild(this._canvasEl);
        this._renderer.initCanvas(this._canvasEl);
      }

      this.triggerReady();
    } catch {
      this.trigger("error");
    }
  }

  createEl(): HTMLDivElement {
    const el = document.createElement("div");
    el.className = "vjs-hevc-wasm";
    el.style.width = "100%";
    el.style.height = "100%";
    el.style.overflow = "hidden";
    return el;
  }

  async setSrc(src: string): Promise<void> {
    this._frames = [];
    this._currentFrame = 0;
    this._currentTime = 0;

    try {
      const response = await fetch(src);
      const data = new Uint8Array(await response.arrayBuffer());

      if (isFMP4(data)) {
        await this._decodeFMP4(data);
      } else {
        await this._decodeRaw(data);
      }

      this._duration = this._frames.length / this._fps;
      this.trigger("loadedmetadata");
      this.trigger("loadeddata");
      this.trigger("canplay");
    } catch {
      this.trigger("error");
    }
  }

  private async _decodeRaw(data: Uint8Array): Promise<void> {
    if (!this._decoder) return;
    const { frames } = this._decoder.decode(data);
    this._frames = frames;
  }

  private async _decodeFMP4(data: Uint8Array): Promise<void> {
    if (!this._decoder) return;

    await this._demuxer.parseInit(data);
    const samples = this._demuxer.parseSegment(data);
    if (samples.length === 0) return;

    // Calculate fps from sample durations
    const track = this._demuxer.videoTrack;
    if (track && samples.length > 1) {
      const avgDuration = samples.reduce((sum, s) => sum + s.duration, 0) / samples.length;
      this._fps = track.timescale / avgDuration;
    }

    // Reassemble NAL units with start codes for batch decode
    const totalSize = samples.reduce((sum, s) => {
      return sum + s.nalUnits.reduce((ns, n) => ns + 4 + n.length, 0);
    }, 0);

    const raw = new Uint8Array(totalSize);
    let offset = 0;
    for (const sample of samples) {
      for (const nal of sample.nalUnits) {
        raw[offset++] = 0;
        raw[offset++] = 0;
        raw[offset++] = 0;
        raw[offset++] = 1;
        raw.set(nal, offset);
        offset += nal.length;
      }
    }

    const { frames } = this._decoder.decode(raw);
    this._frames = frames;
  }

  play(): Promise<void> {
    if (this._playing) return Promise.resolve();
    this._playing = true;
    this.trigger("play");
    this.trigger("playing");

    this._playTimer = setInterval(() => {
      if (this._currentFrame >= this._frames.length - 1) {
        this.pause();
        this.trigger("ended");
        return;
      }
      this._currentFrame++;
      this._currentTime = this._currentFrame / this._fps;
      this._renderCurrentFrame();
      this.trigger("timeupdate");
    }, 1000 / this._fps);

    return Promise.resolve();
  }

  pause(): void {
    this._playing = false;
    if (this._playTimer) {
      clearInterval(this._playTimer);
      this._playTimer = null;
    }
    this.trigger("pause");
  }

  paused(): boolean {
    return !this._playing;
  }

  currentTime(seconds?: number): number {
    if (typeof seconds !== "undefined") {
      this._currentFrame = Math.round(seconds * this._fps);
      this._currentFrame = Math.max(0, Math.min(this._currentFrame, this._frames.length - 1));
      this._currentTime = this._currentFrame / this._fps;
      this._renderCurrentFrame();
      this.trigger("timeupdate");
      this.trigger("seeked");
    }
    return this._currentTime;
  }

  duration(): number {
    return this._duration;
  }

  buffered(): unknown {
    return (videojs as VideoJsStatic)?.time?.createTimeRanges?.(0, this._duration) ?? { length: 0 };
  }

  private _renderCurrentFrame(): void {
    const frame = this._frames[this._currentFrame];
    if (!frame) return;

    const timestamp = (this._currentFrame / this._fps) * 1_000_000;
    this._renderer.renderFrame(frame, timestamp);
  }

  dispose(): void {
    this.pause();
    this._decoder?.destroy();
    this._renderer.destroy();
    this._decoder = null;
    super.dispose();
  }

  static isSupported(): boolean {
    return typeof WebAssembly !== "undefined";
  }

  static canPlayType(type: string): string {
    if (/^video\/mp4.*codecs.*hev1/i.test(type)) return "maybe";
    if (/^video\/mp4.*codecs.*hvc1/i.test(type)) return "maybe";
    if (type === "application/x-hevc") return "maybe";
    if (type === "video/hevc") return "maybe";
    return "";
  }

  static canPlaySource(srcObj: SourceObject): string {
    return HevcWasmTech.canPlayType(srcObj.type || "");
  }
}

/** Detect fMP4 by checking for 'ftyp', 'moov', or 'styp' box at start */
function isFMP4(data: Uint8Array): boolean {
  if (data.length < 8) return false;
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const type = view.getUint32(4);
  return type === 0x66747970 || type === 0x6D6F6F76 || type === 0x73747970;
}

// Register the tech with Video.js (if available)
if ((videojs as VideoJsStatic)?.registerTech) {
  (videojs as VideoJsStatic).registerTech("HevcWasm", HevcWasmTech);
}

export { HevcWasmTech };
