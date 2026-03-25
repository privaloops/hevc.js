# hevc-decode

**Play HEVC/H.265 video in any browser. No codec license. No plugin. No install.**

A from-scratch HEVC decoder written in C++17, compiled to WebAssembly, with drop-in plugins for hls.js and dash.js. The browser only supports H.264? We transcode HEVC to H.264 in real-time, client-side, inside a Web Worker.

1080p @ 60fps. 236KB WASM. Zero dependencies.

## The problem

HEVC (H.265) delivers 50% better compression than H.264 at the same quality. But you can't ship HEVC and assume it plays everywhere:

| Platform | HEVC support |
|---|---|
| **Safari** (macOS/iOS) | Native, hardware-accelerated |
| **Chrome/Edge** (macOS) | Hardware-accelerated via VideoToolbox |
| **Chrome/Edge** (Windows) | Requires [HEVC Video Extensions](https://apps.microsoft.com/detail/9nmzlz57r3t7) ($0.99) — not pre-installed by default. Some OEM devices ship with a free version, but it's not guaranteed |
| **Firefox** (all platforms) | Partial support since v139, with [DRM limitations on Windows](https://connect.mozilla.org/t5/discussions/request-hevc-h-265-drm-support-on-windows-firefox/td-p/106471) |
| **Linux** (all browsers) | Effectively unsupported |

Content providers either avoid HEVC, maintain dual AVC/HEVC pipelines, or accept broken playback for a portion of their audience.

## The solution

hevc-decode intercepts the player's MediaSource pipeline and transparently transcodes HEVC segments to H.264 before they reach the browser's decoder:

```
HEVC stream ──► demux (mp4box.js) ──► decode (WASM) ──► encode (WebCodecs) ──► H.264 to MSE
```

The player (hls.js, dash.js) doesn't know the transcoding is happening. It requests HEVC segments, and our MSE intercept delivers H.264 to the SourceBuffer. Seek, ABR switching, and live streams work normally.

## Quick start

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

### C/C++ library

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

## Architecture

```
hevc-decode/
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
│   └── dashjs/             @hevcjs/dashjs — dash.js plugin
│
├── demo/                   Browser demos (DASH + HLS)
└── tests/                  Unit tests + 128 oracle tests (pixel-perfect vs ffmpeg)
```

## How the transcoding works

1. **MSE intercept** — Patches `MediaSource.addSourceBuffer()` before the player initializes. When the player creates an HEVC SourceBuffer, we return a proxy that accepts HEVC data but feeds H.264 to the real SourceBuffer.

2. **Worker pipeline** — All heavy work runs in a Web Worker:
   - **Demux**: mp4box.js extracts raw HEVC NAL units from fMP4 segments
   - **Decode**: WASM decoder produces YUV frames (spec-compliant, pixel-perfect)
   - **Encode**: WebCodecs `VideoEncoder` compresses to H.264
   - **Mux**: Custom fMP4 muxer wraps H.264 in ISO BMFF with correct timestamps

3. **Transparent to the player** — The proxy reports `updating`, fires `updatestart`/`updateend` events, and returns real `buffered` ranges. The player's buffer management, ABR logic, and seek handling work unmodified.

## Performance

Single-threaded, Apple Silicon (M-series):

| | Native C++ | WASM (Chrome) |
|---|---|---|
| **1080p decode** | 76 fps | 61 fps |
| **4K decode** | 28 fps | 21 fps |
| **1080p transcode** | — | ~2.5x realtime (6s segment in 2.4s) |

The WASM decoder is within 20% of native C++ performance. The full transcode pipeline (demux + decode + encode + mux) runs at ~2.5x realtime for 1080p, enough for smooth playback with buffer headroom.

### Bottleneck breakdown (1080p segment)

| Stage | Time | Share |
|---|---|---|
| WASM decode | ~1.7s | 70% |
| WebCodecs encode | ~0.5s | 20% |
| Demux + mux | ~0.05s | 2% |
| Overhead | ~0.2s | 8% |

CABAC arithmetic decoding accounts for ~33% of decode time. The hot path (decode_decision, renormalize, bypass) is inlined in the header with branchless optimizations and batched renormalization.

## Spec conformance

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

## Build

### Native (debug + tests)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure    # 128 tests
```

### WebAssembly

Requires [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html).

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-wasm -DBUILD_WASM=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm
# Output: build-wasm/hevc-decode.js + hevc-decode.wasm (236KB)
```

### Demo (DASH + HLS player)

```bash
pnpm install
pnpm build:demo     # Builds WASM + JS bundles + copies assets
npx serve demo      # Open http://localhost:3000
```

## C API Reference

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

## Browser compatibility

The transcoding pipeline requires:
- **WebCodecs VideoEncoder** — Chrome 94+, Edge 94+, Safari 16.4+
- **WebAssembly** — All modern browsers
- **Web Workers** — All modern browsers

Firefox does not yet support WebCodecs VideoEncoder. When it does, hevc-decode will work there too.

## License

MIT
