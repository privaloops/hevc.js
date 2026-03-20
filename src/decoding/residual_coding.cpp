#include "decoding/residual_coding.h"
#include "decoding/coding_tree.h"
#include "decoding/syntax_elements.h"
#include "decoding/cabac_tables.h"
#include "common/debug.h"

#include <cstring>
#include <algorithm>

namespace hevc {

// ============================================================
// Scan order generation
// ============================================================

// Get scan order for sub-block positions within a TU
// scanIdx: 0=diagonal, 1=horizontal, 2=vertical
static void get_sub_block_scan(int log2TrafoSize, int scanIdx,
                                int numSubBlocks, int sbScan[][2]) {
    int sbSize = (1 << log2TrafoSize) >> 2; // number of 4x4 sub-blocks per side
    if (sbSize <= 0) sbSize = 1;

    if (scanIdx == 1) {
        // Horizontal
        for (int i = 0; i < numSubBlocks; i++) {
            sbScan[i][0] = i % sbSize;
            sbScan[i][1] = i / sbSize;
        }
    } else if (scanIdx == 2) {
        // Vertical
        for (int i = 0; i < numSubBlocks; i++) {
            sbScan[i][0] = i / sbSize;
            sbScan[i][1] = i % sbSize;
        }
    } else {
        // Diagonal
        if (sbSize == 1) {
            sbScan[0][0] = 0; sbScan[0][1] = 0;
        } else if (sbSize == 2) {
            for (int i = 0; i < 4; i++) {
                sbScan[i][0] = diag_scan_2x2[i][0];
                sbScan[i][1] = diag_scan_2x2[i][1];
            }
        } else if (sbSize == 4) {
            for (int i = 0; i < 16; i++) {
                sbScan[i][0] = diag_scan_4x4[i][0];
                sbScan[i][1] = diag_scan_4x4[i][1];
            }
        } else {
            // 8x8 sub-blocks (32x32 TU)
            for (int i = 0; i < 64; i++) {
                sbScan[i][0] = diag_scan_8x8[i][0];
                sbScan[i][1] = diag_scan_8x8[i][1];
            }
        }
    }
}

// Get coefficient scan within a 4x4 sub-block
static void get_coeff_scan(int scanIdx, int coeffScan[][2]) {
    if (scanIdx == 1) {
        for (int i = 0; i < 16; i++) {
            coeffScan[i][0] = horiz_scan_4x4[i][0];
            coeffScan[i][1] = horiz_scan_4x4[i][1];
        }
    } else if (scanIdx == 2) {
        for (int i = 0; i < 16; i++) {
            coeffScan[i][0] = vert_scan_4x4[i][0];
            coeffScan[i][1] = vert_scan_4x4[i][1];
        }
    } else {
        for (int i = 0; i < 16; i++) {
            coeffScan[i][0] = diag_scan_4x4[i][0];
            coeffScan[i][1] = diag_scan_4x4[i][1];
        }
    }
}

// Derive scan index from intra mode and TU size (§8.4.4.2.1)
static int derive_scan_idx(int log2TrafoSize, int intra_mode, int cIdx) {
    if (log2TrafoSize == 2 || (log2TrafoSize == 3 && cIdx > 0)) {
        // 4x4 luma or 8x8 chroma (which maps to 4x4 in 4:2:0)
        if (intra_mode >= 6 && intra_mode <= 14)
            return 2; // vertical scan
        if (intra_mode >= 22 && intra_mode <= 30)
            return 1; // horizontal scan
    }
    return 0; // diagonal
}

// ============================================================
// sig_coeff_flag context derivation (§9.3.4.2.8)
// This is the most complex context derivation in HEVC CABAC
// ============================================================

static int derive_sig_coeff_flag_ctx(int cIdx, int log2TrafoSize,
                                      int xC, int yC, int xS, int yS,
                                      int scanIdx,
                                      const int coded_sub_block_flag[],
                                      int numSbPerSide, int /*prevCsbf*/) {
    int sigCtx;

    if (log2TrafoSize == 2) {
        // 4x4 block: use position-based context
        static const int ctxIdxMap4x4[16] = {
            0, 1, 4, 5, 2, 3, 4, 5, 6, 6, 8, 8, 7, 7, 8, 8
        };
        int xP = xC;
        int yP = yC;
        if (scanIdx == 2) std::swap(xP, yP);
        sigCtx = ctxIdxMap4x4[yP * 4 + xP];

        if (cIdx > 0) {
            sigCtx += 12;
        }
    } else {
        // Larger blocks: context depends on coded_sub_block_flag of neighbours
        int xPosInSb = xC & 3;
        int yPosInSb = yC & 3;

        // Neighbour coded_sub_block_flag availability
        int csbfRight = 0, csbfBelow = 0;
        if (xS + 1 < numSbPerSide) {
            int idx = (yS) * numSbPerSide + (xS + 1);
            csbfRight = coded_sub_block_flag[idx];
        }
        if (yS + 1 < numSbPerSide) {
            int idx = (yS + 1) * numSbPerSide + (xS);
            csbfBelow = coded_sub_block_flag[idx];
        }

        int cnt = csbfRight + csbfBelow;

        if (cIdx == 0) {
            // Luma
            if (xPosInSb + yPosInSb == 0) {
                sigCtx = cnt > 0 ? 2 : 0;
            } else if (xPosInSb + yPosInSb < 3) {
                sigCtx = cnt > 1 ? 2 : (cnt == 1 ? 1 : 0);
                sigCtx += 3;
            } else {
                sigCtx = cnt > 0 ? 2 : 0;
                sigCtx += 6;
            }

            if (log2TrafoSize == 3) {
                sigCtx += 9;
            } else if (log2TrafoSize == 4) {
                sigCtx += 21;
            } else {
                sigCtx += 30; // log2TrafoSize == 5
            }
        } else {
            // Chroma
            if (xPosInSb + yPosInSb == 0) {
                sigCtx = cnt > 0 ? 2 : 0;
            } else {
                sigCtx = cnt > 0 ? 2 : 0;
                sigCtx += 2;
            }

            sigCtx += 12; // chroma offset after 4x4 chroma contexts
            if (log2TrafoSize >= 4) {
                // Merge larger chroma into single set
                sigCtx = std::min(sigCtx, 15 + 12);
            }
        }
    }

    return sigCtx;
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

    // Derive scan index
    int intra_mode = ctx.intra_mode_at(x0, y0);
    int scanIdx = derive_scan_idx(log2TrafoSize, intra_mode, cIdx);

    int bins_start = cabac.bin_count();
    HEVC_LOG(CABAC, "residual_coding (%d,%d) log2=%d cIdx=%d scanIdx=%d bins_at=%d",
             x0, y0, log2TrafoSize, cIdx, scanIdx, bins_start);

    // Last significant coefficient position
    int lastSigCoeffXPrefix = decode_last_sig_coeff_prefix(cabac, CTX_LAST_SIG_COEFF_X,
                                                            cIdx, log2TrafoSize);
    int lastSigCoeffYPrefix = decode_last_sig_coeff_prefix(cabac, CTX_LAST_SIG_COEFF_Y,
                                                            cIdx, log2TrafoSize);

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

    HEVC_LOG(CABAC, "  lastSig=(%d,%d)", LastSignificantCoeffX, LastSignificantCoeffY);

    // Scan tables
    int numSbPerSide = trSize >> 2;
    if (numSbPerSide < 1) numSbPerSide = 1;
    int numSubBlocks = numSbPerSide * numSbPerSide;

    int sbScan[64][2];
    int coeffScan[16][2];
    get_sub_block_scan(log2TrafoSize, scanIdx, numSubBlocks, sbScan);
    get_coeff_scan(scanIdx, coeffScan);

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
                    cIdx, log2TrafoSize, xC, yC, xS, yS, scanIdx,
                    coded_sub_block_flag, numSbPerSide, 0);
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

        // Context set selection (§9.3.4.2.7)
        int ctxSet = (i > 0 && cIdx == 0) ? 2 : 0;
        if (i == lastSubBlock && cIdx == 0) ctxSet = 0;
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
                    sign = (sumAbsLevel & 1) ? -1 : 1;
                } else {
                    sign = coeff_sign_flag[n] ? -1 : 1;
                }

                coefficients[yC * trSize + xC] = static_cast<int16_t>(sign * absLevel);
                sumAbsLevel += absLevel;
                numSigCoeff++;
            }
        }
    }

    // Count total nonzero
    int totalNonzero = 0;
    for (int k = 0; k < trSize * trSize; k++)
        if (coefficients[k] != 0) totalNonzero++;

    HEVC_LOG(CABAC, "residual_coding done: %d bins consumed, %d nonzero coeffs",
             cabac.bin_count() - bins_start, totalNonzero);
}

} // namespace hevc
