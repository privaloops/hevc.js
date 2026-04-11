# @hevcjs/hlsjs

HEVC/H.265 playback plugin for [hls.js](https://github.com/video-dev/hls.js). Software fallback for the ~6% of browsers without native HEVC support (Chrome < 107, Firefox Linux, old GPUs). Transparently transcodes HEVC segments to H.264 via WebAssembly. When native HEVC is available, the plugin detects it and does nothing.

## Install

```bash
npm install hevc.js hls.js
```

## Usage

```js
import Hls from 'hls.js';
import { attachHevcSupport } from 'hevc.js/hlsjs';

const video = document.querySelector('video');
const hls = new Hls();

// Patches MSE to transcode HEVC to H.264 (in a Web Worker)
const cleanup = attachHevcSupport(hls, {
  workerUrl: '/transcode-worker.js',
});

hls.attachMedia(video);
hls.loadSource('https://example.com/hevc-stream/playlist.m3u8');
```

## How It Works

Same MSE-level interception as `@hevcjs/dashjs` — both hls.js and dash.js use `MediaSource`/`SourceBuffer` identically. The plugin:

1. Patches `MediaSource.isTypeSupported()` to accept HEVC codecs
2. Intercepts `addSourceBuffer()` to create an H.264 SourceBuffer when HEVC is requested
3. Proxies `appendBuffer()` to transcode HEVC segments via WASM before appending

Audio and subtitle tracks pass through untouched.

## Requirements

- Chrome 94+ or Edge 94+ (WebCodecs VideoEncoder with H.264 encoding)
- Secure Context (HTTPS or localhost)

**Firefox**: not supported — `VideoEncoder` H.264 encoding is broken in all current versions ([Bug 1918769](https://bugzilla.mozilla.org/show_bug.cgi?id=1918769)). The plugin detects this and falls back to native AVC playback when the manifest contains both AVC and HEVC levels.

## License

MIT
