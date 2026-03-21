#pragma once

// CABAC Engine — Spec §9.3
// Arithmetic decoding: decode_decision, decode_bypass, decode_terminate
// Context initialization: init_contexts with all Tables 9-5 to 9-31

#include <cstdint>
#include <cstring>

#include "bitstream/bitstream_reader.h"
#include "common/types.h"
#include "decoding/cabac_tables.h"

namespace hevc {

// Single CABAC context (AD-005)
struct CabacContext {
    uint8_t pStateIdx;
    uint8_t valMps;
};

// CABAC arithmetic decoder engine
class CabacEngine {
public:
    CabacEngine() = default;

    // Initialize the arithmetic decoder (§9.3.4.3.1)
    // Must be called at the start of each independent slice segment
    void init_decoder(BitstreamReader& bs);

    // Initialize all contexts for a given slice type and QP (§9.3.1.1)
    // sliceType: 0=I, 1=P, 2=B
    // cabac_init_flag: permutes P/B tables when true
    void init_contexts(int sliceType, int SliceQpY, bool cabac_init_flag);

    // Core decoding operations
    int decode_decision(int ctxIdx);   // §9.3.4.3.2
    int decode_bypass();               // §9.3.4.3.4
    int decode_terminate();            // §9.3.4.3.5

    // Multi-bin bypass decoding (convenience)
    int decode_bypass_bins(int numBins);

    // Context access
    CabacContext& context(int ctxIdx) { return contexts_[ctxIdx]; }
    const CabacContext& context(int ctxIdx) const { return contexts_[ctxIdx]; }

    // Save/restore state (for WPP)
    void save_contexts(CabacContext* dst) const {
        std::memcpy(dst, contexts_, sizeof(contexts_));
    }
    void load_contexts(const CabacContext* src) {
        std::memcpy(contexts_, src, sizeof(contexts_));
    }

    BitstreamReader* bitstream() { return bs_; }

    // Bin counter for debugging
    int bin_count() const { return bin_count_; }
    void reset_bin_count() { bin_count_ = 0; }
    uint16_t dbg_range() const { return ivlCurrRange_; }
    uint16_t dbg_offset() const { return ivlOffset_; }

private:
    void renormalize();  // §9.3.4.3.3

    CabacContext contexts_[NUM_CABAC_CONTEXTS] = {};
    uint16_t ivlCurrRange_ = 0;
    uint16_t ivlOffset_ = 0;
    BitstreamReader* bs_ = nullptr;
    int bin_count_ = 0;
};

} // namespace hevc
