# hevc-decode

A from-scratch HEVC/H.265 decoder written in C++17, compiled to WebAssembly. Pixel-perfect conformance verified against ffmpeg on 122 test bitstreams.

**1080p@60fps in the browser. 236KB WASM. No dependencies.**

## Features

- **Main & Main 10 profiles** (8-bit and 10-bit 4:2:0)
- **Pixel-perfect** — bit-exact match with ffmpeg on every frame
- **WebAssembly** — runs in any modern browser (Chrome, Firefox, Safari)
- **SIMD accelerated** — WASM SIMD 128-bit auto-vectorization
- **Tiny footprint** — 236KB .wasm, no runtime dependencies
- **C API** — easy integration into any project

## Performance

Measured on Apple Silicon (M-series), single-threaded:

| Resolution | Native | WASM (Chrome) |
|-----------|--------|---------------|
| 1080p | 76 fps | 61 fps |
| 4K | 28 fps | 21 fps |

## Quick Start

### Use in the browser

```bash
# Build WASM and launch the demo
./demo/serve.sh
# Open http://localhost:8080, drop a .265 file
```

### Use as a C/C++ library

```c
#include "wasm/hevc_api.h"

HEVCDecoder* dec = hevc_decoder_create();

// Decode a complete bitstream
hevc_decoder_decode(dec, data, size);

// Iterate over decoded frames (in display order)
int count = hevc_decoder_get_frame_count(dec);
for (int i = 0; i < count; i++) {
    HEVCFrame frame;
    hevc_decoder_get_frame(dec, i, &frame);
    // frame.y / frame.cb / frame.cr  — YUV plane pointers (uint16_t*)
    // frame.width / frame.height     — luma dimensions (cropped)
    // frame.stride_y / frame.stride_c — plane strides
    // frame.bit_depth                — 8 or 10
    // frame.poc                      — display order
}

hevc_decoder_destroy(dec);
```

### Use in JavaScript (Web Worker)

```js
const worker = new Worker('worker.js');
worker.postMessage({ type: 'init', wasmUrl: 'hevc-decode.js' });

// Decode a .265 file
const buffer = await file.arrayBuffer();
worker.postMessage({ type: 'decode', data: buffer }, [buffer]);

worker.onmessage = (e) => {
    if (e.data.type === 'frame') {
        const { y, cb, cr, width, height, bitDepth } = e.data.frame;
        // Render YUV frame (e.g. WebGL shader)
    }
};
```

## Build

### Native (debug + tests)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

### Native (release, optimized)

```bash
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel
```

### WebAssembly

Requires [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html).

```bash
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-wasm -DBUILD_WASM=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm
# Output: build-wasm/hevc-decode.js + hevc-decode.wasm
```

## C API Reference

```c
// Lifecycle
HEVCDecoder* hevc_decoder_create(void);
void          hevc_decoder_destroy(HEVCDecoder* dec);

// Decode a complete HEVC bitstream (Annex B format)
int hevc_decoder_decode(HEVCDecoder* dec, const uint8_t* data, size_t size);

// Access decoded frames
int hevc_decoder_get_frame_count(HEVCDecoder* dec);
int hevc_decoder_get_frame(HEVCDecoder* dec, int index, HEVCFrame* frame);

// Stream metadata (available after decode)
int hevc_decoder_get_info(HEVCDecoder* dec, HEVCStreamInfo* info);
```

### HEVCFrame

| Field | Type | Description |
|-------|------|-------------|
| `y`, `cb`, `cr` | `const uint16_t*` | Plane pointers (decoder-owned, valid until next decode) |
| `width`, `height` | `int` | Luma dimensions (conformance window applied) |
| `stride_y`, `stride_c` | `int` | Plane strides in samples |
| `chroma_width`, `chroma_height` | `int` | Chroma plane dimensions |
| `bit_depth` | `int` | 8 or 10 |
| `poc` | `int` | Picture Order Count (display order) |

### Return codes

| Code | Value | Meaning |
|------|-------|---------|
| `HEVC_OK` | 0 | Success |
| `HEVC_ERROR` | -1 | Decode error (partial results may still be available) |

## Architecture

```
src/
├── bitstream/      # Annex B parsing, NAL units, RBSP, Exp-Golomb
├── syntax/         # VPS, SPS, PPS, slice header parsing
├── decoding/       # CABAC, coding tree, intra/inter prediction, transform
├── filters/        # Deblocking filter, SAO
├── common/         # Types, Picture buffer, debug logging
└── wasm/           # C API, JS wrapper, Web Worker
demo/               # HTML demo with WebGL YUV renderer
tests/              # Unit tests + oracle tests (122 pixel-perfect)
```

## Spec Conformance

Implemented per ITU-T H.265 (v8, 08/2021). Validated against ffmpeg and HM reference decoder.

| Feature | Status |
|---------|--------|
| CABAC arithmetic decoding | Complete |
| 35 intra prediction modes | Complete |
| Inter prediction (merge, AMVP, TMVP) | Complete |
| 8-tap luma / 4-tap chroma interpolation | Complete |
| Weighted prediction (default + explicit) | Complete |
| Transform inverse (DCT 4-32, DST 4) | Complete |
| Scaling lists | Complete |
| Deblocking filter | Complete |
| SAO (edge + band offset) | Complete |
| 10-bit decoding | Complete |
| Tiles | Parsed + sequential decode |
| WPP | I-frames only |
| Multi-slice | Not supported |

## License

MIT
