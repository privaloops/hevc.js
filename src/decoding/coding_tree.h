#pragma once

// Coding Tree — slice_segment_data, coding_quadtree, coding_unit
// Spec §7.3.8 (slice data), §7.3.8.3 (coding_quadtree), §7.3.8.5 (coding_unit)

#include <cstdint>
#include <cstring>

#include "common/types.h"
#include "common/picture.h"
#include "syntax/sps.h"
#include "syntax/pps.h"
#include "syntax/slice_header.h"
#include "decoding/cabac.h"
#include "decoding/inter_prediction.h"

namespace hevc {

// Per-CU info stored in the picture grid (for neighbour access)
struct CUInfo {
    PredMode pred_mode = PredMode::MODE_INTRA;
    PartMode part_mode = PartMode::PART_2Nx2N;
    int log2CbSize = 0;
    int intra_mode_luma = 1; // DC default
    int qp_y = 26;
    bool is_pcm = false;
    bool cu_transquant_bypass = false;
    bool merge_flag = false;  // Phase 5: for rqt_root_cbf condition
};

class DPB;  // forward declaration

// Decoding context passed through the coding tree
struct DecodingContext {
    const SPS* sps = nullptr;
    const PPS* pps = nullptr;
    const SliceHeader* sh = nullptr;
    CabacEngine* cabac = nullptr;
    Picture* pic = nullptr;
    DPB* dpb = nullptr;  // Phase 5: access to ref pictures

    // CU info grid: indexed by min-CB position (x >> log2MinCbSize, y >> log2MinCbSize)
    CUInfo* cu_info = nullptr;    // flat array [PicWidthInMinCbs * PicHeightInMinCbs]
    int cu_info_stride = 0;       // = PicWidthInMinCbsY

    // QP tracking
    int QpY_prev = 0;            // QP of previous CU in coding order
    bool IsCuQpDeltaCoded = false;
    int CuQpDeltaVal = 0;

    // Intra mode storage per PU (for neighbour MPM derivation)
    // Indexed like cu_info but at PU granularity
    int* intra_pred_mode_y = nullptr;
    int* intra_pred_mode_c = nullptr;
    int intra_pred_mode_stride = 0;

    // Inter: per-PU motion info at min-PU (4x4) granularity
    PUMotionInfo* motion_info = nullptr;
    int motion_info_stride = 0;  // = pic_width / MinTbSizeY

    // Phase 6: deblocking data at min-TB (4x4) granularity
    uint8_t* cbf_luma_grid = nullptr;    // 1 if TU has nonzero luma coefficients
    uint8_t* log2_tu_size_grid = nullptr; // log2 of TU size covering this 4x4 block
    uint8_t* edge_flags_v = nullptr;     // 1 if there's a vertical edge at this 4x4 position
    uint8_t* edge_flags_h = nullptr;     // 1 if there's a horizontal edge at this 4x4 position
    int filter_grid_stride = 0;          // = pic_width / 4

    // Phase 6: SAO params per CTU
    struct SaoParams {
        int sao_type_idx[3] = {};     // 0=off, 1=band, 2=edge
        int sao_offset_val[3][5] = {}; // derived offset values (spec §7.4.9.3)
        int sao_band_position[3] = {};
        int sao_eo_class[3] = {};
    };
    SaoParams* sao_params = nullptr;   // array [PicWidthInCtbsY * PicHeightInCtbsY]
    int sao_params_stride = 0;         // = PicWidthInCtbsY

    // Helper: get CU info at luma sample position
    CUInfo& cu_at(int x, int y) {
        int minCbSize = sps->MinCbSizeY;
        return cu_info[(y / minCbSize) * cu_info_stride + (x / minCbSize)];
    }
    const CUInfo& cu_at(int x, int y) const {
        int minCbSize = sps->MinCbSizeY;
        return cu_info[(y / minCbSize) * cu_info_stride + (x / minCbSize)];
    }

    // Get intra mode at position (luma) — min-TB granularity for NxN support
    int intra_mode_at(int x, int y) const {
        int minTbSize = sps->MinTbSizeY;
        return intra_pred_mode_y[(y / minTbSize) * intra_pred_mode_stride + (x / minTbSize)];
    }
    // Get chroma intra mode at position
    int chroma_mode_at(int x, int y) const {
        int minTbSize = sps->MinTbSizeY;
        return intra_pred_mode_c[(y / minTbSize) * intra_pred_mode_stride + (x / minTbSize)];
    }
    void set_intra_mode(int x, int y, int size, int mode);
    void set_chroma_mode(int x, int y, int size, int mode);
};

// ============================================================
// Top-level decoding functions
// ============================================================

// Decode a complete slice segment (§7.3.8.1)
bool decode_slice_segment_data(DecodingContext& ctx, BitstreamReader& bs);

// Decode a coding tree unit (§7.3.8.2)
void decode_coding_tree_unit(DecodingContext& ctx, int xCtb, int yCtb);

// Decode a coding quadtree (§7.3.8.3)
void decode_coding_quadtree(DecodingContext& ctx, int x0, int y0,
                            int log2CbSize, int ctDepth);

// Decode a coding unit (§7.3.8.5)
void decode_coding_unit(DecodingContext& ctx, int x0, int y0, int log2CbSize);

// Decode a prediction unit (intra) (§7.3.8.8)
void decode_prediction_unit_intra(DecodingContext& ctx, int x0, int y0,
                                   int log2CbSize, PartMode part_mode);

// Decode a transform tree (§7.3.8.10)
void decode_transform_tree(DecodingContext& ctx, int x0, int y0,
                           int xBase, int yBase,
                           int log2TrafoSize, int trafoDepth,
                           int blkIdx,
                           bool cbf_cb_parent, bool cbf_cr_parent);

// Decode a transform unit (§7.3.8.11)
void decode_transform_unit(DecodingContext& ctx, int x0, int y0,
                           int xBase, int yBase,
                           int log2TrafoSize, int trafoDepth,
                           int blkIdx,
                           bool cbf_luma, bool cbf_cb, bool cbf_cr);

// PCM mode (§7.3.10.2)
void decode_pcm_samples(DecodingContext& ctx, int x0, int y0, int log2CbSize);

} // namespace hevc
