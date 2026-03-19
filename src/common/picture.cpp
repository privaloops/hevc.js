#include "common/picture.h"
#include <cstdio>
#include <cstring>

namespace hevc {

void Picture::allocate(int w, int h, ChromaFormat fmt, int bd_luma, int bd_chroma) {
    pic_width_in_luma = w;
    pic_height_in_luma = h;
    bit_depth_luma = bd_luma;
    bit_depth_chroma = bd_chroma;
    chroma_format = fmt;

    int sub_w = SubWidthC(fmt);
    int sub_h = SubHeightC(fmt);

    // Luma plane
    width[0]  = w;
    height[0] = h;
    stride[0] = w;

    // Chroma planes
    if (fmt == ChromaFormat::MONOCHROME) {
        width[1] = width[2] = 0;
        height[1] = height[2] = 0;
        stride[1] = stride[2] = 0;
    } else {
        width[1]  = width[2]  = w / sub_w;
        height[1] = height[2] = h / sub_h;
        stride[1] = stride[2] = w / sub_w;
    }

    for (int c = 0; c < 3; c++) {
        if (width[c] > 0 && height[c] > 0) {
            planes[c].resize(static_cast<size_t>(stride[c]) * height[c], 0);
        } else {
            planes[c].clear();
        }
    }
}

bool Picture::write_yuv(const char* path) const {
    FILE* fp = fopen(path, "wb");
    if (!fp) return false;

    // Determine output bit depth per plane
    int bd[3] = { bit_depth_luma, bit_depth_chroma, bit_depth_chroma };

    for (int c = 0; c < 3; c++) {
        if (width[c] == 0 || height[c] == 0) continue;

        for (int y = 0; y < height[c]; y++) {
            const uint16_t* row = &planes[c][y * stride[c]];

            if (bd[c] <= 8) {
                // Write as 8-bit: truncate uint16_t to uint8_t
                for (int x = 0; x < width[c]; x++) {
                    uint8_t val = static_cast<uint8_t>(row[x]);
                    if (fwrite(&val, 1, 1, fp) != 1) {
                        fclose(fp);
                        return false;
                    }
                }
            } else {
                // Write as 16-bit little-endian
                if (fwrite(row, sizeof(uint16_t), width[c], fp) !=
                    static_cast<size_t>(width[c])) {
                    fclose(fp);
                    return false;
                }
            }
        }
    }

    fclose(fp);
    return true;
}

} // namespace hevc
