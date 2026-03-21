#include "decoding/coding_tree.h"
#include "decoding/syntax_elements.h"
#include "decoding/cabac_tables.h"
#include "common/debug.h"

#include <cstring>
#include <algorithm>

namespace hevc {

// Forward declarations for residual/transform/intra (implemented in separate files)
void decode_residual_coding(DecodingContext& ctx, int x0, int y0,
                            int log2TrafoSize, int cIdx,
                            int16_t* coefficients);
void perform_dequant(DecodingContext& ctx, int x0, int y0,
                     int log2TrafoSize, int cIdx, int qp,
                     const int16_t* coefficients, int16_t* scaled);
void perform_transform_inverse(int log2TrafoSize, int cIdx,
                                bool is_intra, bool transform_skip,
                                int bit_depth,
                                const int16_t* scaled, int16_t* residual);
void perform_intra_prediction(DecodingContext& ctx, int x0, int y0,
                              int log2PredSize, int cIdx, int intra_mode,
                              int16_t* pred_samples);

// ============================================================
// DecodingContext helpers
// ============================================================

void DecodingContext::set_intra_mode(int x, int y, int size, int mode) {
    int minTbSize = sps->MinTbSizeY;
    int x0 = x / minTbSize;
    int y0 = y / minTbSize;
    int n = std::max(1, size / minTbSize);
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
            intra_pred_mode_y[(y0 + j) * intra_pred_mode_stride + (x0 + i)] = mode;
}

void DecodingContext::set_chroma_mode(int x, int y, int size, int mode) {
    int minTbSize = sps->MinTbSizeY;
    int x0 = x / minTbSize;
    int y0 = y / minTbSize;
    int n = std::max(1, size / minTbSize);
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
            intra_pred_mode_c[(y0 + j) * intra_pred_mode_stride + (x0 + i)] = mode;
}

// ============================================================
// SAO parsing stub (§7.3.8.3)
// ============================================================

struct SaoParams {
    int sao_type_idx[3] = {};  // per component
    int sao_offset_abs[3][4] = {};
    int sao_offset_sign[3][4] = {};
    int sao_band_position[3] = {};
    int sao_eo_class[3] = {};
    bool merge_left = false;
    bool merge_up = false;
};

static void decode_sao(DecodingContext& ctx, int rx, int ry,
                       SaoParams* /*sao_store*/) {
    // §7.3.8.3 — Parse SAO parameters, store but don't apply (Phase 6)
    auto& cabac = *ctx.cabac;
    auto& sh = *ctx.sh;
    int CtbAddrInRs = ry * ctx.sps->PicWidthInCtbsY + rx;

    bool sao_merge_left_flag = false;
    bool sao_merge_up_flag = false;

    if (rx > 0) {
        bool leftInSlice = (CtbAddrInRs > static_cast<int>(sh.slice_segment_address));
        bool leftInTile = (ctx.pps->TileId.empty() ||
            ctx.pps->TileId[ctx.pps->CtbAddrRsToTs[CtbAddrInRs]] ==
            ctx.pps->TileId[ctx.pps->CtbAddrRsToTs[CtbAddrInRs - 1]]);
        if (leftInSlice && leftInTile)
            sao_merge_left_flag = decode_sao_merge_flag(cabac);
    }

    if (ry > 0 && !sao_merge_left_flag) {
        int upAddr = CtbAddrInRs - ctx.sps->PicWidthInCtbsY;
        bool upInSlice = (upAddr >= static_cast<int>(sh.slice_segment_address));
        bool upInTile = (ctx.pps->TileId.empty() ||
            ctx.pps->TileId[ctx.pps->CtbAddrRsToTs[CtbAddrInRs]] ==
            ctx.pps->TileId[ctx.pps->CtbAddrRsToTs[upAddr]]);
        if (upInSlice && upInTile)
            sao_merge_up_flag = decode_sao_merge_flag(cabac);
    }

    if (!sao_merge_left_flag && !sao_merge_up_flag) {
        int numComp = (ctx.sps->ChromaArrayType != 0) ? 3 : 1;
        for (int cIdx = 0; cIdx < numComp; cIdx++) {
            if ((sh.slice_sao_luma_flag && cIdx == 0) ||
                (sh.slice_sao_chroma_flag && cIdx > 0)) {
                int sao_type_idx = 0;
                if (cIdx == 0 || cIdx == 1) {
                    sao_type_idx = decode_sao_type_idx(cabac);
                }
                if (cIdx == 2) {
                    // chroma shares type with Cb
                }
                if (sao_type_idx != 0) {
                    int bitDepth = (cIdx == 0) ? ctx.sps->BitDepthY : ctx.sps->BitDepthC;
                    int maxOff = (1 << (std::min(bitDepth, 10) - 5)) - 1;
                    for (int i = 0; i < 4; i++) {
                        // sao_offset_abs: TR cMax=maxOff, bypass
                        int val = 0;
                        for (int k = 0; k < maxOff; k++) {
                            if (cabac.decode_bypass() == 0) break;
                            val++;
                        }
                        (void)val;
                    }
                    if (sao_type_idx == 1) {
                        // Band offset
                        for (int i = 0; i < 4; i++) {
                            cabac.decode_bypass(); // sao_offset_sign
                        }
                        cabac.decode_bypass_bins(5); // sao_band_position
                    } else {
                        // Edge offset
                        if (cIdx == 0)
                            cabac.decode_bypass_bins(2); // sao_eo_class_luma
                        if (cIdx == 1)
                            cabac.decode_bypass_bins(2); // sao_eo_class_chroma
                    }
                }
            }
        }
    }
}

// ============================================================
// split_cu_flag context derivation (§9.3.4.2.2)
// ============================================================

static int derive_split_cu_flag_ctx(const DecodingContext& ctx, int x0, int y0,
                                     int log2CbSize) {
    int ctxInc = 0;

    // Left neighbour
    int xL = x0 - 1;
    if (xL >= 0 && xL < static_cast<int>(ctx.sps->pic_width_in_luma_samples)) {
        const auto& cuL = ctx.cu_at(xL, y0);
        if (cuL.log2CbSize > 0 && cuL.log2CbSize < log2CbSize)
            ctxInc++;
    }

    // Above neighbour
    int yA = y0 - 1;
    if (yA >= 0 && yA < static_cast<int>(ctx.sps->pic_height_in_luma_samples)) {
        const auto& cuA = ctx.cu_at(x0, yA);
        if (cuA.log2CbSize > 0 && cuA.log2CbSize < log2CbSize)
            ctxInc++;
    }

    return ctxInc;
}

// ============================================================
// cu_skip_flag context derivation
// ============================================================

static int derive_cu_skip_flag_ctx(const DecodingContext& ctx, int x0, int y0) {
    int ctxInc = 0;

    int xL = x0 - 1;
    if (xL >= 0) {
        const auto& cuL = ctx.cu_at(xL, y0);
        if (cuL.pred_mode == PredMode::MODE_SKIP) ctxInc++;
    }

    int yA = y0 - 1;
    if (yA >= 0) {
        const auto& cuA = ctx.cu_at(x0, yA);
        if (cuA.pred_mode == PredMode::MODE_SKIP) ctxInc++;
    }

    return ctxInc;
}

// ============================================================
// MPM derivation (§8.4.2)
// ============================================================

static void derive_mpm(const DecodingContext& ctx, int x0, int y0, int /*puSize*/,
                        int candModeList[3]) {
    // Left neighbour
    int xL = x0 - 1;
    int candA = 1; // DC default if not available
    if (xL >= 0) {
        const auto& cuL = ctx.cu_at(xL, y0);
        if (cuL.pred_mode == PredMode::MODE_INTRA)
            candA = ctx.intra_mode_at(xL, y0);
    }

    // Above neighbour
    int yA = y0 - 1;
    int candB = 1; // DC default if not available
    if (yA >= 0) {
        // §8.4.2: if yPb-1 < ((yPb >> CtbLog2SizeY) << CtbLog2SizeY),
        // the above neighbour is in a different CTB row → use DC
        int ctbRowStart = (y0 >> ctx.sps->CtbLog2SizeY) << ctx.sps->CtbLog2SizeY;
        if (yA >= ctbRowStart) {
            const auto& cuA = ctx.cu_at(x0, yA);
            if (cuA.pred_mode == PredMode::MODE_INTRA)
                candB = ctx.intra_mode_at(x0, yA);
        }
    }

    // Derive 3 MPM candidates
    if (candA == candB) {
        if (candA < 2) {
            candModeList[0] = 0;  // Planar
            candModeList[1] = 1;  // DC
            candModeList[2] = 26; // Vertical
        } else {
            candModeList[0] = candA;
            candModeList[1] = 2 + ((candA - 2 + 29) % 32);
            candModeList[2] = 2 + ((candA - 2 + 1) % 32);
        }
    } else {
        candModeList[0] = candA;
        candModeList[1] = candB;
        if (candA != 0 && candB != 0)
            candModeList[2] = 0;  // Planar
        else if (candA != 1 && candB != 1)
            candModeList[2] = 1;  // DC
        else
            candModeList[2] = 26; // Vertical
    }
}

// ============================================================
// Chroma intra mode derivation (§8.4.3)
// ============================================================

static int derive_chroma_intra_mode(int coded_mode, int luma_mode) {
    // coded_mode: 0=Planar, 1=V(26), 2=H(10), 3=DC(1), 4=DM(=luma_mode)
    if (coded_mode == 4) return luma_mode;

    static const int chroma_cand[4] = { 0, 26, 10, 1 };
    int mode = chroma_cand[coded_mode];

    // If chroma mode equals luma mode, replace with mode 34
    if (mode == luma_mode) mode = 34;

    return mode;
}

// ============================================================
// QP derivation (§8.6.1)
// ============================================================

static int derive_qp_y(DecodingContext& ctx, int x0, int y0) {
    auto& sps = *ctx.sps;
    auto& pps = *ctx.pps;
    auto& sh = *ctx.sh;

    if (!pps.cu_qp_delta_enabled_flag) {
        return sh.SliceQpY;
    }

    if (!ctx.IsCuQpDeltaCoded) {
        return ctx.QpY_prev;
    }

    // QP prediction: average of left and above neighbours
    int qpPredA = ctx.QpY_prev;
    int qpPredB = ctx.QpY_prev;

    int xL = x0 - 1;
    if (xL >= 0) {
        qpPredA = ctx.cu_at(xL, y0).qp_y;
    }

    int yA = y0 - 1;
    if (yA >= 0) {
        qpPredB = ctx.cu_at(x0, yA).qp_y;
    }

    int qpPred = (qpPredA + qpPredB + 1) >> 1;

    int QpY = ((qpPred + ctx.CuQpDeltaVal + 52 + 2 * sps.QpBdOffsetY) %
               (52 + sps.QpBdOffsetY)) - sps.QpBdOffsetY;

    return QpY;
}

// ============================================================
// Reconstruction: pred + residual, clipping (§8.6.5)
// ============================================================

static void reconstruct_block(DecodingContext& ctx, int x0, int y0,
                               int log2Size, int cIdx,
                               const int16_t* pred, const int16_t* residual) {
    int size = 1 << log2Size;
    int bitDepth = (cIdx == 0) ? ctx.sps->BitDepthY : ctx.sps->BitDepthC;
    int maxVal = (1 << bitDepth) - 1;
    auto& pic = *ctx.pic;

    // For chroma, convert luma coordinates to chroma
    int xC = (cIdx > 0) ? x0 / ctx.sps->SubWidthC : x0;
    int yC = (cIdx > 0) ? y0 / ctx.sps->SubHeightC : y0;

    int picW = (cIdx == 0) ? ctx.sps->pic_width_in_luma_samples :
               ctx.sps->pic_width_in_luma_samples / ctx.sps->SubWidthC;
    int picH = (cIdx == 0) ? ctx.sps->pic_height_in_luma_samples :
               ctx.sps->pic_height_in_luma_samples / ctx.sps->SubHeightC;

    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            if (xC + i >= picW || yC + j >= picH) continue;
            int val = pred[j * size + i] + residual[j * size + i];
            val = Clip3(0, maxVal, val);
            pic.sample(cIdx, xC + i, yC + j) = static_cast<uint16_t>(val);
        }
    }
}

