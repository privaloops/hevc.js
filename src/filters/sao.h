#pragma once

// Sample Adaptive Offset — Spec §8.7.3
// Applied after deblocking filter.

#include "decoding/coding_tree.h"

namespace hevc {

// Apply SAO to the entire picture (post-deblocking)
// §8.7.3.1: CTB-by-CTB, using pre-deblocked neighbor samples for EO comparisons
void apply_sao(DecodingContext& ctx);

} // namespace hevc
