// Sample Adaptive Offset — Spec §8.7.3
// Transcription directe de la spec ITU-T H.265 v8 (08/2021)

#include "filters/sao.h"
#include "common/types.h"

#include <vector>

namespace hevc {

// §8.7.3.2: EO class direction offsets
// Class 0 (H):    (-1, 0), (1, 0)
// Class 1 (V):    (0, -1), (0, 1)
// Class 2 (D135): (-1, -1), (1, 1)
// Class 3 (D45):  (1, -1), (-1, 1)
static const int eo_dx[4][2] = {{-1, 1}, {0, 0}, {-1, 1}, {1, -1}};
static const int eo_dy[4][2] = {{0, 0}, {-1, 1}, {-1, 1}, {-1, 1}};

void apply_sao(DecodingContext& ctx) {
    auto& sps = *ctx.sps;
    auto* pic = ctx.pic;

    if (!sps.sample_adaptive_offset_enabled_flag) return;

    int ctbSize = 1 << sps.CtbLog2SizeY;
    int subW = sps.SubWidthC;
    int subH = sps.SubHeightC;
    int numComp = (sps.ChromaArrayType != 0) ? 3 : 1;

    // Quick check: skip entirely if no CTU has SAO enabled
    bool anySao = false;
    for (int i = 0; i < sps.PicSizeInCtbsY && !anySao; i++) {
        for (int c = 0; c < numComp; c++) {
            if (ctx.sao_params[i].sao_type_idx[c] != 0) { anySao = true; break; }
        }
    }
    if (!anySao) return;

    // §8.7.3.1: SAO operates on a copy of the deblocked picture
    // We need the pre-SAO samples for EO neighbor comparisons
    std::vector<uint16_t> origPlane[3];
    for (int c = 0; c < numComp; c++) {
        origPlane[c].assign(pic->planes[c].begin(), pic->planes[c].end());
    }

    // Process each CTU
    for (int ry = 0; ry < sps.PicHeightInCtbsY; ry++) {
        for (int rx = 0; rx < sps.PicWidthInCtbsY; rx++) {
            auto& sao = ctx.sao_params[ry * ctx.sao_params_stride + rx];

            for (int cIdx = 0; cIdx < numComp; cIdx++) {
                if (sao.sao_type_idx[cIdx] == 0) continue;

                // Check slice SAO flags
                // Note: we apply SAO globally; per-slice flag check would need
                // slice index per CTU. For single-slice pictures this is correct.
                // Multi-slice: the SAO params are already set to type=0 during
                // parsing if the slice flag was off.

                int bitDepth = (cIdx == 0) ? sps.BitDepthY : sps.BitDepthC;
                int maxVal = (1 << bitDepth) - 1;
                bool pcmFilterDisabled = sps.pcm_loop_filter_disabled_flag;

                // CTB dimensions in this component
                int nCtbSw, nCtbSh;
                if (cIdx == 0) {
                    nCtbSw = ctbSize;
                    nCtbSh = ctbSize;
                } else {
                    nCtbSw = ctbSize / subW;
                    nCtbSh = ctbSize / subH;
                }

                int xCtb = rx * nCtbSw;
                int yCtb = ry * nCtbSh;
                int compW = pic->width[cIdx];
                int compH = pic->height[cIdx];
                int stride = pic->stride[cIdx];

                if (sao.sao_type_idx[cIdx] == 2) {
                    // Edge offset — §8.7.3.2
                    int eoClass = sao.sao_eo_class[cIdx];

                    for (int j = 0; j < nCtbSh; j++) {
                        int ySj = yCtb + j;
                        if (ySj >= compH) break;
                        for (int i = 0; i < nCtbSw; i++) {
                            int xSi = xCtb + i;
                            if (xSi >= compW) break;

                            // §8.7.3.2: skip PCM and transquant_bypass
                            int xY = (cIdx == 0) ? xSi : xSi * subW;
                            int yY = (cIdx == 0) ? ySj : ySj * subH;
                            auto& cu = ctx.cu_at(xY, yY);
                            if ((pcmFilterDisabled && cu.is_pcm) || cu.cu_transquant_bypass)
                                continue;

                            // Neighbor positions
                            int xN1 = xSi + eo_dx[eoClass][0];
                            int yN1 = ySj + eo_dy[eoClass][0];
                            int xN2 = xSi + eo_dx[eoClass][1];
                            int yN2 = ySj + eo_dy[eoClass][1];

                            // §8.7.3.2: out-of-picture neighbors → no modification
                            if (xN1 < 0 || xN1 >= compW || yN1 < 0 || yN1 >= compH) continue;
                            if (xN2 < 0 || xN2 >= compW || yN2 < 0 || yN2 >= compH) continue;

                            int c_val = origPlane[cIdx][ySj * stride + xSi];
                            int a = origPlane[cIdx][yN1 * stride + xN1];
                            int b = origPlane[cIdx][yN2 * stride + xN2];

                            // §8.7.3.2: edge index categorization
                            int edgeIdx;
                            int signC_A = (c_val < a) ? -1 : (c_val > a) ? 1 : 0;
                            int signC_B = (c_val < b) ? -1 : (c_val > b) ? 1 : 0;
                            // edgeIdx = 2 + sign(c-a) + sign(c-b)
                            edgeIdx = 2 + signC_A + signC_B;
                            // Map: 0=valley(both<), 1=concave(one<), 2=flat, 3=convex(one>), 4=peak(both>)

                            int offset = sao.sao_offset_val[cIdx][edgeIdx];
                            if (offset != 0) {
                                pic->planes[cIdx][ySj * stride + xSi] =
                                    static_cast<uint16_t>(Clip3(0, maxVal, c_val + offset));
                            }
                        }
                    }
                } else {
                    // Band offset — §8.7.3.3
                    int bandShift = bitDepth - 5;
                    int bandPos = sao.sao_band_position[cIdx];

                    for (int j = 0; j < nCtbSh; j++) {
                        int ySj = yCtb + j;
                        if (ySj >= compH) break;
                        for (int i = 0; i < nCtbSw; i++) {
                            int xSi = xCtb + i;
                            if (xSi >= compW) break;

                            int xY = (cIdx == 0) ? xSi : xSi * subW;
                            int yY = (cIdx == 0) ? ySj : ySj * subH;
                            auto& cu = ctx.cu_at(xY, yY);
                            if ((pcmFilterDisabled && cu.is_pcm) || cu.cu_transquant_bypass)
                                continue;

                            int sample = origPlane[cIdx][ySj * stride + xSi];
                            int band = sample >> bandShift;
                            int bandIdx = band - bandPos;
                            if (bandIdx >= 0 && bandIdx < 4) {
                                int offset = sao.sao_offset_val[cIdx][bandIdx];
                                if (offset != 0) {
                                    pic->planes[cIdx][ySj * stride + xSi] =
                                        static_cast<uint16_t>(Clip3(0, maxVal, sample + offset));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace hevc
