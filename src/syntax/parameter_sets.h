#pragma once

#include <array>
#include <optional>
#include <cstdio>

#include "syntax/vps.h"
#include "syntax/sps.h"
#include "syntax/pps.h"
#include "syntax/slice_header.h"
#include "bitstream/nal_unit.h"
#include "bitstream/bitstream_reader.h"

namespace hevc {

// Parameter Set Manager — stores VPS/SPS/PPS by ID (AD-003)
// Spec: VPS 0-15, SPS 0-15, PPS 0-63
class ParameterSetManager {
public:
    // Parse and store a parameter set NAL unit
    // Returns true if parsing succeeded
    bool process_nal(const NalUnit& nal);

    // Parse a slice header using active parameter sets
    // Returns true if parsing succeeded (PPS/SPS must be available)
    bool parse_slice_header(SliceHeader& sh, const NalUnit& nal);

    // Accessors
    const VPS* get_vps(int id) const {
        if (id < 0 || id > 15 || !vps_[id]) return nullptr;
        return &vps_[id].value();
    }
    const SPS* get_sps(int id) const {
        if (id < 0 || id > 15 || !sps_[id]) return nullptr;
        return &sps_[id].value();
    }
    const PPS* get_pps(int id) const {
        if (id < 0 || id > 63 || !pps_[id]) return nullptr;
        return &pps_[id].value();
    }

    // Active parameter sets (set by last parsed slice header)
    const SPS* active_sps() const { return active_sps_; }
    const PPS* active_pps() const { return active_pps_; }

private:
    std::array<std::optional<VPS>, 16> vps_;
    std::array<std::optional<SPS>, 16> sps_;
    std::array<std::optional<PPS>, 64> pps_;

    const SPS* active_sps_ = nullptr;
    const PPS* active_pps_ = nullptr;
};

} // namespace hevc
