# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
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
