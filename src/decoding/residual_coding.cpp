#include "decoding/residual_coding.h"
#include "decoding/coding_tree.h"
#include "decoding/syntax_elements.h"
#include "decoding/cabac_tables.h"
#include "common/debug.h"

#include <cstring>

namespace hevc {

// ============================================================
// Scan order generation
// ============================================================

// Pre-computed horizontal/vertical sub-block scan tables
// (diagonal scans are in cabac_tables.h: diag_scan_2x2, 4x4, 8x8)
static const uint8_t horiz_sb_2x2[4][2] = {{0,0},{1,0},{0,1},{1,1}};
static const uint8_t horiz_sb_4x4[16][2] = {
    {0,0},{1,0},{2,0},{3,0},{0,1},{1,1},{2,1},{3,1},
    {0,2},{1,2},{2,2},{3,2},{0,3},{1,3},{2,3},{3,3}
};
static const uint8_t horiz_sb_8x8[64][2] = {
    {0,0},{1,0},{2,0},{3,0},{4,0},{5,0},{6,0},{7,0},
    {0,1},{1,1},{2,1},{3,1},{4,1},{5,1},{6,1},{7,1},
    {0,2},{1,2},{2,2},{3,2},{4,2},{5,2},{6,2},{7,2},
    {0,3},{1,3},{2,3},{3,3},{4,3},{5,3},{6,3},{7,3},
    {0,4},{1,4},{2,4},{3,4},{4,4},{5,4},{6,4},{7,4},
    {0,5},{1,5},{2,5},{3,5},{4,5},{5,5},{6,5},{7,5},
    {0,6},{1,6},{2,6},{3,6},{4,6},{5,6},{6,6},{7,6},
    {0,7},{1,7},{2,7},{3,7},{4,7},{5,7},{6,7},{7,7}
};
static const uint8_t vert_sb_2x2[4][2] = {{0,0},{0,1},{1,0},{1,1}};
static const uint8_t vert_sb_4x4[16][2] = {
    {0,0},{0,1},{0,2},{0,3},{1,0},{1,1},{1,2},{1,3},
    {2,0},{2,1},{2,2},{2,3},{3,0},{3,1},{3,2},{3,3}
};
static const uint8_t vert_sb_8x8[64][2] = {
    {0,0},{0,1},{0,2},{0,3},{0,4},{0,5},{0,6},{0,7},
    {1,0},{1,1},{1,2},{1,3},{1,4},{1,5},{1,6},{1,7},
    {2,0},{2,1},{2,2},{2,3},{2,4},{2,5},{2,6},{2,7},
    {3,0},{3,1},{3,2},{3,3},{3,4},{3,5},{3,6},{3,7},
    {4,0},{4,1},{4,2},{4,3},{4,4},{4,5},{4,6},{4,7},
    {5,0},{5,1},{5,2},{5,3},{5,4},{5,5},{5,6},{5,7},
    {6,0},{6,1},{6,2},{6,3},{6,4},{6,5},{6,6},{6,7},
    {7,0},{7,1},{7,2},{7,3},{7,4},{7,5},{7,6},{7,7}
};
static const uint8_t diag_scan_1x1[1][2] = {{0,0}};

// Return pointer to sub-block scan table (no copy)
static const uint8_t (*get_sub_block_scan(int log2TrafoSize, int scanIdx))[2] {
    int sbSize = (1 << log2TrafoSize) >> 2;
    if (sbSize <= 0) sbSize = 1;

    if (scanIdx == 1) {
        if (sbSize <= 1) return diag_scan_1x1;
        if (sbSize == 2) return horiz_sb_2x2;
        if (sbSize == 4) return horiz_sb_4x4;
        return horiz_sb_8x8;
    } else if (scanIdx == 2) {
        if (sbSize <= 1) return diag_scan_1x1;
        if (sbSize == 2) return vert_sb_2x2;
        if (sbSize == 4) return vert_sb_4x4;
        return vert_sb_8x8;
    } else {
        if (sbSize <= 1) return diag_scan_1x1;
        if (sbSize == 2) return diag_scan_2x2;
        if (sbSize == 4) return diag_scan_4x4;
        return diag_scan_8x8;
    }
}

// Return pointer to coefficient scan table (no copy)
static const uint8_t (*get_coeff_scan(int scanIdx))[2] {
    if (scanIdx == 1) return horiz_scan_4x4;
    if (scanIdx == 2) return vert_scan_4x4;
    return diag_scan_4x4;
}

// Derive scan index from intra mode and TU size (§8.4.4.2.1)
// §7.4.9.11 — scanIdx derivation
// scanIdx: 0=up-right diagonal, 1=horizontal, 2=vertical
static int derive_scan_idx(int predModeIntra, bool modeDependent) {
    if (modeDependent) {
        if (predModeIntra >= 6 && predModeIntra <= 14)
            return 2; // vertical scan
        if (predModeIntra >= 22 && predModeIntra <= 30)
            return 1; // horizontal scan
    }
    return 0; // diagonal
}

// ============================================================
// sig_coeff_flag context derivation (§9.3.4.2.8)
// This is the most complex context derivation in HEVC CABAC
// ============================================================

// §9.3.4.2.5 — Derivation process of ctxInc for sig_coeff_flag
// Transcription directe des eq 9-40 à 9-55 de la spec.
// Note: eq 9-55 donne ctxInc = 27 + sigCtx pour chroma, mais les init values
// Table 9-29 sont organisées avec 28 luma contexts (offset chroma = 28).
// On utilise 28 (layout HM) pour matcher les init values. Voir LEARNINGS.md.
static int derive_sig_coeff_flag_ctx(int cIdx, int log2TrafoSize,
                                      int xC, int yC,
                                      int scanIdx,
                                      const int coded_sub_block_flag[],
                                      int numSbPerSide,
                                      bool transformSkipOrBypass) {
    // Table 9-50 — spec has only 15 entries (i=0..14), position 15 is never accessed
    // (it's always lastScanPos which is implicitly significant). 99 = sentinel.
    static const int ctxIdxMap[16] = {
        0, 1, 4, 5, 2, 3, 4, 5, 6, 6, 8, 8, 7, 7, 8, 99
    };

    int sigCtx;

    // eq 9-40: transform_skip_context_enabled + (transform_skip || bypass)
    if (transformSkipOrBypass) {
        sigCtx = (cIdx == 0) ? 42 : 16;                          // eq 9-40
    }
    // eq 9-41: 4x4 TU
    else if (log2TrafoSize == 2) {
        sigCtx = ctxIdxMap[(yC << 2) + xC];                      // eq 9-41
    }
    // eq 9-42: DC position
    else if (xC + yC == 0) {
        sigCtx = 0;                                               // eq 9-42
    }
    // eq 9-43 to 9-53: non-4x4, non-DC
    else {
        int xS = xC >> 2;                                        // sub-block location
        int yS = yC >> 2;

        int prevCsbf = 0;                                        // eq 9-43, 9-44
        if (xS < numSbPerSide - 1)
            prevCsbf += coded_sub_block_flag[yS * numSbPerSide + (xS + 1)];
        if (yS < numSbPerSide - 1)
            prevCsbf += coded_sub_block_flag[(yS + 1) * numSbPerSide + xS] << 1;

        int xP = xC & 3;                                        // inner sub-block location
        int yP = yC & 3;

        switch (prevCsbf) {                                      // eq 9-45 to 9-48
            case 0:  sigCtx = (xP + yP == 0) ? 2 : (xP + yP < 3) ? 1 : 0; break;
            case 1:  sigCtx = (yP == 0) ? 2 : (yP == 1) ? 1 : 0; break;
            case 2:  sigCtx = (xP == 0) ? 2 : (xP == 1) ? 1 : 0; break;
            default: sigCtx = 2; break;
        }

        if (cIdx == 0) {
            if ((xS + yS) > 0)
                sigCtx += 3;                                     // eq 9-49
            if (log2TrafoSize == 3)
                sigCtx += (scanIdx == 0) ? 9 : 15;              // eq 9-50
            else
                sigCtx += 21;                                    // eq 9-51
        } else {
            if (log2TrafoSize == 3)
                sigCtx += 9;                                     // eq 9-52
            else
                sigCtx += 12;                                    // eq 9-53
        }
    }

    // eq 9-54, 9-55: ctxInc derivation (27 = chroma offset per spec eq 9-55)
    if (cIdx == 0)
        return sigCtx;                                            // eq 9-54
    else
        return 27 + sigCtx;                                       // eq 9-55
}

// ============================================================
// residual_coding (§7.3.8.11)
// ============================================================

void decode_residual_coding(DecodingContext& ctx, int x0, int y0,
                            int log2TrafoSize, int cIdx,
                            int16_t* coefficients) {
    auto& cabac = *ctx.cabac;
    int trSize = 1 << log2TrafoSize;
    std::memset(coefficients, 0, sizeof(int16_t) * trSize * trSize);

    // §7.4.9.11 — scanIdx derivation
    auto& cu = ctx.cu_at(x0, y0);
    int scanIdx = 0;
    if (cu.pred_mode == PredMode::MODE_INTRA &&
        (log2TrafoSize == 2 ||
         (log2TrafoSize == 3 && cIdx == 0) ||
         (log2TrafoSize == 3 && ctx.sps->ChromaArrayType == 3))) {
        int predModeIntra = (cIdx == 0) ? ctx.intra_mode_at(x0, y0)
                                        : ctx.chroma_mode_at(x0, y0);
        scanIdx = derive_scan_idx(predModeIntra, true);
    }

    [[maybe_unused]] int bins_start = cabac.bin_count();
    HEVC_LOG(CABAC, "residual_coding (%d,%d) log2=%d cIdx=%d scanIdx=%d bins_at=%d",
             x0, y0, log2TrafoSize, cIdx, scanIdx, bins_start);

    // Last significant coefficient position
    // §7.3.8.11: For vertical scan, swap width/height for context derivation,
    // then swap the decoded X/Y coordinates back
    int log2W = log2TrafoSize, log2H = log2TrafoSize;
    if (scanIdx == 2) std::swap(log2W, log2H); // vertical scan: swap dimensions

    int lastSigCoeffXPrefix = decode_last_sig_coeff_prefix(cabac, CTX_LAST_SIG_COEFF_X,
                                                            cIdx, log2W);
    int lastSigCoeffYPrefix = decode_last_sig_coeff_prefix(cabac, CTX_LAST_SIG_COEFF_Y,
                                                            cIdx, log2H);

    int LastSignificantCoeffX = lastSigCoeffXPrefix;
    int LastSignificantCoeffY = lastSigCoeffYPrefix;

    if (lastSigCoeffXPrefix > 3) {
        int suffix = decode_last_sig_coeff_suffix(cabac, lastSigCoeffXPrefix);
        int base = ((lastSigCoeffXPrefix >> 1) - 1);
        LastSignificantCoeffX = (1 << base) * ((lastSigCoeffXPrefix & 1) + 2) + suffix;
    }
    if (lastSigCoeffYPrefix > 3) {
        int suffix = decode_last_sig_coeff_suffix(cabac, lastSigCoeffYPrefix);
        int base = ((lastSigCoeffYPrefix >> 1) - 1);
        LastSignificantCoeffY = (1 << base) * ((lastSigCoeffYPrefix & 1) + 2) + suffix;
    }

    if (scanIdx == 2) std::swap(LastSignificantCoeffX, LastSignificantCoeffY);

    HEVC_LOG(CABAC, "  lastSig=(%d,%d)", LastSignificantCoeffX, LastSignificantCoeffY);

    // Scan tables
    int numSbPerSide = trSize >> 2;
    if (numSbPerSide < 1) numSbPerSide = 1;
    int numSubBlocks = numSbPerSide * numSbPerSide;

    const uint8_t (*sbScan)[2] = get_sub_block_scan(log2TrafoSize, scanIdx);
    const uint8_t (*coeffScan)[2] = get_coeff_scan(scanIdx);

    // Find last sub-block and last scan position
    int lastSubBlock = numSubBlocks - 1;
    int lastScanPos = 16;

    // Search for last position
    while (true) {
        if (lastScanPos == 0) {
            lastScanPos = 16;
            lastSubBlock--;
        }
        lastScanPos--;
        int xS = sbScan[lastSubBlock][0];
        int yS = sbScan[lastSubBlock][1];
        int xC = (xS << 2) + coeffScan[lastScanPos][0];
        int yC = (yS << 2) + coeffScan[lastScanPos][1];
        if (xC == LastSignificantCoeffX && yC == LastSignificantCoeffY)
            break;
    }

    // coded_sub_block_flag array
    int coded_sub_block_flag[64] = {};
    // Set the sub-block containing the last sig coeff
    coded_sub_block_flag[sbScan[lastSubBlock][1] * numSbPerSide +
                         sbScan[lastSubBlock][0]] = 1;
    // DC sub-block is always implicitly 1 if there's any coefficient
    coded_sub_block_flag[0] = 1;

    // Cross-sub-block state for ctxSet derivation (§9.3.4.2.6)
    // prevGreater1Ctx tracks greater1Ctx from the last sub-block where
    // coeff_abs_level_greater1_flag was decoded (skipping empty sub-blocks).
    // ctxSet++ when prevGreater1Ctx == 0 (previous sub-block had a coeff > 1).
    int prevGreater1Ctx = 0;       // 0 = no previous sub-block yet
    bool hasGreater1History = false;

    // Process sub-blocks from last to first
    for (int i = lastSubBlock; i >= 0; i--) {
        int xS = sbScan[i][0];
        int yS = sbScan[i][1];
        int sbIdx = yS * numSbPerSide + xS;

        bool inferSbDcSigCoeffFlag = false;

        // Read coded_sub_block_flag for non-first, non-last sub-blocks
        if (i < lastSubBlock && i > 0) {
            // Context: depends on right and below neighbour csbf
            int csbfRight = 0, csbfBelow = 0;
            if (xS + 1 < numSbPerSide)
                csbfRight = coded_sub_block_flag[yS * numSbPerSide + (xS + 1)];
            if (yS + 1 < numSbPerSide)
                csbfBelow = coded_sub_block_flag[(yS + 1) * numSbPerSide + xS];

            int ctxInc = (cIdx > 0) ? 2 : 0;
            ctxInc += (csbfRight || csbfBelow) ? 1 : 0;

            coded_sub_block_flag[sbIdx] = decode_coded_sub_block_flag(cabac, ctxInc);
            inferSbDcSigCoeffFlag = true;
        }

        // sig_coeff_flag array for this sub-block (scan order)
        int sig_coeff_flag[16] = {};

        int firstN = (i == lastSubBlock) ? lastScanPos - 1 : 15;

        for (int n = firstN; n >= 0; n--) {
            int xC = (xS << 2) + coeffScan[n][0];
            int yC = (yS << 2) + coeffScan[n][1];

            if (coded_sub_block_flag[sbIdx] &&
                (n > 0 || !inferSbDcSigCoeffFlag)) {
                int sigCtx = derive_sig_coeff_flag_ctx(
                    cIdx, log2TrafoSize, xC, yC, scanIdx,
                    coded_sub_block_flag, numSbPerSide, false);
                sig_coeff_flag[n] = decode_sig_coeff_flag(cabac, sigCtx);
                if (sig_coeff_flag[n])
                    inferSbDcSigCoeffFlag = false;
            } else if (coded_sub_block_flag[sbIdx] && n == 0 && inferSbDcSigCoeffFlag) {
                // Infer DC coefficient as significant
                sig_coeff_flag[0] = 1;
            }
        }

        // For the last sub-block, the last scan position is always significant
        if (i == lastSubBlock) {
            sig_coeff_flag[lastScanPos] = 1;
        }

        // Decode coefficient levels
        int firstSigScanPos = 16;
        int lastSigScanPos = -1;
        int numGreater1Flag = 0;
        int lastGreater1ScanPos = -1;

        int coeff_abs_greater1[16] = {};

        // Context set selection (§9.3.4.2.6)
        int ctxSet = (i > 0 && cIdx == 0) ? 2 : 0;

        // §9.3.4.2.6: increment ctxSet when the previous sub-block
        // (where greater1 was decoded) had greater1Ctx == 0
        if (hasGreater1History && prevGreater1Ctx == 0)
            ctxSet++;

        int greater1Ctx = 1;

        for (int n = 15; n >= 0; n--) {
            if (sig_coeff_flag[n]) {
                if (numGreater1Flag < 8) {
                    coeff_abs_greater1[n] = decode_coeff_abs_level_greater1_flag(
                        cabac, ctxSet, greater1Ctx, cIdx);
                    numGreater1Flag++;

                    if (coeff_abs_greater1[n]) {
                        if (lastGreater1ScanPos == -1)
                            lastGreater1ScanPos = n;
                        greater1Ctx = 0;
                    } else if (greater1Ctx > 0 && greater1Ctx < 3) {
                        greater1Ctx++;
                    }
                }

                if (lastSigScanPos == -1) lastSigScanPos = n;
                firstSigScanPos = n;
            }
        }

        // Sign hidden condition (§7.3.8.11)
        auto& pps = *ctx.pps;
        auto& cu = ctx.cu_at(x0, y0);
        bool signHidden = false;
        if (!cu.cu_transquant_bypass) {
            signHidden = pps.sign_data_hiding_enabled_flag &&
                         (lastSigScanPos - firstSigScanPos > 3);
        }

        // coeff_abs_level_greater2_flag
        int coeff_abs_greater2_flag[16] = {};
        if (lastGreater1ScanPos != -1) {
            coeff_abs_greater2_flag[lastGreater1ScanPos] =
                decode_coeff_abs_level_greater2_flag(cabac, ctxSet, cIdx);
        }

        // §7.3.8.11 + SPS RExt §7.4.3.2.2: alignment only when
        // cabac_bypass_alignment_enabled_flag is 1 (RExt profiles).
        // Main profile infers this flag as 0 — no alignment performed.
        if (ctx.sps->cabac_bypass_alignment_enabled_flag)
            cabac.align_bypass();

        // Sign flags — read for all sig coeffs (except hidden one)
        int coeff_sign_flag[16] = {};
        for (int n = 15; n >= 0; n--) {
            if (sig_coeff_flag[n]) {
                if (!signHidden || n != firstSigScanPos) {
                    coeff_sign_flag[n] = decode_coeff_sign_flag(cabac);
                }
            }
        }

        // coeff_abs_level_remaining + reconstruction (spec §7.3.8.11 final loop)
        int numSigCoeff = 0;
        int sumAbsLevel = 0;
        int cRiceParam = 0;

        for (int n = 15; n >= 0; n--) {
            if (sig_coeff_flag[n]) {
                int xC = (xS << 2) + coeffScan[n][0];
                int yC = (yS << 2) + coeffScan[n][1];

                int baseLevel = 1 + coeff_abs_greater1[n] +
                                coeff_abs_greater2_flag[n];

                // Spec: read remaining when baseLevel equals max possible
                // max = 3 if numSigCoeff<8 and n==lastGreater1ScanPos
                // max = 2 if numSigCoeff<8 and n!=lastGreater1ScanPos
                // max = 1 if numSigCoeff>=8
                int maxBase;
                if (numSigCoeff < 8) {
                    maxBase = (n == lastGreater1ScanPos) ? 3 : 2;
                } else {
                    maxBase = 1;
                }

                int coeff_remaining = 0;
                if (baseLevel == maxBase) {
                    coeff_remaining = decode_coeff_abs_level_remaining(
                        cabac, cRiceParam);

                    // Update cRiceParam (§9.3.3.11)
                    int absLevel = baseLevel + coeff_remaining;
                    if (absLevel > 3 * (1 << cRiceParam) && cRiceParam < 4)
                        cRiceParam++;
                }

                int absLevel = baseLevel + coeff_remaining;

                // Sign
                int sign;
                if (signHidden && n == firstSigScanPos) {
                    // §7.4.9.11: parity of sum INCLUDING current level
                    sign = ((sumAbsLevel + absLevel) & 1) ? -1 : 1;
                } else {
                    sign = coeff_sign_flag[n] ? -1 : 1;
                }

                coefficients[yC * trSize + xC] = static_cast<int16_t>(sign * absLevel);

                sumAbsLevel += absLevel;
                numSigCoeff++;
            }
        }

        // Save cross-sub-block state for next iteration (§9.3.4.2.6)
        // Only update when greater1 flags were actually decoded
        if (numGreater1Flag > 0) {
            prevGreater1Ctx = greater1Ctx;
            hasGreater1History = true;
        }
    }

    // Count total nonzero
    [[maybe_unused]] int totalNonzero = 0;
    for (int k = 0; k < trSize * trSize; k++)
        if (coefficients[k] != 0) totalNonzero++;

    HEVC_LOG(CABAC, "residual_coding done: %d bins consumed, %d nonzero coeffs",
             cabac.bin_count() - bins_start, totalNonzero);
}

} // namespace hevc