// ============================================================
// slice_segment_data (§7.3.8.1)
// ============================================================

bool decode_slice_segment_data(DecodingContext& ctx, BitstreamReader& bs) {
    auto& sps = *ctx.sps;
    auto& pps = *ctx.pps;
    auto& sh = *ctx.sh;
    auto& cabac = *ctx.cabac;

    // Initialize CABAC
    if (!sh.dependent_slice_segment_flag) {
        int sliceType = static_cast<int>(sh.slice_type);
        cabac.init_contexts(sliceType, sh.SliceQpY, sh.cabac_init_flag);
    }
    cabac.init_decoder(bs);

    // Init QP
    ctx.QpY_prev = sh.SliceQpY;

    // CTU scan
    int CtbAddrInTs = pps.CtbAddrRsToTs[sh.slice_segment_address];
    int CtbAddrInRs = sh.slice_segment_address;

    HEVC_LOG(TREE, "slice_segment_data: addr=%d type=%d QP=%d",
             sh.slice_segment_address, static_cast<int>(sh.slice_type), sh.SliceQpY);

    bool end_of_slice = false;
    while (!end_of_slice) {
        int xCtb = (CtbAddrInRs % sps.PicWidthInCtbsY) << sps.CtbLog2SizeY;
        int yCtb = (CtbAddrInRs / sps.PicWidthInCtbsY) << sps.CtbLog2SizeY;

        size_t bits_before = bs.bits_read();
        HEVC_LOG(TREE, "CTU addr_rs=%d pos=(%d,%d) bits=%zu", CtbAddrInRs, xCtb, yCtb, bits_before);

        decode_coding_tree_unit(ctx, xCtb, yCtb);

        HEVC_LOG(TREE, "CTU addr_rs=%d consumed %zu bits, remaining=%zu",
                 CtbAddrInRs, bs.bits_read() - bits_before, bs.bits_remaining());

        end_of_slice = decode_end_of_slice_segment_flag(cabac);

        if (!end_of_slice) {
            CtbAddrInTs++;
            if (CtbAddrInTs >= sps.PicSizeInCtbsY) break;
            CtbAddrInRs = pps.CtbAddrTsToRs[CtbAddrInTs];

            // Tile/WPP boundary: skip subset end bit + byte alignment
            if (pps.tiles_enabled_flag &&
                pps.TileId[CtbAddrInTs] != pps.TileId[CtbAddrInTs - 1]) {
                cabac.decode_terminate(); // end_of_subset_one_bit
                bs.byte_alignment();
                cabac.init_decoder(bs);
            }
        }
    }

    return true;
}

