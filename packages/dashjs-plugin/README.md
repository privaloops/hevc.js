# @hevcjs/dashjs-plugin

HEVC/H.265 playback plugin for [dash.js](https://github.com/Dash-Industry-Forum/dash.js). Transparently transcodes HEVC segments to H.264 via WebAssembly when native HEVC is unavailable. When native HEVC is available, the plugin detects it and does nothing.

## Install

```bash
npm install @hevcjs/dashjs-plugin dashjs
```

## Setup

Copy the static assets from `@hevcjs/core` to your public directory:

```bash
cp node_modules/@hevcjs/core/dist/transcode-worker.js public/
cp node_modules/@hevcjs/core/dist/wasm/hevc-decode.js public/
cp node_modules/@hevcjs/core/dist/wasm/hevc-decode.wasm public/
```

Adjust `public/` to match your project structure.

## Usage

```js
import dashjs from 'dashjs';
import { attachHevcSupport } from '@hevcjs/dashjs-plugin';

const video = document.querySelector('video');
const player = dashjs.MediaPlayer().create();

await attachHevcSupport(player, {
  workerUrl: '/transcode-worker.js',
  wasmUrl: '/hevc-decode.js',
});

player.initialize(video, 'https://example.com/stream/manifest.mpd', true);
```

## How It Works

When `attachHevcSupport(player)` is called:

1. **Probes native HEVC support** — creates a real SourceBuffer (not just `isTypeSupported`, which can lie on Firefox)
2. **If native HEVC works** — does nothing, zero overhead
3. **If not** — patches `MediaSource.addSourceBuffer()` to intercept HEVC and return an H.264 proxy
4. **The proxy SourceBuffer** intercepts `appendBuffer()`:
   - Init segments: extracts VPS/SPS/PPS from hvcC
   - Media segments: demux (mp4box.js) → decode HEVC (WASM) → encode H.264 (WebCodecs) → mux fMP4 → append to real H.264 SourceBuffer
5. **Proper `updating` state management** — the proxy reports `updating = true` during transcoding, so dash.js waits between segments

Audio and subtitle tracks pass through untouched.

## API

### `attachHevcSupport(player, config?)`

```ts
const cleanup = await attachHevcSupport(player, {
  workerUrl: '/transcode-worker.js',
  wasmUrl: '/hevc-decode.js',
  fps: 25,              // Target framerate (optional, default: 25)
  bitrate: 4_000_000,   // H.264 encode bitrate (optional)
  forceTranscode: false, // Bypass native HEVC detection (optional)
});

// Remove patches when done
cleanup();
```

## Requirements

- Chrome 94+, Edge 94+, or Firefox with WebCodecs H.264 encoding support
- Secure Context (HTTPS or localhost)
- dash.js >= 4.0.0

## License

MIT
