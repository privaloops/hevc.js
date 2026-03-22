#pragma once

// Deblocking filter — Spec §8.7.2
// Applied after reconstruction, before SAO.

#include "decoding/coding_tree.h"

namespace hevc {

// Apply deblocking filter to the entire picture
// §8.7.2.1: vertical edges first (all CUs), then horizontal edges (all CUs)
void apply_deblocking(DecodingContext& ctx);

} // namespace hevc