// ============================================================
// coding_tree_unit (§7.3.8.2)
// ============================================================

void decode_coding_tree_unit(DecodingContext& ctx, int xCtb, int yCtb) {
    // SAO parsing (store params for Phase 6)
    if (ctx.sh->slice_sao_luma_flag || ctx.sh->slice_sao_chroma_flag) {
        int rx = xCtb >> ctx.sps->CtbLog2SizeY;
        int ry = yCtb >> ctx.sps->CtbLog2SizeY;
        decode_sao(ctx, rx, ry, nullptr);
    }

    // QP group reset
    if (ctx.pps->cu_qp_delta_enabled_flag) {
        ctx.IsCuQpDeltaCoded = false;
        ctx.CuQpDeltaVal = 0;
    }

    decode_coding_quadtree(ctx, xCtb, yCtb, ctx.sps->CtbLog2SizeY, 0);
}

// ============================================================
// coding_quadtree (§7.3.8.4)
// ============================================================

void decode_coding_quadtree(DecodingContext& ctx, int x0, int y0,
                            int log2CbSize, int ctDepth) {
    auto& sps = *ctx.sps;
    auto& pps = *ctx.pps;
    int cbSize = 1 << log2CbSize;

    bool split = false;

    // Determine if split_cu_flag needs to be read
    bool atBoundary = (x0 + cbSize > static_cast<int>(sps.pic_width_in_luma_samples)) ||
                      (y0 + cbSize > static_cast<int>(sps.pic_height_in_luma_samples));

    if (atBoundary) {
        // Force split if we exceed picture boundaries and can still split
        if (log2CbSize > sps.MinCbLog2SizeY)
            split = true;
    } else if (log2CbSize > sps.MinCbLog2SizeY) {
        int ctxInc = derive_split_cu_flag_ctx(ctx, x0, y0, log2CbSize);
        split = decode_split_cu_flag(*ctx.cabac, ctxInc);
    }

    // QP group boundary
    if (pps.cu_qp_delta_enabled_flag && log2CbSize >= pps.Log2MinCuQpDeltaSize) {
        ctx.IsCuQpDeltaCoded = false;
        ctx.CuQpDeltaVal = 0;
    }

    if (split) {
        int x1 = x0 + (1 << (log2CbSize - 1));
        int y1 = y0 + (1 << (log2CbSize - 1));
        int picW = static_cast<int>(sps.pic_width_in_luma_samples);
        int picH = static_cast<int>(sps.pic_height_in_luma_samples);

        decode_coding_quadtree(ctx, x0, y0, log2CbSize - 1, ctDepth + 1);
        if (x1 < picW)
            decode_coding_quadtree(ctx, x1, y0, log2CbSize - 1, ctDepth + 1);
        if (y1 < picH)
            decode_coding_quadtree(ctx, x0, y1, log2CbSize - 1, ctDepth + 1);
        if (x1 < picW && y1 < picH)
            decode_coding_quadtree(ctx, x1, y1, log2CbSize - 1, ctDepth + 1);
    } else {
        decode_coding_unit(ctx, x0, y0, log2CbSize);
    }
}

