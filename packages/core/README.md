# @hevcjs/core

HEVC/H.265 decoder compiled to WebAssembly, with a built-in HEVC-to-H.264 transcoding pipeline. 1080p @ 60fps, 236KB WASM, zero dependencies.

Implemented per **ITU-T H.265 v8** — 716 pages of spec, validated pixel-perfect against ffmpeg on 128 test bitstreams.

## Install

```bash
npm install @hevcjs/core
```

## Usage

### MSE intercept (transparent HEVC playback)

```js
import { installMSEIntercept } from '@hevcjs/core';

// Patches MediaSource so HEVC segments are transcoded to H.264 on-the-fly
installMSEIntercept({ workerUrl: '/transcode-worker.js' });
```

### Segment transcoder (manual control)

```js
import { SegmentTranscoder } from '@hevcjs/core';

const transcoder = new SegmentTranscoder({ fps: 25 });
await transcoder.init();
await transcoder.processInitSegment(initSegmentBytes);
const h264Segment = await transcoder.processMediaSegment(hevcSegmentBytes);
```

## How it works

1. **Demux** — mp4box.js extracts raw HEVC NAL units from fMP4 segments
2. **Decode** — WASM decoder produces YUV frames (spec-compliant, pixel-perfect)
3. **Encode** — WebCodecs `VideoEncoder` compresses to H.264
4. **Mux** — Custom fMP4 muxer wraps H.264 in ISO BMFF with correct timestamps

All heavy work runs in a Web Worker. No `SharedArrayBuffer` required, no special server headers needed.

## Performance

Single-threaded, Apple Silicon:

| Resolution | WASM (Chrome) |
|---|---|
| 1080p decode | 61 fps |
| 4K decode | 21 fps |
| 1080p transcode | ~2.5x realtime |

## Requirements

- WebAssembly + Web Workers
- WebCodecs (Chrome/Edge 94+) for transcoding
- Secure Context (HTTPS or localhost)

## License

MIT
