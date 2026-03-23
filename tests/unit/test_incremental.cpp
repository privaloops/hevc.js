#include <gtest/gtest.h>
#include <fstream>
#include <vector>
#include <numeric>

#include "decoding/decoder.h"

using namespace hevc;

// Helper: read file into byte vector
static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return {std::istreambuf_iterator<char>(f), {}};
}

// Helper: find NAL unit boundaries (start codes 00 00 01 or 00 00 00 01)
static std::vector<size_t> find_nal_starts(const std::vector<uint8_t>& data) {
    std::vector<size_t> starts;
    for (size_t i = 0; i + 2 < data.size(); i++) {
        if (data[i] == 0 && data[i+1] == 0) {
            if (data[i+2] == 1) {
                starts.push_back(i);
                i += 2;
            } else if (i + 3 < data.size() && data[i+2] == 0 && data[i+3] == 1) {
                starts.push_back(i);
                i += 3;
            }
        }
    }
    return starts;
}

// Helper: get NAL type from start code position
static int get_nal_type(const std::vector<uint8_t>& data, size_t start) {
    // Skip start code (3 or 4 bytes)
    size_t off = start;
    if (data[off] == 0 && data[off+1] == 0 && data[off+2] == 0 && data[off+3] == 1)
        off += 4;
    else
        off += 3;
    if (off >= data.size()) return -1;
    return (data[off] >> 1) & 0x3F;
}

// ============================================================
// Test: feed/drain produces same frames as batch decode
// ============================================================

TEST(IncrementalDecode, FeedDrainMatchesBatch) {
    std::string path = std::string(FIXTURES_DIR) + "/full_qcif_10f.265";
    auto data = read_file(path);
    ASSERT_FALSE(data.empty()) << "Cannot read " << path;

    // --- Batch decode ---
    Decoder batch;
    auto status = batch.decode(data.data(), data.size());
    ASSERT_EQ(status, DecodeStatus::OK);
    auto batch_pics = batch.output_pictures();
    ASSERT_GT(batch_pics.size(), 0u);

    // --- Incremental decode ---
    // Split at the first I-frame NAL after at least 1 VCL NAL
    auto nal_starts = find_nal_starts(data);
    ASSERT_GE(nal_starts.size(), 5u);

    // Find a split point: after the first few NALs (VPS/SPS/PPS + first slice)
    // Split at the boundary between 2 access units
    // We look for a VCL NAL with first_slice_segment_in_pic_flag
    size_t split = data.size() / 2;  // default: split in the middle
    for (size_t i = 1; i < nal_starts.size(); i++) {
        int nal_type = get_nal_type(data, nal_starts[i]);
        // VCL NAL types are 0-31
        if (nal_type >= 0 && nal_type <= 31) {
            // Check first_slice_segment_in_pic_flag (first bit after NAL header)
            size_t hdr_off = nal_starts[i];
            if (data[hdr_off] == 0 && data[hdr_off+1] == 0 && data[hdr_off+2] == 0)
                hdr_off += 4;
            else
                hdr_off += 3;
            hdr_off += 2;  // skip 2-byte NAL header
            if (hdr_off < data.size()) {
                bool first_slice = (data[hdr_off] >> 7) & 1;
                if (first_slice && i > 3) {  // not the very first slice
                    split = nal_starts[i];
                    break;
                }
            }
        }
    }

    // Feed in 2 chunks
    Decoder inc;
    std::vector<Picture*> all_inc_pics;

    // Chunk 1
    status = inc.feed(data.data(), split);
    ASSERT_EQ(status, DecodeStatus::OK);
    auto drained1 = inc.drain();

    // Chunk 2
    status = inc.feed(data.data() + split, data.size() - split);
    ASSERT_EQ(status, DecodeStatus::OK);
    auto drained2 = inc.drain();

    // Flush remaining
    auto flushed = inc.flush();

    all_inc_pics.insert(all_inc_pics.end(), drained1.begin(), drained1.end());
    all_inc_pics.insert(all_inc_pics.end(), drained2.begin(), drained2.end());
    all_inc_pics.insert(all_inc_pics.end(), flushed.begin(), flushed.end());

    // Verify: same number of frames
    EXPECT_EQ(all_inc_pics.size(), batch_pics.size())
        << "Incremental produced " << all_inc_pics.size()
        << " frames vs batch " << batch_pics.size()
        << " (drained1=" << drained1.size()
        << " drained2=" << drained2.size()
        << " flushed=" << flushed.size() << ")";

    // Verify: output order is consistent (CVS then POC increasing)
    for (size_t i = 1; i < all_inc_pics.size(); i++) {
        bool order_ok = false;
        if (all_inc_pics[i]->cvs_id > all_inc_pics[i-1]->cvs_id) {
            order_ok = true;  // new CVS
        } else if (all_inc_pics[i]->cvs_id == all_inc_pics[i-1]->cvs_id) {
            order_ok = (all_inc_pics[i]->poc > all_inc_pics[i-1]->poc);
        }
        EXPECT_TRUE(order_ok)
            << "Output order violated at frame " << i
            << ": pic[" << i-1 << "]=(cvs=" << all_inc_pics[i-1]->cvs_id
            << ",poc=" << all_inc_pics[i-1]->poc << ")"
            << " pic[" << i << "]=(cvs=" << all_inc_pics[i]->cvs_id
            << ",poc=" << all_inc_pics[i]->poc << ")";
    }

    // Verify: same (cvs_id, poc) pairs as batch
    size_t min_count = std::min(all_inc_pics.size(), batch_pics.size());
    for (size_t i = 0; i < min_count; i++) {
        EXPECT_EQ(all_inc_pics[i]->cvs_id, batch_pics[i]->cvs_id)
            << "CVS mismatch at frame " << i;
        EXPECT_EQ(all_inc_pics[i]->poc, batch_pics[i]->poc)
            << "POC mismatch at frame " << i;
    }

    // Verify: PIXEL-PERFECT match between batch and incremental
    for (size_t i = 0; i < min_count; i++) {
        auto* bp = batch_pics[i];
        auto* ip = all_inc_pics[i];
        ASSERT_EQ(bp->planes[0].size(), ip->planes[0].size())
            << "Y plane size mismatch at frame " << i;
        bool y_match = (bp->planes[0] == ip->planes[0]);
        bool cb_match = (bp->planes[1] == ip->planes[1]);
        bool cr_match = (bp->planes[2] == ip->planes[2]);
        EXPECT_TRUE(y_match) << "Y plane pixel mismatch at frame " << i
            << " (poc=" << bp->poc << ")";
        EXPECT_TRUE(cb_match) << "Cb plane pixel mismatch at frame " << i;
        EXPECT_TRUE(cr_match) << "Cr plane pixel mismatch at frame " << i;
        if (!y_match) {
            // Find first differing pixel
            for (size_t j = 0; j < bp->planes[0].size(); j++) {
                if (bp->planes[0][j] != ip->planes[0][j]) {
                    int row = j / bp->stride[0];
                    int col = j % bp->stride[0];
                    FAIL() << "First Y diff at pixel (" << col << "," << row
                           << ") batch=" << bp->planes[0][j]
                           << " inc=" << ip->planes[0][j];
                }
            }
        }
    }
}

