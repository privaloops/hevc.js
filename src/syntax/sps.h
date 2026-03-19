#pragma once

#include <cstdint>
#include <array>
#include <vector>

#include "common/types.h"

namespace hevc {

class BitstreamReader;

// Short-term Reference Picture Set — spec §7.3.7
struct ShortTermRefPicSet {
    bool inter_ref_pic_set_prediction_flag = false;

    uint32_t num_negative_pics = 0;
    uint32_t num_positive_pics = 0;
    std::array<int32_t, 16> delta_poc_s0 = {};
    std::array<bool, 16> used_by_curr_pic_s0 = {};
    std::array<int32_t, 16> delta_poc_s1 = {};
    std::array<bool, 16> used_by_curr_pic_s1 = {};

    // Derived
    uint32_t NumNegativePics = 0;
    uint32_t NumPositivePics = 0;
    uint32_t NumDeltaPocs = 0;
    std::array<int32_t, 16> DeltaPocS0 = {};
    std::array<bool, 16> UsedByCurrPicS0 = {};
    std::array<int32_t, 16> DeltaPocS1 = {};
    std::array<bool, 16> UsedByCurrPicS1 = {};
};

// Sequence Parameter Set — spec §7.3.2.2, §7.4.3.2
struct SPS {
    uint8_t sps_video_parameter_set_id = 0;       // u(4)
    uint8_t sps_max_sub_layers_minus1 = 0;         // u(3)
    bool sps_temporal_id_nesting_flag = false;      // u(1)

    // profile_tier_level() — parsed separately

    uint32_t sps_seq_parameter_set_id = 0;          // ue(v)
    uint32_t chroma_format_idc = 1;                  // ue(v)
    bool separate_colour_plane_flag = false;          // u(1)
    uint32_t pic_width_in_luma_samples = 0;           // ue(v)
    uint32_t pic_height_in_luma_samples = 0;          // ue(v)

    bool conformance_window_flag = false;              // u(1)
    uint32_t conf_win_left_offset = 0;
    uint32_t conf_win_right_offset = 0;
    uint32_t conf_win_top_offset = 0;
    uint32_t conf_win_bottom_offset = 0;

    uint32_t bit_depth_luma_minus8 = 0;               // ue(v)
    uint32_t bit_depth_chroma_minus8 = 0;              // ue(v)
    uint32_t log2_max_pic_order_cnt_lsb_minus4 = 0;   // ue(v)

    bool sps_sub_layer_ordering_info_present_flag = false;

    uint32_t log2_min_luma_coding_block_size_minus3 = 0;
    uint32_t log2_diff_max_min_luma_coding_block_size = 0;
    uint32_t log2_min_luma_transform_block_size_minus2 = 0;
    uint32_t log2_diff_max_min_luma_transform_block_size = 0;
    uint32_t max_transform_hierarchy_depth_inter = 0;
    uint32_t max_transform_hierarchy_depth_intra = 0;

    bool scaling_list_enabled_flag = false;
    bool sps_scaling_list_data_present_flag = false;
    // ScalingList data — parsed separately

    bool amp_enabled_flag = false;
    bool sample_adaptive_offset_enabled_flag = false;

    bool pcm_enabled_flag = false;
    uint8_t pcm_sample_bit_depth_luma_minus1 = 0;
    uint8_t pcm_sample_bit_depth_chroma_minus1 = 0;
    uint32_t log2_min_pcm_luma_coding_block_size_minus3 = 0;
    uint32_t log2_diff_max_min_pcm_luma_coding_block_size = 0;
    bool pcm_loop_filter_disabled_flag = false;

    uint32_t num_short_term_ref_pic_sets = 0;
    std::vector<ShortTermRefPicSet> st_ref_pic_sets;

    bool long_term_ref_pics_present_flag = false;
    uint32_t num_long_term_ref_pics_sps = 0;
    std::array<uint32_t, 32> lt_ref_pic_poc_lsb_sps = {};
    std::array<bool, 32> used_by_curr_pic_lt_sps_flag = {};

    bool sps_temporal_mvp_enabled_flag = false;
    bool strong_intra_smoothing_enabled_flag = false;

    bool vui_parameters_present_flag = false;
    // VUI — parsed separately if needed

    // ---- Derived values (§7.4.3.2.1) ----
    int ChromaArrayType = 0;
    int BitDepthY = 8;
    int BitDepthC = 8;
    int QpBdOffsetY = 0;
    int QpBdOffsetC = 0;
    int MaxPicOrderCntLsb = 0;

    int MinCbLog2SizeY = 0;
    int CtbLog2SizeY = 0;
    int MinCbSizeY = 0;
    int CtbSizeY = 0;
    int PicWidthInMinCbsY = 0;
    int PicHeightInMinCbsY = 0;
    int PicWidthInCtbsY = 0;
    int PicHeightInCtbsY = 0;
    int PicSizeInCtbsY = 0;

    int MinTbLog2SizeY = 0;
    int MaxTbLog2SizeY = 0;
    int MinTbSizeY = 0;
    int MaxTbSizeY = 0;

    // Parse from bitstream
    bool parse(BitstreamReader& bs);

    // Compute all derived values after parsing
    void derive();
};

} // namespace hevc
