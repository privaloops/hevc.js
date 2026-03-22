import type { HEVCFrame, HEVCStreamInfo, DecodeResult, DecoderOptions } from "./types.js";

/**
 * Emscripten module interface (subset we use)
 */
interface EmscriptenModule {
  cwrap(name: string, returnType: string | null, argTypes: string[]): Function;
  _malloc(size: number): number;
  _free(ptr: number): void;
  getValue(ptr: number, type: string): number;
  HEAPU8: Uint8Array;
  HEAPU16: Uint16Array;
}

interface DecoderAPI {
  create: () => number;
  destroy: (dec: number) => void;
  decode: (dec: number, ptr: number, size: number) => number;
  getFrameCount: (dec: number) => number;
  getFrame: (dec: number, index: number, framePtr: number) => number;
  getInfo: (dec: number, infoPtr: number) => number;
}

/**
 * HEVC/H.265 Decoder — JavaScript wrapper for the WASM module.
 *
 * @example
 * ```ts
 * const decoder = await HEVCDecoder.create();
 * const { frames, info } = decoder.decode(bitstreamBytes);
 * console.log(`${info.width}x${info.height}, ${frames.length} frames`);
 * decoder.destroy();
 * ```
 */
export class HEVCDecoder {
  private _m: EmscriptenModule;
  private _api: DecoderAPI;
  private _dec: number;

  private constructor(module: EmscriptenModule) {
    this._m = module;
    this._api = {
      create: module.cwrap("hevc_decoder_create", "number", []) as () => number,
      destroy: module.cwrap("hevc_decoder_destroy", null, ["number"]) as (dec: number) => void,
      decode: module.cwrap("hevc_decoder_decode", "number", ["number", "number", "number"]) as (dec: number, ptr: number, size: number) => number,
      getFrameCount: module.cwrap("hevc_decoder_get_frame_count", "number", ["number"]) as (dec: number) => number,
      getFrame: module.cwrap("hevc_decoder_get_frame", "number", ["number", "number", "number"]) as (dec: number, index: number, framePtr: number) => number,
      getInfo: module.cwrap("hevc_decoder_get_info", "number", ["number", "number"]) as (dec: number, infoPtr: number) => number,
    };
    this._dec = this._api.create();
    if (!this._dec) throw new Error("Failed to create HEVC decoder");
  }

  /**
   * Create a new decoder instance. Loads the WASM module.
   */
  static async create(options?: DecoderOptions): Promise<HEVCDecoder> {
    const wasmUrl = options?.wasmUrl;
    const loadModule = async (url: string): Promise<EmscriptenModule> => {
      const mod = await import(/* @vite-ignore */ url);
      const fn = mod.default ?? mod;
      const factoryOpts: Record<string, unknown> = {};
      if (options?.wasmBinaryUrl) {
        factoryOpts.locateFile = () => options.wasmBinaryUrl;
      }
      return fn(factoryOpts) as Promise<EmscriptenModule>;
    };

    const module = await loadModule(wasmUrl ?? "./wasm/hevc-decode.js");
    return new HEVCDecoder(module);
  }

  /**
   * Decode a complete HEVC bitstream.
   * @param data Raw .265 bitstream bytes
   */
  decode(data: Uint8Array): DecodeResult {
    const m = this._m;
    const ptr = m._malloc(data.length);
    try {
      m.HEAPU8.set(data, ptr);
      const ret = this._api.decode(this._dec, ptr, data.length);
      if (ret !== 0) throw new Error(`Decode failed (code ${ret})`);

      const count = this._api.getFrameCount(this._dec);
      const frames: HEVCFrame[] = [];
      for (let i = 0; i < count; i++) {
        const frame = this._extractFrame(i);
        if (frame) frames.push(frame);
      }

      const info = this._extractInfo();
      return { frames, info };
    } finally {
      m._free(ptr);
    }
  }

  /** Number of decoded frames available */
  get frameCount(): number {
    return this._api.getFrameCount(this._dec);
  }

  /** Get stream info (available after decode) */
  get info(): HEVCStreamInfo | null {
    return this._extractInfo();
  }

  private _extractFrame(index: number): HEVCFrame | null {
    const m = this._m;
    const framePtr = m._malloc(48);
    try {
      const ret = this._api.getFrame(this._dec, index, framePtr);
      if (ret !== 0) return null;

      const yPtr    = m.getValue(framePtr, "*");
      const cbPtr   = m.getValue(framePtr + 4, "*");
      const crPtr   = m.getValue(framePtr + 8, "*");
      const width   = m.getValue(framePtr + 12, "i32");
      const height  = m.getValue(framePtr + 16, "i32");
      const strideY = m.getValue(framePtr + 20, "i32");
      const strideC = m.getValue(framePtr + 24, "i32");
      const cw      = m.getValue(framePtr + 28, "i32");
      const ch      = m.getValue(framePtr + 32, "i32");
      const bd      = m.getValue(framePtr + 36, "i32");
      const poc     = m.getValue(framePtr + 40, "i32");

      const y  = copyPlane(m, yPtr, width, height, strideY);
      const cb = copyPlane(m, cbPtr, cw, ch, strideC);
      const cr = copyPlane(m, crPtr, cw, ch, strideC);

      return { y, cb, cr, width, height, chromaWidth: cw, chromaHeight: ch, bitDepth: bd, poc };
    } finally {
      m._free(framePtr);
    }
  }

  private _extractInfo(): HEVCStreamInfo | null {
    const m = this._m;
    const infoPtr = m._malloc(24);
    try {
      const ret = this._api.getInfo(this._dec, infoPtr);
      if (ret !== 0) return null;
      return {
        width:        m.getValue(infoPtr, "i32"),
        height:       m.getValue(infoPtr + 4, "i32"),
        bitDepth:     m.getValue(infoPtr + 8, "i32"),
        chromaFormat: m.getValue(infoPtr + 12, "i32"),
        profile:      m.getValue(infoPtr + 16, "i32"),
        level:        m.getValue(infoPtr + 20, "i32"),
      };
    } finally {
      m._free(infoPtr);
    }
  }

  /** Release decoder resources */
  destroy(): void {
    if (this._dec) {
      this._api.destroy(this._dec);
      this._dec = 0;
    }
  }
}

/** Copy a YUV plane from WASM HEAPU16, handling stride != width */
function copyPlane(m: EmscriptenModule, ptr: number, width: number, height: number, stride: number): Uint16Array {
  const out = new Uint16Array(width * height);
  const base = ptr >> 1;
  for (let y = 0; y < height; y++) {
    out.set(m.HEAPU16.subarray(base + y * stride, base + y * stride + width), y * width);
  }
  return out;
}
