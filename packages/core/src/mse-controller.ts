/**
 * MSE Controller — Manages MediaSource and SourceBuffer for H.264 fMP4 playback.
 */

export class MSEController {
  private _video: HTMLVideoElement;
  private _mediaSource: MediaSource | null = null;
  private _sourceBuffer: SourceBuffer | null = null;
  private _queue: Uint8Array[] = [];
  private _updating = false;
  private _initDone = false;

  constructor(video: HTMLVideoElement) {
    this._video = video;
  }

  /**
   * Initialize: create MediaSource, attach to video, create SourceBuffer.
   * @param initSegment The fMP4 init segment (ftyp + moov)
   * @param codec MIME codec string (default: avc1.42001f)
   */
  async init(initSegment: Uint8Array, codec = "avc1.640028"): Promise<void> {
    this._mediaSource = new MediaSource();
    this._video.src = URL.createObjectURL(this._mediaSource);

    await new Promise<void>((resolve) => {
      this._mediaSource!.addEventListener("sourceopen", () => resolve(), { once: true });
    });

    const mimeType = `video/mp4; codecs="${codec}"`;
    if (!MediaSource.isTypeSupported(mimeType)) {
      throw new Error(`MIME type not supported: ${mimeType}`);
    }

    this._sourceBuffer = this._mediaSource.addSourceBuffer(mimeType);
    this._sourceBuffer.addEventListener("updateend", () => this._onUpdateEnd());

    // Append init segment
    await this._append(initSegment);
    this._initDone = true;
  }

  /**
   * Append a media segment. Queues if a previous append is still in progress.
   */
  async appendSegment(segment: Uint8Array): Promise<void> {
    if (!this._initDone) throw new Error("MSE not initialized. Call init() first.");
    await this._append(segment);
  }

  /**
   * Remove buffered data older than keepSeconds before current time.
   */
  async trimBuffer(keepSeconds = 30): Promise<void> {
    if (!this._sourceBuffer || this._sourceBuffer.updating) return;
    const currentTime = this._video.currentTime;
    const removeEnd = currentTime - keepSeconds;
    if (removeEnd <= 0) return;

    return new Promise<void>((resolve) => {
      const sb = this._sourceBuffer!;
      const handler = () => { sb.removeEventListener("updateend", handler); resolve(); };
      sb.addEventListener("updateend", handler);
      sb.remove(0, removeEnd);
    });
  }

  /** Signal end of stream */
  endOfStream(): void {
    if (this._mediaSource?.readyState === "open") {
      this._mediaSource.endOfStream();
    }
  }

  /** Get buffered time ranges */
  get buffered(): TimeRanges | null {
    return this._sourceBuffer?.buffered ?? null;
  }

  /** Get media duration */
  get duration(): number {
    return this._mediaSource?.duration ?? 0;
  }

  /** Clean up resources */
  destroy(): void {
    if (this._sourceBuffer && this._mediaSource?.readyState === "open") {
      try { this._mediaSource.removeSourceBuffer(this._sourceBuffer); } catch {}
    }
    if (this._video.src) {
      URL.revokeObjectURL(this._video.src);
      this._video.removeAttribute("src");
    }
    this._mediaSource = null;
    this._sourceBuffer = null;
    this._queue = [];
  }

  private _append(data: Uint8Array): Promise<void> {
    return new Promise<void>((resolve, reject) => {
      if (!this._sourceBuffer) { reject(new Error("No SourceBuffer")); return; }

      const doAppend = () => {
        try {
          this._sourceBuffer!.appendBuffer(data as BufferSource);
          this._updating = true;
          const handler = () => {
            this._sourceBuffer!.removeEventListener("updateend", handler);
            this._sourceBuffer!.removeEventListener("error", errHandler);
            resolve();
          };
          const errHandler = () => {
            this._sourceBuffer!.removeEventListener("updateend", handler);
            this._sourceBuffer!.removeEventListener("error", errHandler);
            reject(new Error("SourceBuffer error during append"));
          };
          this._sourceBuffer!.addEventListener("updateend", handler);
          this._sourceBuffer!.addEventListener("error", errHandler);
        } catch (e) {
          if (e instanceof DOMException && e.name === "QuotaExceededError") {
            this.trimBuffer(10).then(() => doAppend()).catch(reject);
          } else {
            reject(e);
          }
        }
      };

      if (this._sourceBuffer.updating) {
        this._queue.push(data);
        const check = () => {
          if (!this._queue.includes(data)) { resolve(); return; }
          setTimeout(check, 10);
        };
        check();
      } else {
        doAppend();
      }
    });
  }

  private _onUpdateEnd(): void {
    this._updating = false;
    if (this._queue.length > 0 && this._sourceBuffer && !this._sourceBuffer.updating) {
      const next = this._queue.shift()!;
      try {
        this._sourceBuffer.appendBuffer(next as BufferSource);
      } catch {}
    }
  }
}
