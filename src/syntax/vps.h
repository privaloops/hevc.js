#pragma once

#include <cstdint>
#include <array>

namespace hevc {

class BitstreamReader;

// Video Parameter Set — spec §7.3.2.1, §7.4.3.1
struct VPS {
    uint8_t vps_video_parameter_set_id = 0;     // u(4)
    bool vps_base_layer_internal_flag = true;    // u(1)
    bool vps_base_layer_available_flag = true;   // u(1)
    uint8_t vps_max_layers_minus1 = 0;           // u(6)
    uint8_t vps_max_sub_layers_minus1 = 0;       // u(3)
    bool vps_temporal_id_nesting_flag = false;    // u(1)

    // profile_tier_level() — parsed separately
    // sub_layer_ordering_info
    struct SubLayerOrderingInfo {
        uint32_t max_dec_pic_buffering_minus1 = 0;
        uint32_t max_num_reorder_pics = 0;
        uint32_t max_latency_increase_plus1 = 0;
    };
    std::array<SubLayerOrderingInfo, 7> sub_layer_ordering;

    uint8_t vps_max_layer_id = 0;                // u(6)
    uint16_t vps_num_layer_sets_minus1 = 0;      // ue(v)

    bool vps_timing_info_present_flag = false;    // u(1)
    // timing info fields (optional, stubbed)

    // Parse from bitstream
    bool parse(BitstreamReader& bs);
};

} // namespace hevc