// ============================================================
// coding_unit (§7.3.8.5)
// ============================================================

void decode_coding_unit(DecodingContext& ctx, int x0, int y0, int log2CbSize) {
    auto& sps = *ctx.sps;
    auto& pps = *ctx.pps;
    auto& sh = *ctx.sh;
    auto& cabac = *ctx.cabac;

    int cbSize = 1 << log2CbSize;

    bool cu_transquant_bypass = false;
    if (pps.transquant_bypass_enabled_flag) {
        cu_transquant_bypass = decode_cu_transquant_bypass_flag(cabac);
    }

    bool cu_skip = false;
    if (sh.slice_type != SliceType::I) {
        int ctxInc = derive_cu_skip_flag_ctx(ctx, x0, y0);
        cu_skip = decode_cu_skip_flag(cabac, ctxInc);
    }

    PredMode pred_mode = PredMode::MODE_INTRA;
    PartMode part_mode = PartMode::PART_2Nx2N;

    if (cu_skip) {
        // Skip mode — inter prediction (Phase 5)
        pred_mode = PredMode::MODE_SKIP;
        HEVC_LOG(TREE, "CU (%d,%d) %dx%d SKIP", x0, y0, cbSize, cbSize);
    } else {
        if (sh.slice_type != SliceType::I) {
            pred_mode = decode_pred_mode_flag(cabac) ?
                        PredMode::MODE_INTRA : PredMode::MODE_INTER;
        }

        if (pred_mode != PredMode::MODE_INTRA ||
            log2CbSize == sps.MinCbLog2SizeY) {
            part_mode = static_cast<PartMode>(
                decode_part_mode(cabac, pred_mode, log2CbSize,
                                 sps.MinCbLog2SizeY, sps.amp_enabled_flag));
        }
        HEVC_LOG(TREE, "CU (%d,%d) %dx%d pred=%d part=%d",
                 x0, y0, cbSize, cbSize, static_cast<int>(pred_mode),
                 static_cast<int>(part_mode));

        if (pred_mode == PredMode::MODE_INTRA) {
            // Check PCM
            bool is_pcm = false;
            if (part_mode == PartMode::PART_2Nx2N && sps.pcm_enabled_flag &&
                log2CbSize >= sps.Log2MinIpcmCbSizeY &&
                log2CbSize <= sps.Log2MaxIpcmCbSizeY) {
                is_pcm = cabac.decode_terminate();
            }

            if (is_pcm) {
                decode_pcm_samples(ctx, x0, y0, log2CbSize);
                // Store CU info
                int n = cbSize / sps.MinCbSizeY;
                for (int j = 0; j < n; j++)
                    for (int i = 0; i < n; i++) {
                        auto& cu = ctx.cu_at(x0 + i * sps.MinCbSizeY,
                                              y0 + j * sps.MinCbSizeY);
                        cu.pred_mode = PredMode::MODE_INTRA;
                        cu.log2CbSize = log2CbSize;
                        cu.is_pcm = true;
                        cu.qp_y = ctx.QpY_prev;
                    }
                return;
            }

            // Intra prediction
            decode_prediction_unit_intra(ctx, x0, y0, log2CbSize, part_mode);
        }
        // Inter prediction handled in Phase 5
    }

    // Store CU info in grid
    int n = cbSize / sps.MinCbSizeY;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            auto& cu = ctx.cu_at(x0 + i * sps.MinCbSizeY,
                                  y0 + j * sps.MinCbSizeY);
            cu.pred_mode = pred_mode;
            cu.part_mode = part_mode;
            cu.log2CbSize = log2CbSize;
            cu.cu_transquant_bypass = cu_transquant_bypass;
        }

    // Transform tree (only for non-skip, non-PCM)
    if (!cu_skip) {
        bool rqt_root_cbf = true;
        if (pred_mode != PredMode::MODE_INTRA) {
            // For inter: parse rqt_root_cbf if not merge-2Nx2N
            // Phase 5 — for now assume cbf=1
        }

        if (rqt_root_cbf) {
            decode_transform_tree(ctx, x0, y0, x0, y0, log2CbSize, 0, 0,
                                  true, true);
        }
    }

    // Derive QP
    int qpY = derive_qp_y(ctx, x0, y0);
    ctx.QpY_prev = qpY;

    // Store QP in grid
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            ctx.cu_at(x0 + i * sps.MinCbSizeY,
                      y0 + j * sps.MinCbSizeY).qp_y = qpY;
        }
}

