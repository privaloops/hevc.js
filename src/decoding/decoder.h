#pragma once

// Top-level HEVC decoder
// Orchestrates NAL parsing, parameter set management, and frame decoding

#include <cstdint>
#include <cstddef>
#include <vector>

#include "common/picture.h"
#include "syntax/parameter_sets.h"
#include "decoding/coding_tree.h"
#include "decoding/dpb.h"

namespace hevc {

enum class DecodeStatus {
    OK,
    NEED_MORE_DATA,
    ERROR,
};

class Decoder {
public:
    Decoder() = default;

    // Decode a complete bitstream
    DecodeStatus decode(const uint8_t* data, size_t size);

    // Get decoded pictures (in output order)
    std::vector<Picture*> output_pictures();

    // Get DPB (for testing)
    const DPB& dpb() const { return dpb_; }

private:
    DecodeStatus decode_picture(const std::vector<NalUnit>& nals,
                                 size_t first_vcl_idx);

    ParameterSetManager ps_mgr_;
    DPB dpb_;

    // Output pictures (accumulated across all decoded pictures)
    std::vector<Picture*> output_pics_;

    // Decoding context allocations (per-picture)
    std::vector<CUInfo> cu_info_buf_;
    std::vector<int> intra_mode_buf_;
    std::vector<int> chroma_mode_buf_;
    std::vector<PUMotionInfo> motion_info_buf_;
};

} // namespace hevc
