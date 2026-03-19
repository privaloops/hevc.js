# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- `Picture::allocate()` and `Picture::write_yuv()` implementation (8-bit and 10-bit YUV output)
- `HEVC_LOG` debug logging infrastructure with 12 categories and runtime filtering via `HEVC_DEBUG_FILTER`
- 7 unit tests for Picture (allocate 420/422/444/mono, sample access, write YUV)
- 3 toy bitstreams (64x64, 1 CTU, I-only, QP 10/30/45) for step-by-step debugging
- CABAC reference data extraction script (`tools/extract_cabac_reference.py`)
- Toy bitstream generation script (`tools/gen_toy_bitstreams.sh`)
- Agent guide (`docs/agent-guide.md`) with phase-by-phase error catalog and debug workflow
- CTest oracle entries for toy bitstreams (label `toy`)
- RBSP reference data headers for `toy_qp30` and `i_64x64_qp22`
