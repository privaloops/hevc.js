#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <optional>

#include "common/types.h"
#include "common/picture.h"
#include "syntax/vps.h"
#include "syntax/sps.h"
#include "syntax/pps.h"

namespace hevc {

// Decode result codes (AD-004: no exceptions)
enum class DecodeStatus : int {
    OK             =  0,
    NEED_MORE_DATA =  1,  // Feed more data to continue
    FRAME_READY    =  2,  // A decoded frame is available via get_frame()
    SKIP           =  3,  // Feature not yet implemented, graceful skip

    ERR_BITSTREAM  = -1,  // Malformed bitstream
    ERR_UNSUPPORTED = -2, // Unsupported feature (profile, level, etc.)
    ERR_INTERNAL   = -3,  // Internal error
};

// Decoder — stateful HEVC decoding engine
//
// Designed for WASM integration (Phase 8):
// - Incremental feeding: call feed() with chunks of data as they arrive
// - No exceptions (AD-004)
// - Reusable internal buffers (avoids per-NAL allocation)
// - Clean C++ API that maps directly to a C API wrapper
//
// Usage:
//   Decoder dec;
//   dec.feed(data, size);           // feed Annex B byte stream
//   while (auto* pic = dec.get_frame()) {
//       pic->write_yuv("output.yuv");
//   }
//   dec.flush();
class Decoder {
public:
    Decoder() = default;

    // Feed raw byte stream data (Annex B format)
    // Can be called multiple times with chunks (streaming support)
    DecodeStatus feed(const uint8_t* data, size_t size);

    // Get next decoded frame in display order (nullptr if none ready)
    const Picture* get_frame();

    // Signal end of stream, flush remaining frames
    DecodeStatus flush();

    // Stream info (available after first SPS is parsed)
    bool has_info() const;
    int width() const;
    int height() const;
    int bit_depth_luma() const;
    int bit_depth_chroma() const;
    ChromaFormat chroma_format() const;

private:
    // Parameter sets — stored by value, indexed by ID (AD-003)
    std::array<std::optional<VPS>, 16> vps_{};
    std::array<std::optional<SPS>, 16> sps_{};
    std::array<std::optional<PPS>, 64> pps_{};

    // Active parameter set IDs (-1 = none)
    int active_sps_id_ = -1;

    // Input buffer for incremental feeding (Annex B stream accumulator)
    std::vector<uint8_t> pending_data_;

    // Reusable RBSP extraction buffer (avoids malloc per NAL)
    std::vector<uint8_t> rbsp_buffer_;

    // Output queue (decoded frames in display order)
    std::vector<Picture> output_queue_;
    size_t output_read_index_ = 0;

    // Accessors for active parameter sets
    const SPS* active_sps() const;
};

} // namespace hevc
