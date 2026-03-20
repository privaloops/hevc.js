#pragma once

#include <cstdint>
#include <vector>

#include "syntax/sps.h"

namespace hevc {

class BitstreamReader;

// Picture Parameter Set — spec §7.3.2.3, §7.4.3.3
struct PPS {
    uint32_t pps_pic_parameter_set_id = 0;          // ue(v)
    uint32_t pps_seq_parameter_set_id = 0;          // ue(v)
    bool dependent_slice_segments_enabled_flag = false;
    bool output_flag_present_flag = false;
    uint8_t num_extra_slice_header_bits = 0;         // u(3)
    bool sign_data_hiding_enabled_flag = false;
    bool cabac_init_present_flag = false;

    uint32_t num_ref_idx_l0_default_active_minus1 = 0;
    uint32_t num_ref_idx_l1_default_active_minus1 = 0;

    int32_t init_qp_minus26 = 0;                     // se(v)
    bool constrained_intra_pred_flag = false;
    bool transform_skip_enabled_flag = false;
    bool cu_qp_delta_enabled_flag = false;
    uint32_t diff_cu_qp_delta_depth = 0;

    int32_t pps_cb_qp_offset = 0;                    // se(v)
    int32_t pps_cr_qp_offset = 0;                    // se(v)
    bool pps_slice_chroma_qp_offsets_present_flag = false;

    bool weighted_pred_flag = false;
    bool weighted_bipred_flag = false;
    bool transquant_bypass_enabled_flag = false;

    bool tiles_enabled_flag = false;
    bool entropy_coding_sync_enabled_flag = false;

    // Tiles
    uint32_t num_tile_columns_minus1 = 0;
    uint32_t num_tile_rows_minus1 = 0;
    bool uniform_spacing_flag = true;
    std::vector<uint32_t> column_width_minus1;
    std::vector<uint32_t> row_height_minus1;
    bool loop_filter_across_tiles_enabled_flag = true;

    bool pps_loop_filter_across_slices_enabled_flag = false;

    bool deblocking_filter_control_present_flag = false;
    bool deblocking_filter_override_enabled_flag = false;
    bool pps_deblocking_filter_disabled_flag = false;
    int32_t pps_beta_offset_div2 = 0;
    int32_t pps_tc_offset_div2 = 0;

    bool pps_scaling_list_data_present_flag = false;
    ScalingListData scaling_list_data;

    bool lists_modification_present_flag = false;
    uint32_t log2_parallel_merge_level_minus2 = 0;
    bool slice_segment_header_extension_present_flag = false;

    // ---- Derived values ----
    int Log2MinCuQpDeltaSize = 0;

    // Tile scan conversion tables (derived from tile layout)
    std::vector<int> CtbAddrRsToTs;  // raster scan -> tile scan
    std::vector<int> CtbAddrTsToRs;  // tile scan -> raster scan
    std::vector<int> TileId;         // CTB address -> tile ID

    // Parse from bitstream
    bool parse(BitstreamReader& bs, const SPS& sps);

    // Compute tile scan tables
    void derive_tile_scan(const SPS& sps);
};

} // namespace hevc
