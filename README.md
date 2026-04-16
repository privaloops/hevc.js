# hevc.js

[![Build](https://github.com/privaloops/hevc.js/actions/workflows/build.yml/badge.svg)](https://github.com/privaloops/hevc.js/actions/workflows/build.yml)
[![Tests](https://github.com/privaloops/hevc.js/actions/workflows/test.yml/badge.svg)](https://github.com/privaloops/hevc.js/actions/workflows/test.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

**Play HEVC/H.265 video in Chrome & Edge without native support. No plugin. No install. No server changes.**

A from-scratch HEVC decoder written in C++17, compiled to WebAssembly, with a drop-in plugin for dash.js. Transcodes HEVC to H.264 in real-time, client-side, via WebCodecs inside a Web Worker. Firefox support (Canvas rendering fallback) is planned.

1080p @ 60fps. 236KB WASM. Zero dependencies. No special server headers required.

Built in 8 days by one developer, assisted by AI — [read the story](https://hevcjs.dev).

---

## JavaScript plugin

### Installation

```bash
npm install @hevcjs/dashjs-plugin
```

### dash.js

```js
import dashjs from 'dashjs';
import { attachHevcSupport } from '@hevcjs/dashjs-plugin';

const player = dashjs.MediaPlayer().create();
attachHevcSupport(player, { workerUrl: './transcode-worker.js' });
player.initialize(videoElement, 'https://example.com/manifest.mpd', true);
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

hevc.js transcodes HEVC to H.264 client-side. This requires two things from the browser: **WebAssembly** (to run the HEVC decoder) and **WebCodecs VideoEncoder with H.264 support** (to re-encode the decoded frames). When native HEVC is available, the plugin detects it and does nothing — zero overhead.

**Detection strategy**: `MediaSource.isTypeSupported()` can lie (Firefox on Windows reports HEVC support even without the HEVC Video Extension installed). hevc.js verifies native support by actually creating a SourceBuffer — if that fails, it falls back to transcoding.

| Browser | Native HEVC | hevc.js activates? | Transcoding works? | Why |
|---|---|---|---|---|
| **Safari 13+** | Yes (VideoToolbox) | No — native | — | Hardware decode via macOS/iOS |
| **Chrome/Edge 107+** (Win/Mac) | Yes (hardware) | No — native | — | GPU decode via platform APIs |
| **Chrome/Edge 94–106** (Win/Mac) | No | **Yes** | **Yes** | WebCodecs H.264 encoder available (hardware or software) |
| **Chrome/Edge < 94** | No | No | No | WebCodecs API does not exist |
| **Firefox 133+** (Win) | Yes (Media Foundation) | No — native | — | Requires [HEVC Video Extension](https://apps.microsoft.com/detail/9nmzlz57r3t7) installed |
| **Firefox 133+** (Win, no extension) | Reported but fake | **Yes** | **Yes** | SourceBuffer probe catches the false positive, falls back to transcoding |
| **Firefox** (Mac) | Yes (VideoToolbox) | No — native | — | Hardware decode via macOS |
| **Firefox** (Linux) | No | **Yes** | Depends | Requires a working H.264 encoder via WebCodecs — works if hardware encoder available, fails on headless/VM setups |
| **Chrome** (Linux, no VAAPI) | No | **Yes** | **Yes** | Software H.264 encode via WebCodecs |

**Requirements** (supported by all modern browsers):
- **WebAssembly** + **Web Workers**
- **Secure Context** (HTTPS or localhost) — WebCodecs is not available on plain HTTP
- **WebCodecs VideoEncoder** with H.264 support — this is the main limiting factor

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
│   └── dashjs-plugin/      @hevcjs/dashjs-plugin — dash.js plugin
│
├── demo/                   Browser demos (DASH)
└── tests/                  Unit tests + 128 oracle tests (pixel-perfect vs ffmpeg)
```

## Demos

**[Live demos](https://hevcjs.dev/demo/)** — try each plugin in your browser:

| Demo | Description |
|---|---|
| [Decoder](https://hevcjs.dev/demo/) | Raw WASM decoder — drop a .265 file, frame-by-frame playback |
| [dash.js](https://hevcjs.dev/demo/dash.html) | HEVC DASH streams via dash.js + WASM transcoding |

Each demo includes a **"Force transcoding"** toggle to bypass native HEVC detection — useful for testing the WASM pipeline on browsers that already support HEVC.

### Run locally

```bash
pnpm install
pnpm build:demo     # Builds WASM + JS bundles + copies assets
npx serve demo      # Open http://localhost:3000
```

## License

MIT — see [LICENSE](LICENSE).

HEVC/H.265 may be covered by patents managed by Access Advance and other patent pools. This software is an independent implementation and does not include or grant any patent license. Users are responsible for evaluating patent obligations in their jurisdiction and use case.

Media samples use [Big Buck Bunny](https://peach.blender.org/) (CC-BY 3.0, Blender Foundation). See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for full attribution.
