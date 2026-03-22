/**
 * YUV Frame Renderer — converts decoded YUV frames to displayable video.
 *
 * Uses VideoFrame + MediaStreamTrackGenerator when available (Chrome 94+),
 * falls back to WebGL canvas rendering.
 */

import type { HEVCFrame } from "@hevcjs/core";

/**
 * Renderer that converts YUV frames to a <video>-compatible MediaStream
 * or renders directly to a canvas.
 */
export class FrameRenderer {
  private _generator: MediaStreamTrackGenerator<VideoFrame> | null = null;
  private _writer: WritableStreamDefaultWriter<VideoFrame> | null = null;
  private _canvas: OffscreenCanvas | HTMLCanvasElement | null = null;
  private _gl: WebGLRenderingContext | null = null;
  private _program: WebGLProgram | null = null;
  private _texY: WebGLTexture | null = null;
  private _texCb: WebGLTexture | null = null;
  private _texCr: WebGLTexture | null = null;

  /**
   * Check if MediaStreamTrackGenerator is available (Chrome 94+).
   * When available, frames can be piped to a <video> element.
   */
  static get supportsMediaStream(): boolean {
    return typeof MediaStreamTrackGenerator !== "undefined";
  }

  /**
   * Get a MediaStream that can be assigned to a <video>.srcObject.
   * Only available when supportsMediaStream is true.
   */
  getMediaStream(): MediaStream | null {
    if (!FrameRenderer.supportsMediaStream) return null;

    if (!this._generator) {
      this._generator = new MediaStreamTrackGenerator({ kind: "video" });
      this._writer = this._generator.writable.getWriter();
    }

    return new MediaStream([this._generator]);
  }

  /**
   * Render a decoded YUV frame.
   *
   * If MediaStreamTrackGenerator is available, creates a VideoFrame and writes it.
   * Otherwise, renders to the provided canvas via WebGL.
   */
  async renderFrame(frame: HEVCFrame, timestamp: number): Promise<void> {
    if (this._writer) {
      await this._renderToVideoFrame(frame, timestamp);
    } else if (this._gl) {
      this._renderToWebGL(frame);
    }
  }

  /**
   * Initialize WebGL canvas fallback.
   * Call this if MediaStreamTrackGenerator is not supported.
   */
  initCanvas(canvas: HTMLCanvasElement | OffscreenCanvas): void {
    this._canvas = canvas;
    const gl = (canvas as HTMLCanvasElement).getContext("webgl");
    if (!gl) throw new Error("WebGL not supported");
    this._gl = gl;
    this._initWebGL(gl);
  }

  private async _renderToVideoFrame(frame: HEVCFrame, timestamp: number): Promise<void> {
    // Convert Uint16Array planes to Uint8Array for VideoFrame
    const w = frame.width;
    const h = frame.height;
    const cw = frame.chromaWidth;
    const ch = frame.chromaHeight;
    const shift = frame.bitDepth > 8 ? frame.bitDepth - 8 : 0;

    // I420 layout: Y plane, then U plane, then V plane
    const i420 = new Uint8Array(w * h + cw * ch * 2);
    let dst = 0;

    // Y
    for (let i = 0; i < w * h; i++) {
      i420[dst++] = frame.y[i]! >> shift;
    }
    // U (Cb)
    for (let i = 0; i < cw * ch; i++) {
      i420[dst++] = frame.cb[i]! >> shift;
    }
    // V (Cr)
    for (let i = 0; i < cw * ch; i++) {
      i420[dst++] = frame.cr[i]! >> shift;
    }

    const videoFrame = new VideoFrame(i420, {
      format: "I420",
      codedWidth: w,
      codedHeight: h,
      timestamp,
    });

    await this._writer!.write(videoFrame);
    videoFrame.close();
  }

