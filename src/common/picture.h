#pragma once

#include <cstdint>
#include <vector>

#include "common/types.h"

namespace hevc {

// Picture buffer — planar YUV layout (AD-002)
// Spec ref: §6.1 (source, coded, decoded picture formats)
struct Picture {
    // Plane data
    std::vector<uint16_t> planes[3];  // 0=Y, 1=Cb, 2=Cr
    int width[3]  = {};               // width per plane (in samples)
    int height[3] = {};               // height per plane (in samples)
    int stride[3] = {};               // stride per plane (in samples)

    // Picture properties
    int pic_width_in_luma = 0;
    int pic_height_in_luma = 0;
    int bit_depth_luma = 8;
    int bit_depth_chroma = 8;
    ChromaFormat chroma_format = ChromaFormat::YUV420;

    // Picture Order Count
    int32_t poc = 0;

    // Reference status
    bool used_for_short_term_ref = false;
    bool used_for_long_term_ref = false;
    bool needed_for_output = false;

    // Allocate planes based on dimensions and chroma format
    void allocate(int width, int height, ChromaFormat fmt, int bd_luma, int bd_chroma);

    // Get sample at position (x, y) in plane c
    uint16_t& sample(int c, int x, int y) {
        return planes[c][y * stride[c] + x];
    }
    uint16_t sample(int c, int x, int y) const {
        return planes[c][y * stride[c] + x];
    }

    // Write to raw YUV file
    bool write_yuv(const char* path) const;

    // Is this picture a reference?
    bool is_reference() const {
        return used_for_short_term_ref || used_for_long_term_ref;
    }
};

} // namespace hevc
