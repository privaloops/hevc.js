# hevc.js

**Play HEVC/H.265 video in any browser. No codec license. No plugin. No install.**

A from-scratch HEVC decoder written in C++17, compiled to WebAssembly, with drop-in plugins for hls.js, dash.js and Video.js. The browser only supports H.264? We transcode HEVC to H.264 in real-time, client-side, inside a Web Worker.

1080p @ 60fps. 236KB WASM. Zero dependencies. No special server headers required.

Built in 8 days by one developer, assisted by AI — [read the story](https://hevcjs.dev).

---

## JavaScript plugins

### Installation

```bash
npm install @hevcjs/hlsjs   # for hls.js
npm install @hevcjs/dashjs   # for dash.js
npm install @hevcjs/videojs  # for Video.js
```

### hls.js

```js
import Hls from 'hls.js';
import { attachHevcSupport } from '@hevcjs/hlsjs';

const hls = new Hls();
attachHevcSupport(hls, { workerUrl: './transcode-worker.js' });
hls.attachMedia(videoElement);
hls.loadSource('https://example.com/stream.m3u8');
```

### dash.js

```js
import dashjs from 'dashjs';
import { attachHevcSupport } from '@hevcjs/dashjs';

const player = dashjs.MediaPlayer().create();
attachHevcSupport(player, { workerUrl: './transcode-worker.js' });
player.initialize(videoElement, 'https://example.com/manifest.mpd', true);
```

### Video.js

```js
import videojs from 'video.js';
import { attachHevcSupport } from '@hevcjs/videojs';

attachHevcSupport({ workerUrl: './transcode-worker.js' });
const player = videojs('my-video');
```

### How the transcoding works

1. **MSE intercept** — Patches `MediaSource.addSourceBuffer()` before the player initializes. When the player creates an HEVC SourceBuffer, we return a proxy that accepts HEVC data but feeds H.264 to the real SourceBuffer.

2. **Worker pipeline** — All heavy work runs in a Web Worker:
   - **Demux**: mp4box.js extracts raw HEVC NAL units from fMP4 segments
   - **Decode**: WASM decoder produces YUV frames (spec-compliant, pixel-perfect)
   - **Encode**: WebCodecs `VideoEncoder` compresses to H.264
   - **Mux**: Custom fMP4 muxer wraps H.264 in ISO BMFF with correct timestamps

3. **Transparent to the player** — The proxy reports `updating`, fires `updatestart`/`updateend` events, and returns real `buffered` ranges. The player's buffer management, ABR logic, and seek handling work unmodified.

**Tradeoff**: the software fallback introduces 2-3s of startup latency on the first segment (vs instant playback with native hardware decode). Once buffered, playback is smooth. When native HEVC is available, hevc.js detects it and does nothing.

### Browser compatibility

**~94% of browsers play HEVC natively** (hardware decode). hevc.js activates only for the ~6% that don't:

| Browser | Native HEVC | hevc.js needed? | Transcoding works? |
|---|---|---|---|
| **Safari** 13+ | Yes (hardware) | No — bypassed | — |
| **Chrome/Edge 107+** (Win/Mac) | Yes (hardware GPU) | No — bypassed | — |
| **Chrome 94-106** (all platforms) | No | **Yes** | Yes (WebCodecs H.264) |
| **Chrome < 94** | No | **Yes** | No (no WebCodecs) — falls back to AVC |
| **Firefox 137+** (Windows) | Partial (hardware) | No — bypassed | — |
| **Firefox** (Linux, older) | No | **Yes** | No — Firefox H.264 encoding broken ([Bug 1918769](https://bugzilla.mozilla.org/show_bug.cgi?id=1918769)), falls back to AVC |
| **Linux** (Chrome, no VAAPI) | No | **Yes** | Yes (software encode) |

Other requirements (supported by all modern browsers):
- **WebAssembly**
- **Web Workers**
- **Secure Context** (HTTPS or localhost)

No `Cross-Origin-Embedder-Policy` or `Cross-Origin-Opener-Policy` headers needed — the WASM decoder is single-threaded and doesn't use `SharedArrayBuffer`. Works on any static file server.

---

## C/C++ decoder

### C API

```c
#include "wasm/hevc_api.h"

HEVCDecoder* dec = hevc_decoder_create();
hevc_decoder_decode(dec, data, size);

int count = hevc_decoder_get_frame_count(dec);
for (int i = 0; i < count; i++) {
    HEVCFrame frame;
    hevc_decoder_get_frame(dec, i, &frame);
    // frame.y / frame.cb / frame.cr — YUV planes (uint16_t*)
    // frame.width / frame.height — luma dimensions
    // frame.bit_depth — 8 or 10
}

hevc_decoder_destroy(dec);
```

### API reference

```c
// Lifecycle
HEVCDecoder* hevc_decoder_create(void);
void          hevc_decoder_destroy(HEVCDecoder* dec);

// Decode a complete HEVC bitstream (Annex B format)
int hevc_decoder_decode(HEVCDecoder* dec, const uint8_t* data, size_t size);

// Incremental decode (feed NAL units progressively)
int hevc_decoder_feed(HEVCDecoder* dec, const uint8_t* data, size_t size);
int hevc_decoder_drain(HEVCDecoder* dec);

// Access decoded frames (display order)
int hevc_decoder_get_frame_count(HEVCDecoder* dec);
int hevc_decoder_get_frame(HEVCDecoder* dec, int index, HEVCFrame* frame);
```

| HEVCFrame field | Type | Description |
|---|---|---|
| `y`, `cb`, `cr` | `const uint16_t*` | YUV plane pointers |
| `width`, `height` | `int` | Luma dimensions (conformance window applied) |
| `stride_y`, `stride_c` | `int` | Plane strides in samples |
| `bit_depth` | `int` | 8 or 10 |
| `poc` | `int` | Picture Order Count (display order) |

### Build

#### Native (debug + tests)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure    # 128 tests
```

#### WebAssembly

Requires [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html).

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-wasm -DBUILD_WASM=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm
# Output: build-wasm/hevc-decode.js + hevc-decode.wasm (236KB)
```

### Performance

Single-threaded, Apple Silicon (M-series):

| | Native C++ | WASM (Chrome) |
|---|---|---|
| **1080p decode** | 76 fps | 61 fps |
| **4K decode** | 28 fps | 21 fps |
| **1080p transcode** | — | ~2.5x realtime (6s segment in 2.4s) |

The WASM decoder is within 20% of native C++ performance, and reaches **83% of libde265** speed (a mature, 10-year-old optimized HEVC decoder) when both are compiled to WASM.

### Spec conformance

Implemented per **ITU-T H.265 (v8, 08/2021)** — 716 pages, transcribed directly from the spec. Validated pixel-perfect against ffmpeg on 128 test bitstreams.

| Feature | Status |
|---|---|
| CABAC arithmetic decoding (§9.3) | Complete |
| 35 intra prediction modes (§8.4) | Complete |
| Inter prediction — merge, AMVP, TMVP (§8.5) | Complete |
| 8-tap luma / 4-tap chroma interpolation (§8.5.3) | Complete |
| Weighted prediction — default + explicit (§8.5.3.3) | Complete |
| Inverse transform — DCT 4-32, DST 4 (§8.6) | Complete |
| Scaling lists (§8.6.3) | Complete |
| Deblocking filter (§8.7.2) | Complete |
| SAO — edge + band offset (§8.7.3) | Complete |
| 10-bit decoding (Main 10 profile) | Complete |
| Multi-slice (dependent + independent) | Complete |
| Tiles | Parsed + sequential decode |
| WPP (Wavefront Parallel Processing) | Complete |

---

## Architecture

```
hevc.js/
├── src/                    C++17 HEVC decoder (ITU-T H.265 spec-compliant)
│   ├── bitstream/          Annex B parsing, NAL units, RBSP, Exp-Golomb
│   ├── syntax/             VPS, SPS, PPS, slice header parsing
│   ├── decoding/           CABAC, coding tree, intra/inter prediction, transform
│   ├── filters/            Deblocking filter, SAO
│   ├── common/             Types, Picture buffer, thread pool
│   └── wasm/               C API, Emscripten bindings
│
├── packages/
│   ├── core/               @hevcjs/core — WASM decoder + transcoding pipeline
│   ├── hlsjs/              @hevcjs/hlsjs — hls.js plugin
│   ├── dashjs/             @hevcjs/dashjs — dash.js plugin
│   └── videojs/            @hevcjs/videojs — Video.js plugin
│
├── demo/                   Browser demos (DASH + HLS + Video.js)
└── tests/                  Unit tests + 128 oracle tests (pixel-perfect vs ffmpeg)
```

## Demo

```bash
pnpm install
pnpm build:demo     # Builds WASM + JS bundles + copies assets
npx serve demo      # Open http://localhost:3000
```

## License

MIT