// ============================================================
// prediction_unit intra (§7.3.8.8 — intra part)
// ============================================================

void decode_prediction_unit_intra(DecodingContext& ctx, int x0, int y0,
                                   int log2CbSize, PartMode part_mode) {
    auto& cabac = *ctx.cabac;
    auto& sps = *ctx.sps;

    int cbSize = 1 << log2CbSize;
    int pbOffset = (part_mode == PartMode::PART_NxN) ? (cbSize / 2) : cbSize;
    // First pass: decode prev_intra_luma_pred_flag for all PUs
    bool prev_flag[4] = {};
    int mpm_idx_val[4] = {};
    int rem_mode[4] = {};

    int pu = 0;
    for (int j = 0; j < cbSize; j += pbOffset) {
        for (int i = 0; i < cbSize; i += pbOffset) {
            prev_flag[pu] = decode_prev_intra_luma_pred_flag(cabac);
            pu++;
        }
    }

    // Second pass: decode mpm_idx or rem_intra_luma_pred_mode
    pu = 0;
    for (int j = 0; j < cbSize; j += pbOffset) {
        for (int i = 0; i < cbSize; i += pbOffset) {
            if (prev_flag[pu])
                mpm_idx_val[pu] = decode_mpm_idx(cabac);
            else
                rem_mode[pu] = decode_rem_intra_luma_pred_mode(cabac);
            pu++;
        }
    }

    // Derive luma intra modes
    pu = 0;
    for (int j = 0; j < cbSize; j += pbOffset) {
        for (int i = 0; i < cbSize; i += pbOffset) {
            int px = x0 + i;
            int py = y0 + j;

            int candModeList[3];
            derive_mpm(ctx, px, py, pbOffset, candModeList);

            int intra_mode;
            if (prev_flag[pu]) {
                intra_mode = candModeList[mpm_idx_val[pu]];
            } else {
                // Sort candidates
                if (candModeList[0] > candModeList[1])
                    std::swap(candModeList[0], candModeList[1]);
                if (candModeList[0] > candModeList[2])
                    std::swap(candModeList[0], candModeList[2]);
                if (candModeList[1] > candModeList[2])
                    std::swap(candModeList[1], candModeList[2]);

                intra_mode = rem_mode[pu];
                for (int k = 0; k < 3; k++) {
                    if (intra_mode >= candModeList[k])
                        intra_mode++;
                }
            }
            HEVC_LOG(INTRA, "PU (%d,%d) luma_mode=%d (prev=%d mpm_idx=%d rem=%d)",
                     px, py, intra_mode, prev_flag[pu], mpm_idx_val[pu], rem_mode[pu]);

            ctx.set_intra_mode(px, py, pbOffset, intra_mode);
            pu++;
        }
    }

    // Chroma mode
    if (sps.ChromaArrayType != 0) {
        int coded_chroma = decode_intra_chroma_pred_mode(cabac);
        // For 4:2:0/4:2:2, one chroma mode per CU
        int luma_mode_for_chroma = ctx.intra_mode_at(x0, y0);
        int chroma_mode = derive_chroma_intra_mode(coded_chroma, luma_mode_for_chroma);
        ctx.set_chroma_mode(x0, y0, cbSize, chroma_mode);
        HEVC_LOG(INTRA, "CU (%d,%d) chroma_mode=%d (coded=%d luma=%d)",
                 x0, y0, chroma_mode, coded_chroma, luma_mode_for_chroma);
    }
}

