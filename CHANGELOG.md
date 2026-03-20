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
- **BitstreamReader**: replaced bit-by-bit loop with 64-bit cached read (O(1) per `read_bits()` call)
- **BitstreamReader**: `more_rbsp_data()` now O(1) — `find_last_one_bit` computed once at construction
- **Picture::write_yuv()**: 8-bit output writes per-line instead of per-byte

### Fixed
- **DECISIONS.md**: corrected WASM default stack size (64KB, not 1MB)
