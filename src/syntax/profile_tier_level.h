#pragma once

#include <cstdint>
#include <array>

namespace hevc {

class BitstreamReader;

// Profile/Tier/Level — spec §7.3.3, §7.4.4
struct ProfileTierLevel {
    // General profile
    uint8_t general_profile_space = 0;       // u(2)
    bool general_tier_flag = false;          // u(1)
    uint8_t general_profile_idc = 0;         // u(5)
    std::array<bool, 32> general_profile_compatibility_flag = {};
    bool general_progressive_source_flag = false;
    bool general_interlaced_source_flag = false;
    bool general_non_packed_constraint_flag = false;
    bool general_frame_only_constraint_flag = false;

    // Constraint flags (for profile_idc 4-11)
    bool general_max_12bit_constraint_flag = false;
    bool general_max_10bit_constraint_flag = false;
    bool general_max_8bit_constraint_flag = false;
    bool general_max_422chroma_constraint_flag = false;
    bool general_max_420chroma_constraint_flag = false;
    bool general_max_monochrome_constraint_flag = false;
    bool general_intra_constraint_flag = false;
    bool general_one_picture_only_constraint_flag = false;
    bool general_lower_bit_rate_constraint_flag = false;

    uint8_t general_level_idc = 0;           // u(8)

    // Sub-layer profile/level
    std::array<bool, 6> sub_layer_profile_present_flag = {};
    std::array<bool, 6> sub_layer_level_present_flag = {};
    std::array<uint8_t, 6> sub_layer_profile_idc = {};
    std::array<uint8_t, 6> sub_layer_level_idc = {};

    // Parse from bitstream
    // Spec §7.3.3
    bool parse(BitstreamReader& bs, bool profilePresentFlag, int maxNumSubLayersMinus1);
};

} // namespace hevc