// ============================================================
// transform_tree (§7.3.8.8)
// ============================================================

void decode_transform_tree(DecodingContext& ctx, int x0, int y0,
                           int xBase, int yBase,
                           int log2TrafoSize, int trafoDepth,
                           int blkIdx,
                           bool cbf_cb_parent, bool cbf_cr_parent) {
    auto& sps = *ctx.sps;
    auto& cabac = *ctx.cabac;

    bool split = false;

    // IntraSplitFlag — Table 7-10: only set when PartMode == NxN for intra
    auto& cu = ctx.cu_at(x0, y0);
    bool intraSplitFlag = (cu.pred_mode == PredMode::MODE_INTRA &&
                           cu.part_mode == PartMode::PART_NxN);
    int maxTrafoDepth;
    if (cu.pred_mode == PredMode::MODE_INTRA) {
        maxTrafoDepth = sps.max_transform_hierarchy_depth_intra +
                        (intraSplitFlag ? 1 : 0);
    } else {
        maxTrafoDepth = sps.max_transform_hierarchy_depth_inter;
    }

    // Determine split_transform_flag
    if (log2TrafoSize <= sps.MaxTbLog2SizeY &&
        log2TrafoSize > sps.MinTbLog2SizeY &&
        trafoDepth < maxTrafoDepth &&
        !(intraSplitFlag && trafoDepth == 0)) {
        split = decode_split_transform_flag(cabac, log2TrafoSize);
    } else {
        // Implicit split
        split = (log2TrafoSize > sps.MaxTbLog2SizeY) ||
                (intraSplitFlag && trafoDepth == 0);
    }

    // Chroma CBF (only when log2TrafoSize > 2 for 4:2:0)
    bool cbf_cb = false, cbf_cr = false;
    if ((log2TrafoSize > 2 && sps.ChromaArrayType != 0) ||
        sps.ChromaArrayType == 3) {
        if (trafoDepth == 0 || cbf_cb_parent) {
            cbf_cb = decode_cbf_chroma(cabac, trafoDepth);
        }
        if (trafoDepth == 0 || cbf_cr_parent) {
            cbf_cr = decode_cbf_chroma(cabac, trafoDepth);
        }
    }

    if (split) {
        int x1 = x0 + (1 << (log2TrafoSize - 1));
        int y1 = y0 + (1 << (log2TrafoSize - 1));

        decode_transform_tree(ctx, x0, y0, x0, y0, log2TrafoSize - 1,
                              trafoDepth + 1, 0, cbf_cb, cbf_cr);
        decode_transform_tree(ctx, x1, y0, x0, y0, log2TrafoSize - 1,
                              trafoDepth + 1, 1, cbf_cb, cbf_cr);
        decode_transform_tree(ctx, x0, y1, x0, y0, log2TrafoSize - 1,
                              trafoDepth + 1, 2, cbf_cb, cbf_cr);
        decode_transform_tree(ctx, x1, y1, x0, y0, log2TrafoSize - 1,
                              trafoDepth + 1, 3, cbf_cb, cbf_cr);
    } else {
        // Leaf: read cbf_luma and decode transform unit
        bool cbf_luma = true;
        if (cu.pred_mode == PredMode::MODE_INTRA || trafoDepth != 0 ||
            cbf_cb || cbf_cr) {
            cbf_luma = decode_cbf_luma(cabac, trafoDepth);
        }

        decode_transform_unit(ctx, x0, y0, xBase, yBase, log2TrafoSize, trafoDepth, blkIdx,
                              cbf_luma, cbf_cb, cbf_cr);
    }
}