// ============================================================
// Test: DPB stays bounded during incremental decode
// ============================================================

TEST(IncrementalDecode, DPBBounded) {
    std::string path = std::string(FIXTURES_DIR) + "/full_qcif_10f.265";
    auto data = read_file(path);
    ASSERT_FALSE(data.empty());

    // Split into individual NAL units and feed one by one
    auto nal_starts = find_nal_starts(data);
    ASSERT_GE(nal_starts.size(), 3u);

    Decoder dec;
    size_t max_dpb = 0;
    size_t total_drained = 0;

    for (size_t i = 0; i < nal_starts.size(); i++) {
        size_t start = nal_starts[i];
        size_t end = (i + 1 < nal_starts.size()) ? nal_starts[i+1] : data.size();
        size_t len = end - start;

        auto status = dec.feed(data.data() + start, len);
        ASSERT_EQ(status, DecodeStatus::OK) << "Feed failed at NAL " << i;

        auto drained = dec.drain();
        total_drained += drained.size();

        size_t dpb_size = dec.dpb().pictures().size();
        if (dpb_size > max_dpb) max_dpb = dpb_size;
    }

    auto flushed = dec.flush();
    total_drained += flushed.size();

    // DPB should stay bounded (Main profile max DPB = 16)
    EXPECT_LE(max_dpb, 16u) << "DPB grew too large: " << max_dpb;

    // Should have output all frames
    EXPECT_EQ(total_drained, 10u)
        << "Expected 10 frames, got " << total_drained;
}
