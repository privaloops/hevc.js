# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Fixed
- **Merge candidate availability §6.4.2 + early part_mode**: `is_pu_available` applied z-scan (§6.4.1) before sameCb, violating §6.4.2. Additionally, `part_mode` was only written to CU grid after all PUs, so partition-specific merge exclusions (lines 279-288) used stale values. These correlated bugs masked each other — the z-scan rejection hid the stale `part_mode`. Fix: early `part_mode` write + sameCb-first check in `is_pu_available`. Fixes BBB 1080p frames 18+ (4677→0 diffs). **128/128 tests pixel-perfect.**
- **Scaling list pred_mode lookup (§8.6.3)**: `perform_dequant` used `cu_at(0,0)` instead of `cu_at(x0,y0)` to determine pred_mode for scaling list matrix selection. For intra CUs in inter frames, the inter matrix was used instead of intra, producing wrong dequant values (±1 residual error per affected pixel). Default 8x8 matrices differ significantly (intra[7][7]=115 vs inter[7][7]=91). Fixes BBB 1080p frames 2-17 (29→0 diffs).
- **AMVP prediction block availability (§6.4.2)**: `is_amvp_nb_available` used z-scan order (§6.4.1) for ALL neighbors, including those within the same coding block. Per §6.4.2, intra-CU neighbors are always available (no z-scan needed, except NxN partition exclusion). Additionally, `pred_mode` was only stored in the CU grid AFTER all PUs were processed, so the 2nd PU of a multi-PU CU saw the previous frame's `pred_mode` (INTRA) for same-CU neighbors, causing AMVP to fall back to zero-MV padding. Fixes BBB 1080p P/B frames (4517→0 Y diffs at frame 1).
- **WPP substream seek (§7.3.8.1)**: BitstreamReader was never repositioned at WPP row boundaries, causing crash (`read past end`) at CTU 899 on BBB 1080p. Now computes absolute RBSP positions from `entry_point_offset_minus1` and seeks to correct substream start.
- **EP byte accounting in entry_point_offsets (§7.4.7.1)**: entry_point_offsets count emulation prevention bytes, but substream positions were computed in RBSP space (EP bytes removed). Added `coded_to_rbsp_offset` conversion using tracked EP byte positions from `extract_rbsp`.
- **QP derivation uses QG coordinates (§8.6.1)**: `derive_qp_y` was using CU coordinates `(xCb, yCb)` for neighbor QP prediction instead of quantization group coordinates `(xQg, yQg)`. Caused systematic QP errors (+4 to -4) on streams with `cu_qp_delta_enabled_flag`.
- **WPP QpY_prev reset (§8.6.1)**: `qPY_PREV` was not reset to `SliceQpY` at the first quantization group of each CTB row when `entropy_coding_sync_enabled_flag` is set. Same reset added for tile boundaries.
- **QP derivation shortcut removed (§8.6.1)**: `derive_qp_y` incorrectly returned `QpY_prev` when `IsCuQpDeltaCoded` was false. The spec requires full neighbor-based `qPY_PRED` computation even with `CuQpDeltaVal=0`.
- **QpY_prev_qg tracking (§8.6.1)**: `qPY_PREV` was updated after every CU, but the spec defines it as the QP of the last CU in the *previous* QG. Introduced `QpY_prev_qg` saved at QG boundary start. Fixes catastrophic decode errors on `cu_qp_delta_enabled` streams (BBB 4K: 12M→<2K diffs/frame, all I-frames now pixel-perfect).
- **SAO cross-slice boundary (§8.7.3.2)**: SAO edge offset did not check `slice_loop_filter_across_slices_enabled_flag` before accessing neighbor samples in adjacent slices. Also added cross-tile boundary check. Fixes `conf_b_xslice_256` (pixel-perfect).

### Added
- **hevc.js monorepo restructure**:
  - pnpm workspace with `packages/core/` (TypeScript WASM wrapper) and `packages/videojs/` (Video.js Tech + fMP4 demuxer + renderer)
  - `@hevcjs/core`: typed `HEVCDecoder` class with `.create()` / `.decode()` / `.destroy()` API
  - `@hevcjs/videojs`: `HevcWasmTech` Video.js Tech, `FMP4Demuxer` (moof/mdat → NAL units), `FrameRenderer` (WebGL + VideoFrame/MediaStreamTrackGenerator)
  - npm subpath exports: `import { HEVCDecoder } from 'hevc.js'` and `import 'hevc.js/videojs'`
  - tsup build (ESM + .d.ts), TypeScript strict mode
  - C++ source and tests unchanged (122/126 tests pass)

