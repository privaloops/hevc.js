#pragma once

// Residual coding — Spec §7.3.8.11
// Parses transform coefficients from CABAC bitstream

#include <cstdint>

namespace hevc {

struct DecodingContext;

// Parse residual coefficients for one TU block
// coefficients: output array of size (1<<log2TrafoSize)^2
void decode_residual_coding(DecodingContext& ctx, int x0, int y0,
                            int log2TrafoSize, int cIdx,
                            int16_t* coefficients);

} // namespace hevc
