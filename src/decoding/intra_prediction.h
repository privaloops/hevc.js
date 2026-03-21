#pragma once

// Intra Prediction — 35 modes (Planar, DC, Angular 2-34)
// Spec §8.4.4.2

#include <cstdint>

namespace hevc {

struct DecodingContext;

// Perform intra prediction for a block
// pred_samples: output array of size (1<<log2PredSize)^2
void perform_intra_prediction(DecodingContext& ctx, int x0, int y0,
                              int log2PredSize, int cIdx, int intra_mode,
                              int16_t* pred_samples);

} // namespace hevc