- **Phase 7 — Main 10 Profile (10-bit 4:2:0)**:
  - 10-bit decoding pixel-perfect (I-frame + full pipeline I+P+B with deblock+SAO)
  - 2 new oracle tests: `oracle_i_64x64_10bit`, `oracle_full_qcif_10f_10bit`
  - `oracle_test.sh` now supports 10-bit output format via optional `PIX_FMT` parameter

- **Phase 9 — Performance optimizations**:
  - Interior PU interpolation: skip bounds checking for PUs not at picture edges (+32% native, 56→75 fps 1080p)
  - Stack-allocated interpolation buffers: eliminate heap allocations per PU
  - SAO early exit: skip full-picture copy when no CTU has SAO enabled
  - Fixed stray `fprintf` in `dpb.cpp` (debug output in release builds)

- **Phase 8 — WASM Integration**:
  - C API (`src/wasm/hevc_api.h/cpp`): `hevc_decoder_create/destroy/decode/get_frame/get_info`
  - Emscripten build: MODULARIZE, ALLOW_MEMORY_GROWTH, STACK_SIZE=1MB, EXPORTED_FUNCTIONS
  - JS wrapper (`src/wasm/hevc_decoder.js`): promise-based API with typed array frame extraction
  - Web Worker (`src/wasm/worker.js`): decode in background thread, transferable frame buffers
  - Demo HTML (`demo/index.html`): WebGL YUV→RGB renderer (BT.709), file input, play/pause/step, keyboard shortcuts
  - WASM pixel-perfect verified against native build (MD5 match on `i_64x64_qp22.265`)
  - .wasm size: 123KB

### Fixed
- **cu_skip_flag pred_mode not stored before decode_prediction_unit_inter** — skip CUs entered AMVP path (reading extra CABAC bin) instead of merge path because `pred_mode` was only stored in the CU grid AFTER the prediction unit decode. Latent bug masked in 8-bit tests by coincidental CABAC alignment; exposed by 10-bit bitstreams with different CABAC state.
- **Multi-frame YUV output ignored bit depth** — the multi-frame output path in `main.cpp` cast all samples to `uint8_t`, producing 8-bit files for 10-bit content. Single-frame path (`write_yuv`) was correct. Also fixed chroma crop assuming 4:2:0 (`/2` hardcoded instead of using `SubWidthC`/`SubHeightC`).

### Previously added
- **Phase 6 — Loop Filters** (11/14 tests pass, 3 failures are multi-slice limitation):
  - Deblocking filter (§8.7.2) — boundary strength derivation, strong/weak luma filter, chroma filter (Bs==2)
  - SAO filter (§8.7.3) — edge offset (4 EO classes), band offset (32 bands), CTU merge (left/up)
  - Per-TU cbf and edge flag storage for deblocking boundary detection
  - SAO parameter storage with derived SaoOffsetVal (§7.4.9.3)
  - **`oracle_full_qcif_10f` pixel-perfect — Main profile complet**

### Fixed
- Chroma deblocking skipped when luma decision dE==0 (luma `continue` also skipped chroma filtering)

### Previously added (Phase 5)
- **Phase 5 — Inter Prediction** (10/10 tests pass):
  - Explicit weighted sample prediction (§8.5.3.3.4.3) — `weighted_pred_flag` P-slices with per-ref luma/chroma weights and offsets
  - CVS-aware output frame ordering — `cvs_id` counter for multi-GOP bitstreams with POC wrap at IDR boundaries
  - `interSplitFlag` (§7.4.9.4) — forces transform tree split for non-2Nx2N inter CUs when `max_transform_hierarchy_depth_inter == 0`

### Fixed
- P-slices with `weighted_pred_flag=1` used default WP instead of explicit (caused luma mismatch cascading to B-frames)
- Multi-IDR bitstreams output frames interleaved across GOPs (POC-only sort mixed frames from different coded video sequences)
- Non-2Nx2N inter CUs (PART_2NxN, PART_Nx2N, AMP) read `split_transform_flag` from bitstream when it should be inferred as 1, corrupting subsequent CABAC state

