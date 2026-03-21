#pragma once

#include <cstdint>
#include <array>
#include <vector>

#include "common/types.h"
#include "syntax/profile_tier_level.h"

namespace hevc {

class BitstreamReader;

// Scaling List Data — spec §7.3.4, §7.4.5
struct ScalingListData {
    // ScalingList[sizeId][matrixId][coefIdx]
    // sizeId 0 (4x4):   6 matrices of 16 coefficients
    // sizeId 1 (8x8):   6 matrices of 64 coefficients
    // sizeId 2 (16x16): 6 matrices of 64 coefficients (upscaled from 8x8)
    // sizeId 3 (32x32): 6 matrices of 64 coefficients (only matrixId 0,3 used)
    std::array<std::array<std::array<uint8_t, 64>, 6>, 4> scaling_list = {};

    // DC coefficients for 16x16 and 32x32
    std::array<std::array<uint8_t, 6>, 2> scaling_list_dc = {};  // [sizeId-2][matrixId]

    // Parse from bitstream
    bool parse(BitstreamReader& bs);

    // Initialize with default values (spec Tables 7-3 to 7-5)
    void set_defaults();
};

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

    // Parse from bitstream
    // stRpsIdx: index of this set in the SPS array
    // num_short_term_ref_pic_sets: total count (for inter prediction)
    // rps_array: previously parsed sets (for inter prediction reference)
    bool parse(BitstreamReader& bs, int stRpsIdx, int num_short_term_ref_pic_sets,
               const std::vector<ShortTermRefPicSet>& rps_array);
};

// Sub-layer ordering info (shared between VPS and SPS)
struct SubLayerOrderingInfo {
    uint32_t max_dec_pic_buffering_minus1 = 0;
    uint32_t max_num_reorder_pics = 0;
    uint32_t max_latency_increase_plus1 = 0;
};

// Sequence Parameter Set — spec §7.3.2.2, §7.4.3.2
struct SPS {
    uint8_t sps_video_parameter_set_id = 0;       // u(4)
    uint8_t sps_max_sub_layers_minus1 = 0;         // u(3)
    bool sps_temporal_id_nesting_flag = false;      // u(1)

    ProfileTierLevel profile_tier_level;

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
    std::array<SubLayerOrderingInfo, 7> sub_layer_ordering;

    uint32_t log2_min_luma_coding_block_size_minus3 = 0;
    uint32_t log2_diff_max_min_luma_coding_block_size = 0;
    uint32_t log2_min_luma_transform_block_size_minus2 = 0;
    uint32_t log2_diff_max_min_luma_transform_block_size = 0;
    uint32_t max_transform_hierarchy_depth_inter = 0;
    uint32_t max_transform_hierarchy_depth_intra = 0;

    bool scaling_list_enabled_flag = false;
    bool sps_scaling_list_data_present_flag = false;
    ScalingListData scaling_list_data;

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
    bool intra_smoothing_disabled_flag = false;

    bool vui_parameters_present_flag = false;

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

    int SubWidthC = 0;
    int SubHeightC = 0;

    // PCM derived (§7.4.3.2.1)
    int Log2MinIpcmCbSizeY = 0;
    int Log2MaxIpcmCbSizeY = 0;

    // Parse from bitstream
    bool parse(BitstreamReader& bs);

    // Compute all derived values after parsing
    void derive();
};

} // namespace hevc