// ============================================================
// transform_unit (§7.3.8.10)
// ============================================================

void decode_transform_unit(DecodingContext& ctx, int x0, int y0,
                           int xBase, int yBase,
                           int log2TrafoSize, int /*trafoDepth*/,
                           int blkIdx,
                           bool cbf_luma, bool cbf_cb, bool cbf_cr) {
    auto& sps = *ctx.sps;
    auto& pps = *ctx.pps;
    auto& cabac = *ctx.cabac;
    auto& cu = ctx.cu_at(x0, y0);

    int trSize = 1 << log2TrafoSize;

    // QP delta
    if ((cbf_luma || cbf_cb || cbf_cr) &&
        pps.cu_qp_delta_enabled_flag && !ctx.IsCuQpDeltaCoded) {
        ctx.CuQpDeltaVal = decode_cu_qp_delta(cabac);
        ctx.IsCuQpDeltaCoded = true;
    }

    int qpY = derive_qp_y(ctx, x0, y0);

    // Luma residual
    if (cbf_luma) {
        int16_t coefficients[64 * 64] = {};
        int16_t scaled[64 * 64] = {};
        int16_t residual[64 * 64] = {};

        bool transform_skip = false;
        if (pps.transform_skip_enabled_flag && !cu.cu_transquant_bypass &&
            log2TrafoSize <= 2) {
            transform_skip = decode_transform_skip_flag(cabac, 0);
        }

        decode_residual_coding(ctx, x0, y0, log2TrafoSize, 0, coefficients);

        if (!cu.cu_transquant_bypass) {
            int qpPrime = qpY + sps.QpBdOffsetY;
            perform_dequant(ctx, x0, y0, log2TrafoSize, 0, qpPrime,
                           coefficients, scaled);

            perform_transform_inverse(log2TrafoSize, 0,
                                       cu.pred_mode == PredMode::MODE_INTRA,
                                       transform_skip, sps.BitDepthY,
                                       scaled, residual);
        } else {
            std::memcpy(residual, coefficients, sizeof(int16_t) * trSize * trSize);
        }

        // Intra prediction for luma
        int intra_mode = ctx.intra_mode_at(x0, y0);
        int16_t pred_samples[64 * 64] = {};
        perform_intra_prediction(ctx, x0, y0, log2TrafoSize, 0, intra_mode,
                                 pred_samples);

        // Reconstruct
        reconstruct_block(ctx, x0, y0, log2TrafoSize, 0, pred_samples, residual);

    } else if (cu.pred_mode == PredMode::MODE_INTRA) {
        // No residual but still need intra prediction
        int intra_mode = ctx.intra_mode_at(x0, y0);
        int16_t pred_samples[64 * 64] = {};
        perform_intra_prediction(ctx, x0, y0, log2TrafoSize, 0, intra_mode,
                                 pred_samples);

        // Reconstruct with zero residual
        int16_t zero[64 * 64] = {};
        reconstruct_block(ctx, x0, y0, log2TrafoSize, 0, pred_samples, zero);

    }

    // Chroma residual (4:2:0: chroma TU is log2TrafoSize-1, min 2)
    if (sps.ChromaArrayType != 0) {
        int log2TrafoSizeC = std::max(2, log2TrafoSize - 1);
        int trSizeC = 1 << log2TrafoSizeC;

        // For 4:2:0 with log2TrafoSize==2, chroma is deferred to blkIdx==3
        bool processChroma = (log2TrafoSize > 2) || (blkIdx == 3);

        // §7.3.8.10: chroma position uses xBase/yBase when log2TrafoSize==2
        int xC = (sps.ChromaArrayType != 3 && log2TrafoSize == 2) ? xBase : x0;
        int yC = (sps.ChromaArrayType != 3 && log2TrafoSize == 2) ? yBase : y0;

        if (processChroma) {
            for (int cIdx = 1; cIdx <= 2; cIdx++) {
                bool cbf_c = (cIdx == 1) ? cbf_cb : cbf_cr;
                if (cbf_c) {
                    int16_t coefficients[32 * 32] = {};
                    int16_t scaled[32 * 32] = {};
                    int16_t residual[32 * 32] = {};

                    bool transform_skip = false;
                    if (pps.transform_skip_enabled_flag &&
                        !cu.cu_transquant_bypass && log2TrafoSizeC <= 2) {
                        transform_skip = decode_transform_skip_flag(cabac, cIdx);
                    }

                    decode_residual_coding(ctx, xC, yC, log2TrafoSizeC, cIdx,
                                          coefficients);

                    if (!cu.cu_transquant_bypass) {
                        // Chroma QP derivation
                        int qpOffset = (cIdx == 1) ? pps.pps_cb_qp_offset +
                                        ctx.sh->slice_cb_qp_offset
                                      : pps.pps_cr_qp_offset +
                                        ctx.sh->slice_cr_qp_offset;
                        int qPi = Clip3(-sps.QpBdOffsetC, 57, qpY + qpOffset);
                        int qPc;
                        if (qPi < 0) qPc = qPi;
                        else if (qPi < 58) qPc = qpChromaTable[qPi];
                        else qPc = qPi - 6;
                        int qpPrimeC = qPc + sps.QpBdOffsetC;

                        perform_dequant(ctx, xC, yC, log2TrafoSizeC, cIdx,
                                       qpPrimeC, coefficients, scaled);
                        perform_transform_inverse(log2TrafoSizeC, cIdx,
                                                   cu.pred_mode == PredMode::MODE_INTRA,
                                                   transform_skip, sps.BitDepthC,
                                                   scaled, residual);
                    } else {
                        std::memcpy(residual, coefficients,
                                    sizeof(int16_t) * trSizeC * trSizeC);
                    }

                    // Chroma intra prediction
                    int chroma_mode = ctx.chroma_mode_at(xC, yC);
                    int16_t pred_samples[32 * 32] = {};
                    perform_intra_prediction(ctx, xC, yC, log2TrafoSizeC, cIdx,
                                            chroma_mode, pred_samples);

                    reconstruct_block(ctx, xC, yC, log2TrafoSizeC, cIdx,
                                     pred_samples, residual);
                } else if (cu.pred_mode == PredMode::MODE_INTRA) {
                    int chroma_mode = ctx.chroma_mode_at(xC, yC);
                    int16_t pred_samples[32 * 32] = {};
                    perform_intra_prediction(ctx, xC, yC, log2TrafoSizeC, cIdx,
                                            chroma_mode, pred_samples);
                    int16_t zero[32 * 32] = {};
                    reconstruct_block(ctx, xC, yC, log2TrafoSizeC, cIdx,
                                     pred_samples, zero);
                }
            }
        }
    }
}

