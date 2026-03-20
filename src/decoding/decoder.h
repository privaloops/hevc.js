#pragma once

// Top-level HEVC decoder
// Orchestrates NAL parsing, parameter set management, and frame decoding

#include <cstdint>
#include <cstddef>
#include <vector>

#include "common/picture.h"
#include "syntax/parameter_sets.h"
#include "decoding/coding_tree.h"

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
    const std::vector<Picture>& pictures() const { return pictures_; }
    std::vector<Picture>& pictures() { return pictures_; }

private:
    DecodeStatus decode_picture(const std::vector<NalUnit>& nals,
                                 size_t first_vcl_idx);

    ParameterSetManager ps_mgr_;
    std::vector<Picture> pictures_;

    // Decoding context allocations (per-picture)
    std::vector<CUInfo> cu_info_buf_;
    std::vector<int> intra_mode_buf_;
};

} // namespace hevc
