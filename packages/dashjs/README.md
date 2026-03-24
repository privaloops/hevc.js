# @hevcjs/dashjs

HEVC/H.265 playback plugin for [dash.js](https://github.com/Dash-Industry-Forum/dash.js). Transparently transcodes HEVC segments to H.264 via WebAssembly, enabling HEVC DASH streams on browsers without native HEVC support (Windows without paid codec, older Chrome, etc.).

## Install

```bash
npm install hevc.js dashjs
```

## Usage

```js
import dashjs from 'dashjs';
import { attachHevcSupport } from 'hevc.js/dashjs';

const video = document.querySelector('video');
const player = dashjs.MediaPlayer().create();

// One line — that's it
attachHevcSupport(player);

player.initialize(video, 'https://example.com/stream/manifest.mpd', true);
```

## How It Works

When `attachHevcSupport(player)` is called:

1. **Patches `MediaSource.isTypeSupported()`** — returns `true` for HEVC codecs (hev1/hvc1)
2. **Patches `navigator.mediaCapabilities.decodingInfo()`** — same, for dash.js 4.x+
3. **Registers a custom capabilities filter** on the dash.js player — accepts HEVC representations
4. **Intercepts `addSourceBuffer()`** — when dash.js creates an HEVC SourceBuffer, creates an H.264 one instead and returns a Proxy
5. **The Proxy SourceBuffer** intercepts `appendBuffer()`:
   - Init segments: extracts VPS/SPS/PPS from hvcC
   - Media segments: demux (mp4box.js) → decode HEVC (WASM) → encode H.264 (WebCodecs) → mux fMP4 → append to real H.264 SourceBuffer
6. **Proper `updating` state management** — the proxy reports `updating = true` during transcoding, so dash.js waits between segments (no flooding)

Audio and subtitle tracks pass through untouched.

## API

### `attachHevcSupport(player, config?)`

```ts
import { attachHevcSupport } from 'hevc.js/dashjs';

const cleanup = attachHevcSupport(player, {
  workerUrl: '/transcode-worker.js',   // Web Worker for off-main-thread transcoding
  wasmUrl: '/path/to/hevc-decode.js',  // WASM glue location (optional)
  fps: 25,                              // Target framerate (optional, default: 25)
  bitrate: 4_000_000,                   // H.264 encode bitrate (optional)
});

// Remove patches when done
cleanup();
```

### Lower-level API

```ts
import { installMSEIntercept, uninstallMSEIntercept } from 'hevc.js/dashjs';
import { SegmentTranscoder } from 'hevc.js/dashjs';

// Manual MSE patching (without dash.js)
installMSEIntercept({ wasmUrl: '/hevc-decode.js' });

// Or use the transcoder directly
const transcoder = new SegmentTranscoder({ fps: 25 });
await transcoder.init();
await transcoder.processInitSegment(initSegmentBytes);
const h264Segment = await transcoder.processMediaSegment(mediaSegmentBytes);
```

## Requirements

- Chrome 94+ (WebCodecs VideoEncoder)
- Secure Context (HTTPS or localhost)

## License

MIT
