#pragma once

// CABAC Engine — Spec §9.3
// Arithmetic decoding: decode_decision, decode_bypass, decode_terminate
// Context initialization: init_contexts with all Tables 9-5 to 9-31
//
// Hot path functions (decode_decision, decode_bypass, decode_bypass_bins,
// renormalize) are defined inline in this header to enable cross-TU inlining,
// which is critical for WASM builds without LTO.

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

    // §9.3.4.3.2 — Arithmetic decoding of a bin with context (inline hot path)
    inline int decode_decision(int ctxIdx) {
        auto& ctx = contexts_[ctxIdx];
        uint8_t pStateIdx = ctx.pStateIdx;
        uint8_t valMps    = ctx.valMps;

        uint16_t ivlLpsRange = rangeTabLps[pStateIdx][(ivlCurrRange_ >> 6) & 3];
        ivlCurrRange_ -= ivlLpsRange;

        int binVal;
        if (ivlOffset_ >= ivlCurrRange_) {
            // LPS path
            binVal = 1 - valMps;
            ivlOffset_ -= ivlCurrRange_;
            ivlCurrRange_ = ivlLpsRange;
            if (pStateIdx == 0)
                ctx.valMps = 1 - valMps;
            ctx.pStateIdx = transIdxLps[pStateIdx];
        } else {
            // MPS path
            binVal = valMps;
            ctx.pStateIdx = transIdxMps[pStateIdx];
        }

        renormalize();

#ifdef HEVC_DEBUG
        bin_count_++;
#endif
        return binVal;
    }

    // §9.3.4.3.4 — Bypass decoding (inline, branchless)
    inline int decode_bypass() {
        ivlOffset_ = static_cast<uint16_t>((ivlOffset_ << 1) | bs_->read_bit_fast());

        // Branchless: avoid unpredictable branch on 50/50 bypass bins
        int val = (ivlOffset_ >= ivlCurrRange_);
        ivlOffset_ -= ivlCurrRange_ & static_cast<uint16_t>(-val);

#ifdef HEVC_DEBUG
        bin_count_++;
#endif
        return val;
    }

    // Decode multiple bypass bins, MSB first (batched bit read)
    inline int decode_bypass_bins(int numBins) {
        // Read all bits at once — eliminates N-1 refill checks
        uint32_t bits = bs_->read_bits_safe(numBins);

        int value = 0;
        for (int i = numBins - 1; i >= 0; i--) {
            ivlOffset_ = static_cast<uint16_t>((ivlOffset_ << 1) | ((bits >> i) & 1));
            int val = (ivlOffset_ >= ivlCurrRange_);
            ivlOffset_ -= ivlCurrRange_ & static_cast<uint16_t>(-val);
            value = (value << 1) | val;
        }

#ifdef HEVC_DEBUG
        bin_count_ += numBins;
#endif
        return value;
    }

    // §9.3.4.3.5 — Terminate decoding (cold path, stays out-of-line)
    int decode_terminate();

    // §9.3.4.3.6: Alignment prior to bypass decoding of coeff_sign_flag
    // and coeff_abs_level_remaining. Sets ivlCurrRange to 256.
    void align_bypass() { ivlCurrRange_ = 256; }

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

    // Bin counter for debugging — compiled out in release
#ifdef HEVC_DEBUG
    int bin_count() const { return bin_count_; }
    void reset_bin_count() { bin_count_ = 0; }
#else
    int bin_count() const { return 0; }
    void reset_bin_count() {}
#endif
    uint16_t dbg_range() const { return ivlCurrRange_; }
    uint16_t dbg_offset() const { return ivlOffset_; }

private:
    // §9.3.4.3.3 — Renormalization (batched clz + bulk read)
    inline void renormalize() {
        if (ivlCurrRange_ >= 256) return;
        int shift = __builtin_clz(static_cast<unsigned>(ivlCurrRange_)) - 23;
        ivlCurrRange_ <<= shift;
        uint32_t bits = bs_->read_bits_safe(shift);
        ivlOffset_ = static_cast<uint16_t>((ivlOffset_ << shift) | bits);
    }

    CabacContext contexts_[NUM_CABAC_CONTEXTS] = {};
    uint16_t ivlCurrRange_ = 0;
    uint16_t ivlOffset_ = 0;
    BitstreamReader* bs_ = nullptr;
#ifdef HEVC_DEBUG
    int bin_count_ = 0;
#endif
};

} // namespace hevc