// ============================================================
// PCM mode (§7.3.10.2)
// ============================================================

void decode_pcm_samples(DecodingContext& ctx, int x0, int y0, int log2CbSize) {
    auto& sps = *ctx.sps;
    auto bs = ctx.cabac->bitstream();

    HEVC_LOG(TREE, "PCM samples at (%d,%d) size=%d", x0, y0, 1 << log2CbSize);

    // Byte alignment before PCM data
    bs->byte_alignment();

    int cbSize = 1 << log2CbSize;
    int numLumaSamples = cbSize * cbSize;
    int lumaBits = sps.pcm_sample_bit_depth_luma_minus1 + 1;

    // Read luma samples
    for (int i = 0; i < numLumaSamples; i++) {
        int y = i / cbSize;
        int x = i % cbSize;
        uint16_t val = static_cast<uint16_t>(bs->read_bits(lumaBits));
        ctx.pic->sample(0, x0 + x, y0 + y) = val;
    }

    // Read chroma samples
    if (sps.ChromaArrayType != 0) {
        int chromaBits = sps.pcm_sample_bit_depth_chroma_minus1 + 1;
        int chromaW = cbSize / sps.SubWidthC;
        int chromaH = cbSize / sps.SubHeightC;
        int numChromaSamples = chromaW * chromaH;

        for (int cIdx = 1; cIdx <= 2; cIdx++) {
            int xC = x0 / sps.SubWidthC;
            int yC = y0 / sps.SubHeightC;
            for (int i = 0; i < numChromaSamples; i++) {
                int y = i / chromaW;
                int x = i % chromaW;
                uint16_t val = static_cast<uint16_t>(bs->read_bits(chromaBits));
                ctx.pic->sample(cIdx, xC + x, yC + y) = val;
            }
        }
    }

    // Reset CABAC contexts after PCM (§9.3.1.2)
    int sliceType = static_cast<int>(ctx.sh->slice_type);
    ctx.cabac->init_contexts(sliceType, ctx.sh->SliceQpY, ctx.sh->cabac_init_flag);
    ctx.cabac->init_decoder(*bs);
}

} // namespace hevc
