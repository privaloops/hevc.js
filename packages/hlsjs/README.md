# @hevcjs/hlsjs

HEVC/H.265 playback plugin for [hls.js](https://github.com/video-dev/hls.js). Transparently transcodes HEVC segments to H.264 via WebAssembly, enabling HEVC HLS streams on browsers without native HEVC support.

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

// One line — patches MSE to transcode HEVC to H.264
const cleanup = attachHevcSupport(hls);

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

- Chrome 94+ (WebCodecs VideoEncoder)
- Secure Context (HTTPS or localhost)

## License

MIT
