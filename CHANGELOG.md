# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
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
- BitstreamReader: replaced bit-per-bit loop with 64-bit sliding window buffer (O(1) per read_bits call)
- BitstreamReader: replaced exceptions with internal error state (`has_error()`) for WASM safety (AD-004)
- CMake: added `-fno-exceptions` globally, consistent with AD-004
- CMake WASM flags: added EXPORTED_FUNCTIONS, EXPORTED_RUNTIME_METHODS, INITIAL_MEMORY (128MB), STACK_SIZE (256KB)
- main.cpp: uses Decoder class instead of direct BitstreamReader access

### Added (WASM optimization)
- Decoder class (`src/decoder/`) with `feed()` / `get_frame()` / `flush()` API, ready for streaming WASM integration (Phase 8)
- `extract_rbsp_to()`: reusable buffer variant to avoid per-NAL allocation
- Unit tests: ErrorOnReadPastEnd, ErrorOnExpGolombOverflow, BitsRead, ReusableBuffer
