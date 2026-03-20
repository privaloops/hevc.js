#pragma once

// CABAC syntax element decoding — Spec §9.3.2, §9.3.3
// Each function decodes one syntax element using the appropriate
// binarization and context model(s).

#include "decoding/cabac.h"
#include "common/types.h"

namespace hevc {

// Forward declarations
struct SPS;
struct PPS;
struct SliceHeader;

// ============================================================
// Syntax element decoders
// Each takes the CABAC engine + any context derivation parameters
// ============================================================

// §9.3.3.4 — end_of_slice_segment_flag (decoded via terminate)
inline int decode_end_of_slice_segment_flag(CabacEngine& cabac) {
    return cabac.decode_terminate();
}

// §7.3.8.5 — sao_merge_left_flag, sao_merge_up_flag
inline int decode_sao_merge_flag(CabacEngine& cabac) {
    return cabac.decode_decision(CTX_SAO_MERGE_FLAG);
}

// §7.3.8.5 — sao_type_idx_luma, sao_type_idx_chroma
int decode_sao_type_idx(CabacEngine& cabac);

// §7.3.8.6 — split_cu_flag
// ctxInc depends on depth of left and above neighbours
int decode_split_cu_flag(CabacEngine& cabac, int ctxInc);

// §7.3.8.6 — cu_transquant_bypass_flag
inline int decode_cu_transquant_bypass_flag(CabacEngine& cabac) {
    return cabac.decode_decision(CTX_CU_TRANSQUANT_BYPASS);
}

// §7.3.8.6 — cu_skip_flag
int decode_cu_skip_flag(CabacEngine& cabac, int ctxInc);

// §7.3.8.7 — pred_mode_flag
inline int decode_pred_mode_flag(CabacEngine& cabac) {
    return cabac.decode_decision(CTX_PRED_MODE_FLAG);
}

// §7.3.8.7 — part_mode (depends on pred_mode, log2CbSize, amp_enabled)
int decode_part_mode(CabacEngine& cabac, PredMode pred_mode,
                     int log2CbSize, int log2MinCbSize, bool amp_enabled);

// §7.3.8.8 — prev_intra_luma_pred_flag
inline int decode_prev_intra_luma_pred_flag(CabacEngine& cabac) {
    return cabac.decode_decision(CTX_PREV_INTRA_LUMA_PRED);
}

// §7.3.8.8 — mpm_idx (FL, 2 bins bypass)
inline int decode_mpm_idx(CabacEngine& cabac) {
    // TR binarization cMax=2: bin0 bypass, if 1 then bin1 bypass
    int bin0 = cabac.decode_bypass();
    if (bin0 == 0) return 0;
    int bin1 = cabac.decode_bypass();
    return bin1 ? 2 : 1;
}

// §7.3.8.8 — rem_intra_luma_pred_mode (FL, 5 bins bypass)
inline int decode_rem_intra_luma_pred_mode(CabacEngine& cabac) {
    return cabac.decode_bypass_bins(5);
}

// §7.3.8.8 — intra_chroma_pred_mode
int decode_intra_chroma_pred_mode(CabacEngine& cabac);

// §7.3.8.9 — merge_flag
inline int decode_merge_flag(CabacEngine& cabac) {
    return cabac.decode_decision(CTX_MERGE_FLAG);
}

// §7.3.8.9 — merge_idx (TU, cMax = MaxNumMergeCand - 1)
int decode_merge_idx(CabacEngine& cabac, int maxNumMergeCand);

// §7.3.8.11 — split_transform_flag
int decode_split_transform_flag(CabacEngine& cabac, int log2TrafoSize);

// §7.3.8.11 — cbf_luma
int decode_cbf_luma(CabacEngine& cabac, int trafoDepth);

// §7.3.8.11 — cbf_cb, cbf_cr
int decode_cbf_chroma(CabacEngine& cabac, int trafoDepth);

// §7.3.8.11 — cu_qp_delta
int decode_cu_qp_delta(CabacEngine& cabac);

// §7.3.8.11 — transform_skip_flag
int decode_transform_skip_flag(CabacEngine& cabac, int cIdx);

// §7.3.8.12 — last_sig_coeff_x_prefix, last_sig_coeff_y_prefix
int decode_last_sig_coeff_prefix(CabacEngine& cabac, int ctxOffset, int cIdx,
                                  int log2TrafoSize);

// §7.3.8.12 — last_sig_coeff_x_suffix, last_sig_coeff_y_suffix
int decode_last_sig_coeff_suffix(CabacEngine& cabac, int prefix);

// §7.3.8.12 — coded_sub_block_flag
int decode_coded_sub_block_flag(CabacEngine& cabac, int ctxInc);

// §7.3.8.12 — sig_coeff_flag
int decode_sig_coeff_flag(CabacEngine& cabac, int ctxInc);

// §7.3.8.12 — coeff_abs_level_greater1_flag
int decode_coeff_abs_level_greater1_flag(CabacEngine& cabac, int ctxSet, int greater1Ctx, int cIdx);

// §7.3.8.12 — coeff_abs_level_greater2_flag
int decode_coeff_abs_level_greater2_flag(CabacEngine& cabac, int ctxSet, int cIdx);

// §7.3.8.12 — coeff_abs_level_remaining (Golomb-Rice + EGk bypass)
int decode_coeff_abs_level_remaining(CabacEngine& cabac, int cRiceParam);

// §7.3.8.12 — coeff_sign_flag (bypass)
inline int decode_coeff_sign_flag(CabacEngine& cabac) {
    return cabac.decode_bypass();
}

} // namespace hevc