  private _initWebGL(gl: WebGLRenderingContext): void {
    const vs = gl.createShader(gl.VERTEX_SHADER)!;
    gl.shaderSource(vs, VERTEX_SRC);
    gl.compileShader(vs);

    const fs = gl.createShader(gl.FRAGMENT_SHADER)!;
    gl.shaderSource(fs, FRAGMENT_SRC);
    gl.compileShader(fs);

    this._program = gl.createProgram()!;
    gl.attachShader(this._program, vs);
    gl.attachShader(this._program, fs);
    gl.linkProgram(this._program);
    gl.useProgram(this._program);

    // Fullscreen quad
    const buf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(
      gl.ARRAY_BUFFER,
      new Float32Array([-1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0]),
      gl.STATIC_DRAW,
    );

    const aPos = gl.getAttribLocation(this._program, "a_pos");
    const aTex = gl.getAttribLocation(this._program, "a_tex");
    gl.enableVertexAttribArray(aPos);
    gl.enableVertexAttribArray(aTex);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 16, 0);
    gl.vertexAttribPointer(aTex, 2, gl.FLOAT, false, 16, 8);

    this._texY = createTexture(gl, 0);
    this._texCb = createTexture(gl, 1);
    this._texCr = createTexture(gl, 2);

    gl.uniform1i(gl.getUniformLocation(this._program, "u_texY"), 0);
    gl.uniform1i(gl.getUniformLocation(this._program, "u_texCb"), 1);
    gl.uniform1i(gl.getUniformLocation(this._program, "u_texCr"), 2);
  }

  private _renderToWebGL(frame: HEVCFrame): void {
    const gl = this._gl!;
    const canvas = this._canvas!;

    if (canvas instanceof HTMLCanvasElement) {
      canvas.width = frame.width;
      canvas.height = frame.height;
    }
    gl.viewport(0, 0, frame.width, frame.height);

    const shift = frame.bitDepth > 8 ? frame.bitDepth - 8 : 0;

    uploadPlane(gl, this._texY!, 0, frame.y, frame.width, frame.height, shift);
    uploadPlane(gl, this._texCb!, 1, frame.cb, frame.chromaWidth, frame.chromaHeight, shift);
    uploadPlane(gl, this._texCr!, 2, frame.cr, frame.chromaWidth, frame.chromaHeight, shift);

    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
  }

  /** Release all resources */
  destroy(): void {
    this._writer?.close();
    this._generator?.stop();
    this._generator = null;
    this._writer = null;
    this._gl = null;
    this._canvas = null;
  }
}

function createTexture(gl: WebGLRenderingContext, unit: number): WebGLTexture {
  gl.activeTexture(gl.TEXTURE0 + unit);
  const tex = gl.createTexture()!;
  gl.bindTexture(gl.TEXTURE_2D, tex);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  return tex;
}

function uploadPlane(
  gl: WebGLRenderingContext,
  tex: WebGLTexture,
  unit: number,
  data: Uint16Array,
  width: number,
  height: number,
  shift: number,
): void {
  gl.activeTexture(gl.TEXTURE0 + unit);
  gl.bindTexture(gl.TEXTURE_2D, tex);
  const u8 = new Uint8Array(width * height);
  for (let i = 0; i < width * height; i++) {
    u8[i] = Math.min(255, data[i]! >> shift);
  }
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.LUMINANCE, width, height, 0, gl.LUMINANCE, gl.UNSIGNED_BYTE, u8);
}

// BT.709 YUV → RGB shaders
const VERTEX_SRC = `
  attribute vec2 a_pos;
  attribute vec2 a_tex;
  varying vec2 v_tex;
  void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_tex = a_tex;
  }
`;

const FRAGMENT_SRC = `
  precision mediump float;
  varying vec2 v_tex;
  uniform sampler2D u_texY;
  uniform sampler2D u_texCb;
  uniform sampler2D u_texCr;
  void main() {
    float y  = texture2D(u_texY,  v_tex).r;
    float cb = texture2D(u_texCb, v_tex).r - 0.5;
    float cr = texture2D(u_texCr, v_tex).r - 0.5;
    float r = y + 1.5748 * cr;
    float g = y - 0.1873 * cb - 0.4681 * cr;
    float b = y + 1.8556 * cb;
    gl_FragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
  }
`;

// Type augmentations for MediaStreamTrackGenerator (Chrome 94+)
declare class MediaStreamTrackGenerator<T> extends MediaStreamTrack {
  constructor(init: { kind: string });
  readonly writable: WritableStream<T>;
}
