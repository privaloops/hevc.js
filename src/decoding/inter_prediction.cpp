#include "decoding/inter_prediction.h"
#include "decoding/coding_tree.h"
#include "decoding/dpb.h"
#include "decoding/syntax_elements.h"
#include "common/debug.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>

namespace hevc {

// ============================================================
// PU motion grid access
// ============================================================

void store_pu_motion(DecodingContext& ctx, int xPb, int yPb,
                     int nPbW, int nPbH, const PUMotionInfo& mi) {
    int minPuSize = ctx.sps->MinTbSizeY;  // 4x4 granularity
    int stride = ctx.motion_info_stride;
    for (int y = 0; y < nPbH; y += minPuSize) {
        for (int x = 0; x < nPbW; x += minPuSize) {
            int idx = ((yPb + y) / minPuSize) * stride + ((xPb + x) / minPuSize);
            ctx.motion_info[idx] = mi;
        }
    }
}

PUMotionInfo get_pu_motion(const DecodingContext& ctx, int x, int y) {
    int minPuSize = ctx.sps->MinTbSizeY;
    int stride = ctx.motion_info_stride;
    int idx = (y / minPuSize) * stride + (x / minPuSize);
    return ctx.motion_info[idx];
}

// ============================================================
// §6.4.2 — Prediction block availability (simplified for inter)
// ============================================================

static bool is_pu_available(const DecodingContext& ctx,
                             int xCb, int yCb, int nCbS,
                             int xPb, int yPb, int nPbW, int nPbH,
                             int xNb, int yNb, int partIdx) {
    auto& sps = *ctx.sps;
    int picW = static_cast<int>(sps.pic_width_in_luma_samples);
    int picH = static_cast<int>(sps.pic_height_in_luma_samples);

    // Out of picture
    if (xNb < 0 || yNb < 0 || xNb >= picW || yNb >= picH)
        return false;

    // §6.4.1 z-scan availability
    int minTb = sps.MinTbSizeY;
    int ctbSize = static_cast<int>(sps.CtbSizeY);
    int curCtbX = xPb / ctbSize;
    int curCtbY = yPb / ctbSize;
    int nbCtbX = xNb / ctbSize;
    int nbCtbY = yNb / ctbSize;

    if (nbCtbX != curCtbX || nbCtbY != curCtbY) {
        // Different CTU — available if CTU was already decoded
        int nbAddr = nbCtbY * sps.PicWidthInCtbsY + nbCtbX;
        int curAddr = curCtbY * sps.PicWidthInCtbsY + curCtbX;
        if (nbAddr > curAddr) return false;
    } else {
        // Same CTU — z-scan check
        auto zscan = [](int bx, int by) -> uint32_t {
            uint32_t z = 0;
            for (int i = 0; i < 8; i++) {
                z |= ((bx >> i) & 1) << (2 * i);
                z |= ((by >> i) & 1) << (2 * i + 1);
            }
            return z;
        };
        int ctbOrgX = curCtbX * ctbSize;
        int ctbOrgY = curCtbY * ctbSize;
        uint32_t curZ = zscan((xPb - ctbOrgX) / minTb, (yPb - ctbOrgY) / minTb);
        uint32_t nbZ = zscan((xNb - ctbOrgX) / minTb, (yNb - ctbOrgY) / minTb);
        if (nbZ > curZ) return false;
    }

    // §6.4.2: check if same CU and prohibited partition
    // (e.g., for Nx2N partIdx=1, left neighbor in same CU is prohibited)
    bool sameCb = (xNb >= xCb && yNb >= yCb &&
                   xNb < xCb + nCbS && yNb < yCb + nCbS);
    if (sameCb) {
        if (nPbW * 2 == nCbS && nPbH * 2 == nCbS && partIdx == 1 &&
            yNb >= yCb + nPbH && xNb < xCb + nPbW)
            return false;
    }

    // Must be inter mode
    if (ctx.cu_at(xNb, yNb).pred_mode == PredMode::MODE_INTRA)
        return false;

    return true;
}

// ============================================================
// §8.5.3.2.12 — MV scaling (by POC distance)
// ============================================================

static MV scale_mv(MV mv, int currPOC, int currRefPOC, int colPOC, int colRefPOC) {
    // Spec §8.5.3.2.12 eq 8-218 to 8-221
    int td = Clip3(-128, 127, colPOC - colRefPOC);
    int tb = Clip3(-128, 127, currPOC - currRefPOC);

    if (td == 0 || td == tb) return mv;

    // §8.5.3.2.12 eq 8-219: tx = (16384 + (Abs(td) >> 1)) / td
    int tx = (16384 + (std::abs(td) >> 1)) / td;
    // §8.5.3.2.12 eq 8-220: distScaleFactor = Clip3(-4096, 4095, (tb * tx + 32) >> 6)
    int distScaleFactor = Clip3(-4096, 4095, (tb * tx + 32) >> 6);

    MV scaled;
    // §8.5.3.2.12 eq 8-221
    scaled.x = static_cast<int16_t>(Clip3(-32768, 32767,
        (distScaleFactor * mv.x + 128 - (distScaleFactor * mv.x >= 0 ? 0 : 1)) >> 8));
    scaled.y = static_cast<int16_t>(Clip3(-32768, 32767,
        (distScaleFactor * mv.y + 128 - (distScaleFactor * mv.y >= 0 ? 0 : 1)) >> 8));
    return scaled;
}

// ============================================================
// §8.5.3.2.8 — Temporal MV prediction (TMVP)
// ============================================================

static bool derive_temporal_mv(const DecodingContext& ctx,
                                int xPb, int yPb, int nPbW, int nPbH,
                                int refIdxLX, int listX,
                                MV& mvLXCol) {
    // Simplified TMVP: check bottom-right then center of collocated PU
    if (!ctx.dpb || !ctx.dpb->col_pic()) return false;

    Picture* colPic = ctx.dpb->col_pic();
    int picW = static_cast<int>(ctx.sps->pic_width_in_luma_samples);
    int picH = static_cast<int>(ctx.sps->pic_height_in_luma_samples);

    // §8.5.3.2.8: try bottom-right first, then center
    // Bottom-right: (xPb + nPbW, yPb + nPbH) — but must be in same/next CTU row
    int xColBr = xPb + nPbW;
    int yColBr = yPb + nPbH;

    // Check if bottom-right is valid (§8.5.3.2.8 constraints)
    bool brValid = (xColBr < picW && yColBr < picH &&
                    (yColBr >> (ctx.sps->CtbLog2SizeY)) <= (yPb >> (ctx.sps->CtbLog2SizeY)));

    int xCol, yCol;
    if (brValid) {
        xCol = xColBr;
        yCol = yColBr;
    } else {
        // Center of PU
        xCol = xPb + (nPbW >> 1);
        yCol = yPb + (nPbH >> 1);
    }

    // The collocated picture must have motion info stored
    if (!colPic->motion_info || colPic->motion_info_stride == 0) return false;

    int minPuSize = ctx.sps->MinTbSizeY;
    int miIdx = (yCol / minPuSize) * colPic->motion_info_stride + (xCol / minPuSize);
    const auto& colMi = colPic->motion_info[miIdx];

    // §8.5.3.2.8: find available MV in collocated PU
    // Try same list first, then other list
    int colRefPOC = -1;
    MV colMV = {};
    bool found = false;

    for (int tryList = 0; tryList < 2 && !found; tryList++) {
        int l = (tryList == 0) ? listX : (1 - listX);
        if (colMi.pred_flag[l]) {
            colMV.x = colMi.mv_x[l];
            colMV.y = colMi.mv_y[l];
            if (l < 2 && colMi.ref_idx[l] >= 0 &&
                colMi.ref_idx[l] < static_cast<int>(colPic->ref_poc[l].size())) {
                colRefPOC = colPic->ref_poc[l][colMi.ref_idx[l]];
                found = true;
            }
        }
    }

    if (!found) return false;

    // Scale MV by POC distance
    int currPOC = ctx.pic->poc;
    Picture* currRef = (listX == 0) ? ctx.dpb->ref_pic_list0(refIdxLX)
                                     : ctx.dpb->ref_pic_list1(refIdxLX);
    if (!currRef) return false;
    int currRefPOC = currRef->poc;

    mvLXCol = scale_mv(colMV, currPOC, currRefPOC, colPic->poc, colRefPOC);
    return true;
}

// ============================================================
// §8.5.3.2.3 — Spatial merge candidates
// ============================================================

static void derive_spatial_merge_candidates(
    const DecodingContext& ctx,
    int xCb, int yCb, int nCbS,
    int xPb, int yPb, int nPbW, int nPbH, int partIdx,
    MergeCandidate cands[5], bool avail[5]) {

    // §8.5.3.2.3: Candidate positions
    // A1: (xPb - 1, yPb + nPbH - 1)
    // B1: (xPb + nPbW - 1, yPb - 1)
    // B0: (xPb + nPbW, yPb - 1)
    // A0: (xPb - 1, yPb + nPbH)
    // B2: (xPb - 1, yPb - 1)
    struct { int x, y; } nbPos[5] = {
        { xPb - 1,     yPb + nPbH - 1 },  // A1
        { xPb + nPbW - 1, yPb - 1 },       // B1
        { xPb + nPbW,     yPb - 1 },       // B0
        { xPb - 1,        yPb + nPbH },    // A0
        { xPb - 1,        yPb - 1 },       // B2
    };

    int Log2ParMrgLevel = ctx.pps->log2_parallel_merge_level_minus2 + 2;

    for (int i = 0; i < 5; i++) {
        avail[i] = false;
        cands[i] = {};
    }

    // Check each candidate
    for (int i = 0; i < 5; i++) {
        int xNb = nbPos[i].x;
        int yNb = nbPos[i].y;

        // §8.5.3.2.3: additional pruning conditions per candidate
        bool available = is_pu_available(ctx, xCb, yCb, nCbS, xPb, yPb, nPbW, nPbH, xNb, yNb, partIdx);

        if (!available) continue;

        // §8.5.3.2.3: parallel merge level constraint
        if ((xPb >> Log2ParMrgLevel) == (xNb >> Log2ParMrgLevel) &&
            (yPb >> Log2ParMrgLevel) == (yNb >> Log2ParMrgLevel))
            continue;

        // §8.5.3.2.3: partition-specific pruning
        // A1: if PART_Nx2N / nLx2N / nRx2N and partIdx==1 → skip (handled in is_pu_available)
        // B1: if PART_2NxN / 2NxnU / 2NxnD and partIdx==1 → skip
        if (i == 1) { // B1
            auto pm = ctx.cu_at(xPb, yPb).part_mode;
            if (partIdx == 1 && (pm == PartMode::PART_2NxN ||
                                  pm == PartMode::PART_2NxnU ||
                                  pm == PartMode::PART_2NxnD))
                continue;
        }

        PUMotionInfo mi = get_pu_motion(ctx, xNb, yNb);

        // Pruning: check if this candidate is identical to a previous one
        // §8.5.3.2.3: B1 pruned if == A1, B0 pruned if == B1,
        // A0 pruned if == A1, B2 pruned if == A1 or B1
        bool prune = false;
        if (i == 1 && avail[0]) { // B1 vs A1
            prune = (mi.mv[0].x == cands[0].mv[0].x && mi.mv[0].y == cands[0].mv[0].y &&
                     mi.mv[1].x == cands[0].mv[1].x && mi.mv[1].y == cands[0].mv[1].y &&
                     mi.ref_idx[0] == cands[0].ref_idx[0] && mi.ref_idx[1] == cands[0].ref_idx[1]);
        }
        if (i == 2 && avail[1]) { // B0 vs B1
            prune = (mi.mv[0].x == cands[1].mv[0].x && mi.mv[0].y == cands[1].mv[0].y &&
                     mi.mv[1].x == cands[1].mv[1].x && mi.mv[1].y == cands[1].mv[1].y &&
                     mi.ref_idx[0] == cands[1].ref_idx[0] && mi.ref_idx[1] == cands[1].ref_idx[1]);
        }
        if (i == 3 && avail[0]) { // A0 vs A1
            prune = (mi.mv[0].x == cands[0].mv[0].x && mi.mv[0].y == cands[0].mv[0].y &&
                     mi.mv[1].x == cands[0].mv[1].x && mi.mv[1].y == cands[0].mv[1].y &&
                     mi.ref_idx[0] == cands[0].ref_idx[0] && mi.ref_idx[1] == cands[0].ref_idx[1]);
        }
        if (i == 4) { // B2 vs A1 and B1
            if (avail[0])
                prune = (mi.mv[0].x == cands[0].mv[0].x && mi.mv[0].y == cands[0].mv[0].y &&
                         mi.mv[1].x == cands[0].mv[1].x && mi.mv[1].y == cands[0].mv[1].y &&
                         mi.ref_idx[0] == cands[0].ref_idx[0] && mi.ref_idx[1] == cands[0].ref_idx[1]);
            if (!prune && avail[1])
                prune = (mi.mv[0].x == cands[1].mv[0].x && mi.mv[0].y == cands[1].mv[0].y &&
                         mi.mv[1].x == cands[1].mv[1].x && mi.mv[1].y == cands[1].mv[1].y &&
                         mi.ref_idx[0] == cands[1].ref_idx[0] && mi.ref_idx[1] == cands[1].ref_idx[1]);
        }

        // §8.5.3.2.3: B2 only checked if < 4 candidates so far
        if (i == 4) {
            int cnt = 0;
            for (int k = 0; k < 4; k++) if (avail[k]) cnt++;
            if (cnt >= 4) continue;
        }

        if (prune) continue;

        avail[i] = true;
        cands[i].mv[0] = mi.mv[0];
        cands[i].mv[1] = mi.mv[1];
        cands[i].ref_idx[0] = mi.ref_idx[0];
        cands[i].ref_idx[1] = mi.ref_idx[1];
        cands[i].pred_flag[0] = mi.pred_flag[0];
        cands[i].pred_flag[1] = mi.pred_flag[1];
    }
}

// ============================================================
// §8.5.3.2.2 — Merge mode: build candidate list + select
// ============================================================

static PUMotionInfo derive_merge_mode(DecodingContext& ctx,
                                       int xCb, int yCb, int nCbS,
                                       int xPb, int yPb, int nPbW, int nPbH,
                                       int partIdx, int mergeIdx) {
    auto& sh = *ctx.sh;
    int MaxNumMergeCand = sh.MaxNumMergeCand;

    // Build merge candidate list
    std::vector<MergeCandidate> mergeCandList;

    // Step 1: Spatial candidates (§8.5.3.2.3)
    MergeCandidate spatialCands[5];
    bool spatialAvail[5];
    derive_spatial_merge_candidates(ctx, xCb, yCb, nCbS, xPb, yPb, nPbW, nPbH,
                                     partIdx, spatialCands, spatialAvail);

    // §8.5.3.2.2 eq 8-119: add in order A1, B1, B0, A0, B2
    for (int i = 0; i < 5 && static_cast<int>(mergeCandList.size()) < MaxNumMergeCand; i++) {
        if (spatialAvail[i])
            mergeCandList.push_back(spatialCands[i]);
    }

    // Step 2-4: Temporal candidate (§8.5.3.2.8)
    if (static_cast<int>(mergeCandList.size()) < MaxNumMergeCand &&
        sh.slice_temporal_mvp_enabled_flag) {
        MergeCandidate colCand = {};
        MV mvL0Col = {};
        bool availL0 = derive_temporal_mv(ctx, xPb, yPb, nPbW, nPbH, 0, 0, mvL0Col);
        if (availL0) {
            colCand.mv[0] = mvL0Col;
            colCand.ref_idx[0] = 0;
            colCand.pred_flag[0] = true;
        }
        if (sh.slice_type == SliceType::B) {
            MV mvL1Col = {};
            bool availL1 = derive_temporal_mv(ctx, xPb, yPb, nPbW, nPbH, 0, 1, mvL1Col);
            if (availL1) {
                colCand.mv[1] = mvL1Col;
                colCand.ref_idx[1] = 0;
                colCand.pred_flag[1] = true;
            }
        }
        if (colCand.pred_flag[0] || colCand.pred_flag[1])
            mergeCandList.push_back(colCand);
    }

    // Step 7: Combined bi-pred candidates (§8.5.3.2.4) — B slices only
    if (sh.slice_type == SliceType::B) {
        int numOrigMergeCand = static_cast<int>(mergeCandList.size());
        if (numOrigMergeCand > 1 && numOrigMergeCand < MaxNumMergeCand) {
            // Table 8-7
            static const int l0Idx[] = {0,1,0,2,1,2,0,3,1,3,2,3};
            static const int l1Idx[] = {1,0,2,0,2,1,3,0,3,1,3,2};
            for (int combIdx = 0;
                 combIdx < numOrigMergeCand * (numOrigMergeCand - 1) &&
                 static_cast<int>(mergeCandList.size()) < MaxNumMergeCand;
                 combIdx++) {
                int l0 = l0Idx[combIdx];
                int l1 = l1Idx[combIdx];
                if (l0 >= numOrigMergeCand || l1 >= numOrigMergeCand) continue;
                auto& c0 = mergeCandList[l0];
                auto& c1 = mergeCandList[l1];
                if (c0.pred_flag[0] && c1.pred_flag[1]) {
                    // §8.5.3.2.4: check different ref or different MV
                    Picture* ref0 = ctx.dpb->ref_pic_list0(c0.ref_idx[0]);
                    Picture* ref1 = ctx.dpb->ref_pic_list1(c1.ref_idx[1]);
                    if (ref0 != ref1 ||
                        c0.mv[0].x != c1.mv[1].x || c0.mv[0].y != c1.mv[1].y) {
                        MergeCandidate comb;
                        comb.mv[0] = c0.mv[0];
                        comb.ref_idx[0] = c0.ref_idx[0];
                        comb.pred_flag[0] = true;
                        comb.mv[1] = c1.mv[1];
                        comb.ref_idx[1] = c1.ref_idx[1];
                        comb.pred_flag[1] = true;
                        mergeCandList.push_back(comb);
                    }
                }
            }
        }
    }

    // Step 8: Zero motion vector candidates (§8.5.3.2.5)
    {
        int numRefIdx;
        if (sh.slice_type == SliceType::P)
            numRefIdx = sh.num_ref_idx_l0_active_minus1 + 1;
        else
            numRefIdx = std::min(static_cast<int>(sh.num_ref_idx_l0_active_minus1 + 1),
                                  static_cast<int>(sh.num_ref_idx_l1_active_minus1 + 1));
        int zeroIdx = 0;
        while (static_cast<int>(mergeCandList.size()) < MaxNumMergeCand) {
            MergeCandidate zero = {};
            zero.ref_idx[0] = (zeroIdx < numRefIdx) ? zeroIdx : 0;
            zero.pred_flag[0] = true;
            zero.mv[0] = {0, 0};
            if (sh.slice_type == SliceType::B) {
                zero.ref_idx[1] = (zeroIdx < numRefIdx) ? zeroIdx : 0;
                zero.pred_flag[1] = true;
                zero.mv[1] = {0, 0};
            }
            mergeCandList.push_back(zero);
            zeroIdx++;
        }
    }

    // Step 9: Select the merge candidate (§8.5.3.2.2 eq 8-120 to 8-125)
    auto& sel = mergeCandList[mergeIdx];
    PUMotionInfo result;
    result.mv[0] = sel.mv[0];
    result.mv[1] = sel.mv[1];
    result.ref_idx[0] = sel.ref_idx[0];
    result.ref_idx[1] = sel.ref_idx[1];
    result.pred_flag[0] = sel.pred_flag[0];
    result.pred_flag[1] = sel.pred_flag[1];

    // §8.5.3.2.2 step 10: bi-pred restriction for small PUs
    if (result.pred_flag[0] && result.pred_flag[1] && (nPbW + nPbH) == 12) {
        result.ref_idx[1] = -1;
        result.pred_flag[1] = false;
    }

    return result;
}

// ============================================================
// §8.5.3.2.6 — AMVP mode (MV prediction + MVD)
// ============================================================

static MV derive_amvp_predictor(const DecodingContext& ctx,
                                 int /*xCb*/, int /*yCb*/, int /*nCbS*/,
                                 int xPb, int yPb, int nPbW, int nPbH,
                                 int refIdxLX, int listX, int /*partIdx*/,
                                 int mvpFlag) {
    // §8.5.3.2.6: AMVP candidate derivation
    // Step 1: Spatial candidates (§8.5.3.2.7)
    // Simplified: check A0/A1 for candidate A, B0/B1/B2 for candidate B

    MV mvA = {}, mvB = {};
    bool availA = false, availB = false;

    // Left candidates: A0 (xPb-1, yPb+nPbH), A1 (xPb-1, yPb+nPbH-1)
    int leftPositions[][2] = {{xPb - 1, yPb + nPbH}, {xPb - 1, yPb + nPbH - 1}};
    for (auto& pos : leftPositions) {
        if (availA) break;
        int xNb = pos[0], yNb = pos[1];
        if (xNb < 0 || yNb < 0 ||
            xNb >= static_cast<int>(ctx.sps->pic_width_in_luma_samples) ||
            yNb >= static_cast<int>(ctx.sps->pic_height_in_luma_samples))
            continue;
        if (ctx.cu_at(xNb, yNb).pred_mode == PredMode::MODE_INTRA) continue;

        PUMotionInfo mi = get_pu_motion(ctx, xNb, yNb);
        // Check same list, same ref
        if (mi.pred_flag[listX] && mi.ref_idx[listX] == refIdxLX) {
            mvA = mi.mv[listX];
            availA = true;
        } else if (mi.pred_flag[1 - listX]) {
            // Check other list with same ref POC
            Picture* currRef = (listX == 0) ? ctx.dpb->ref_pic_list0(refIdxLX)
                                             : ctx.dpb->ref_pic_list1(refIdxLX);
            Picture* nbRef = ((1 - listX) == 0) ? ctx.dpb->ref_pic_list0(mi.ref_idx[1 - listX])
                                                  : ctx.dpb->ref_pic_list1(mi.ref_idx[1 - listX]);
            if (currRef && nbRef && currRef->poc == nbRef->poc) {
                mvA = mi.mv[1 - listX];
                availA = true;
            }
        }
    }

    // Above candidates: B0 (xPb+nPbW, yPb-1), B1 (xPb+nPbW-1, yPb-1), B2 (xPb-1, yPb-1)
    int abovePositions[][2] = {{xPb + nPbW, yPb - 1}, {xPb + nPbW - 1, yPb - 1}, {xPb - 1, yPb - 1}};
    for (auto& pos : abovePositions) {
        if (availB) break;
        int xNb = pos[0], yNb = pos[1];
        if (xNb < 0 || yNb < 0 ||
            xNb >= static_cast<int>(ctx.sps->pic_width_in_luma_samples) ||
            yNb >= static_cast<int>(ctx.sps->pic_height_in_luma_samples))
            continue;
        if (ctx.cu_at(xNb, yNb).pred_mode == PredMode::MODE_INTRA) continue;

        PUMotionInfo mi = get_pu_motion(ctx, xNb, yNb);
        if (mi.pred_flag[listX] && mi.ref_idx[listX] == refIdxLX) {
            mvB = mi.mv[listX];
            availB = true;
        } else if (mi.pred_flag[1 - listX]) {
            Picture* currRef = (listX == 0) ? ctx.dpb->ref_pic_list0(refIdxLX)
                                             : ctx.dpb->ref_pic_list1(refIdxLX);
            Picture* nbRef = ((1 - listX) == 0) ? ctx.dpb->ref_pic_list0(mi.ref_idx[1 - listX])
                                                  : ctx.dpb->ref_pic_list1(mi.ref_idx[1 - listX]);
            if (currRef && nbRef && currRef->poc == nbRef->poc) {
                mvB = mi.mv[1 - listX];
                availB = true;
            }
        }
    }

    // §8.5.3.2.6 step 2-3: Build AMVP list
    MV mvpList[2] = {};
    int numMvp = 0;

    if (availA) mvpList[numMvp++] = mvA;
    if (availB && (!availA || mvB.x != mvA.x || mvB.y != mvA.y))
        mvpList[numMvp++] = mvB;
    else if (!availA && availB)
        mvpList[numMvp++] = mvB;

    // Temporal candidate if needed
    if (numMvp < 2 && ctx.sh->slice_temporal_mvp_enabled_flag) {
        MV mvCol = {};
        if (derive_temporal_mv(ctx, xPb, yPb, nPbW, nPbH, refIdxLX, listX, mvCol))
            mvpList[numMvp++] = mvCol;
    }

    // Zero padding
    while (numMvp < 2) {
        mvpList[numMvp] = {0, 0};
        numMvp++;
    }

    // Step 4: select
    return mvpList[mvpFlag];
}

// ============================================================
// §7.3.8.6 — prediction_unit (inter) parsing
// ============================================================

void decode_prediction_unit_inter(DecodingContext& ctx,
                                   int xCb, int yCb, int nCbS,
                                   int xPb, int yPb, int nPbW, int nPbH,
                                   int partIdx) {
    auto& cabac = *ctx.cabac;
    auto& sh = *ctx.sh;
    auto& cu = ctx.cu_at(xPb, yPb);
    PUMotionInfo result;

    if (cu.pred_mode == PredMode::MODE_SKIP) {
        // §7.3.8.6: cu_skip → merge mode
        int mergeIdx = 0;
        if (sh.MaxNumMergeCand > 1)
            mergeIdx = decode_merge_idx(cabac, sh.MaxNumMergeCand);

        // Store merge_flag for rqt_root_cbf condition
        ctx.cu_at(xPb, yPb).merge_flag = true;

        result = derive_merge_mode(ctx, xCb, yCb, nCbS, xPb, yPb, nPbW, nPbH,
                                    partIdx, mergeIdx);
    } else {
        // §7.3.8.6: MODE_INTER — check merge_flag
        bool merge_flag = decode_merge_flag(cabac);
        ctx.cu_at(xPb, yPb).merge_flag = merge_flag;

        if (merge_flag) {
            int mergeIdx = 0;
            if (sh.MaxNumMergeCand > 1)
                mergeIdx = decode_merge_idx(cabac, sh.MaxNumMergeCand);

            result = derive_merge_mode(ctx, xCb, yCb, nCbS, xPb, yPb, nPbW, nPbH,
                                        partIdx, mergeIdx);
        } else {
            // AMVP mode
            int inter_pred_idc = 0;  // 0=PRED_L0, 1=PRED_L1, 2=PRED_BI
            if (sh.slice_type == SliceType::B) {
                inter_pred_idc = decode_inter_pred_idc(cabac, nPbW, nPbH,
                                                        ctx.sps->CtbLog2SizeY);
            }

            // L0
            if (inter_pred_idc != 1) {  // PRED_L0 or PRED_BI
                int refIdxL0 = 0;
                if (sh.num_ref_idx_l0_active_minus1 > 0)
                    refIdxL0 = decode_ref_idx(cabac, sh.num_ref_idx_l0_active_minus1);

                MV mvdL0 = decode_mvd(cabac);
                int mvpL0Flag = decode_mvp_flag(cabac);

                MV mvpL0 = derive_amvp_predictor(ctx, xCb, yCb, nCbS, xPb, yPb,
                                                   nPbW, nPbH, refIdxL0, 0, partIdx, mvpL0Flag);

                result.mv[0].x = mvpL0.x + mvdL0.x;
                result.mv[0].y = mvpL0.y + mvdL0.y;
                result.ref_idx[0] = refIdxL0;
                result.pred_flag[0] = true;
            }

            // L1
            if (inter_pred_idc != 0) {  // PRED_L1 or PRED_BI
                int refIdxL1 = 0;
                if (sh.num_ref_idx_l1_active_minus1 > 0)
                    refIdxL1 = decode_ref_idx(cabac, sh.num_ref_idx_l1_active_minus1);

                MV mvdL1 = {};
                if (sh.mvd_l1_zero_flag && inter_pred_idc == 2) {
                    // §7.3.8.6: MvdL1 = 0 when mvd_l1_zero_flag and PRED_BI
                } else {
                    mvdL1 = decode_mvd(cabac);
                }

                int mvpL1Flag = decode_mvp_flag(cabac);

                MV mvpL1 = derive_amvp_predictor(ctx, xCb, yCb, nCbS, xPb, yPb,
                                                   nPbW, nPbH, refIdxL1, 1, partIdx, mvpL1Flag);

                result.mv[1].x = mvpL1.x + mvdL1.x;
                result.mv[1].y = mvpL1.y + mvdL1.y;
                result.ref_idx[1] = refIdxL1;
                result.pred_flag[1] = true;
            }
        }
    }

    // Store MV info in the motion grid
    store_pu_motion(ctx, xPb, yPb, nPbW, nPbH, result);

    HEVC_LOG(INTER, "PU (%d,%d) %dx%d: L0[%d]=(%d,%d) L1[%d]=(%d,%d) pf=%d%d",
             xPb, yPb, nPbW, nPbH,
             result.ref_idx[0], result.mv[0].x, result.mv[0].y,
             result.ref_idx[1], result.mv[1].x, result.mv[1].y,
             result.pred_flag[0], result.pred_flag[1]);
}

} // namespace hevc