### Previously added
- **Phase 3 — Parameter Sets & Slice Header parsing**:
  - `ProfileTierLevel` parsing (§7.3.3) — general + sub-layer profiles, constraint flags for all profile_idc branches
  - `VPS::parse()` (§7.3.2.1) — timing info, layer sets, sub-layer ordering
  - `SPS::parse()` + `SPS::derive()` (§7.3.2.2, §7.4.3.2.1) — chroma format, dimensions, conformance window, bit depth, quad-tree config, scaling list data with full fallback, short-term reference picture sets with inter-prediction, long-term ref pics, VUI skip
  - `PPS::parse()` + `PPS::derive_tile_scan()` (§7.3.2.3, §6.5.1) — tiles layout, CtbAddrRsToTs/TsToRs/TileId derivation, deblocking filter control
  - `SliceHeader::parse()` (§7.3.6) — POC, short-term/long-term RPS, ref pic list modification, pred_weight_table, SAO flags, deblocking overrides, entry point offsets, dependent slices
  - `ScalingListData` with default matrices (Tables 7-3 to 7-5) and copy/prediction mechanism
  - `ParameterSetManager` — VPS/SPS/PPS storage by ID (AD-003), activation via slice header
  - CLI `--dump-headers` — full parameter set and slice header inspection
  - 17 new unit tests covering parsers across toy (64x64), QCIF (176x144), 1080p, 4K, and conformance edge-case bitstreams

- **NalParser** (`src/bitstream/nal_parser.cpp`) — Annex B byte stream parsing:
  - Start code detection (3-byte `0x000001` and 4-byte `0x00000001`)
  - NAL unit header parsing (nal_unit_type, nuh_layer_id, nuh_temporal_id_plus1) with `forbidden_zero_bit` validation
  - RBSP extraction integrated into parsing pipeline
  - Access Unit boundary detection (§7.4.2.4.4) — groups NAL units into frames
  - `nal_type_name()` helper for human-readable NAL type names
- **CLI `--dump-nals`** — Lists all NAL units with offset, type, size, TemporalId + Access Unit grouping summary
- 22 new unit tests for NalParser (start codes, header parsing, EP removal, AU boundaries, Exp-Golomb edge cases)
- Integration test on real bitstream (`toy_qp30.265`)
- Project infrastructure: CMake build system, Google Test, CTest oracle tests
- BitstreamReader with bit-level reading, Exp-Golomb (ue/se), RBSP extraction
- `Picture::allocate()` and `Picture::write_yuv()` implementation (8-bit and 10-bit YUV output)
- `HEVC_LOG` debug logging infrastructure with 12 categories and runtime filtering via `HEVC_DEBUG_FILTER`
- Header interfaces for all phases: NalUnit, VPS, SPS, PPS, SliceHeader
- 10 oracle test fixtures (toy, conformance, real-world)
- Real-world test bitstreams: Big Buck Bunny 1080p (50 frames), 4K (25 frames)
- Oracle test scripts (oracle_test.sh, oracle_compare.py)
- CABAC reference data extraction script (`tools/extract_cabac_reference.py`)
- Toy bitstream generation script (`tools/gen_toy_bitstreams.sh`)
- Agent guide (`docs/agent-guide.md`) with phase-by-phase error catalog and debug workflow
- GitHub Actions CI (build native/WASM + unit tests + oracle tests)
- CLI with `-o` output flag (stub, exit code 2 = SKIP until decode pipeline implemented)

### Changed
- Renamed project from hevc-torture to hevc-decode
- **BitstreamReader**: replaced bit-by-bit loop with 64-bit cached read (O(1) per `read_bits()` call)
- **BitstreamReader**: `more_rbsp_data()` now O(1) — `find_last_one_bit` computed once at construction
- **Picture::write_yuv()**: 8-bit output writes per-line instead of per-byte

### Fixed
- **debug.h**: replaced GNU extension `##__VA_ARGS__` with standard `__VA_OPT__(,)` to fix `-Wpedantic` build error
- **DECISIONS.md**: corrected WASM default stack size (64KB, not 1MB)
