#include "decoding/cabac.h"
#include "common/debug.h"

namespace hevc {

// §9.3.4.3.1 — Initialization of the arithmetic decoding engine
void CabacEngine::init_decoder(BitstreamReader& bs) {
    bs_ = &bs;
    ivlCurrRange_ = 510;
    ivlOffset_ = static_cast<uint16_t>(bs.read_bits(9));
    HEVC_LOG(CABAC, "init_decoder: ivlCurrRange=%d ivlOffset=%d", ivlCurrRange_, ivlOffset_);
}

// §9.3.1.1 — Context initialization
void CabacEngine::init_contexts(int sliceType, int SliceQpY, bool cabac_init_flag) {
    // Map SliceType enum (B=0, P=1, I=2) to init table index (I=0, P=1, B=2)
    int initType;
    switch (sliceType) {
        case 2: initType = 0; break; // I -> table index 0
        case 1: initType = 1; break; // P -> table index 1
        case 0: initType = 2; break; // B -> table index 2
        default: initType = 0; break;
    }

    // cabac_init_flag permutation (§9.2.1.1):
    // P slice with cabac_init_flag=1 uses B init values
    // B slice with cabac_init_flag=1 uses P init values
    if (cabac_init_flag) {
        if (initType == 1) initType = 2; // P uses B
        else if (initType == 2) initType = 1; // B uses P
    }

    int qp = Clip3(0, 51, SliceQpY);

    for (int i = 0; i < NUM_CABAC_CONTEXTS; i++) {
        uint8_t initValue = cabacInitValues[i].initValue[initType];
        int slope  = (initValue >> 4) * 5 - 45;
        int offset = ((initValue & 15) << 3) - 16;
        int preCtxState = Clip3(1, 126, ((slope * qp) >> 4) + offset);

        if (preCtxState <= 63) {
            contexts_[i].pStateIdx = static_cast<uint8_t>(63 - preCtxState);
            contexts_[i].valMps = 0;
        } else {
            contexts_[i].pStateIdx = static_cast<uint8_t>(preCtxState - 64);
            contexts_[i].valMps = 1;
        }
    }

    HEVC_LOG(CABAC, "init_contexts: sliceType=%d QP=%d cabac_init=%d initType=%d",
             sliceType, SliceQpY, cabac_init_flag, initType);
}

// §9.3.4.3.2 — Arithmetic decoding of a bin with context
int CabacEngine::decode_decision(int ctxIdx) {
    uint8_t pStateIdx = contexts_[ctxIdx].pStateIdx;
    uint8_t valMps    = contexts_[ctxIdx].valMps;

    uint8_t qRangeIdx = (ivlCurrRange_ >> 6) & 3;
    uint16_t ivlLpsRange = rangeTabLps[pStateIdx][qRangeIdx];

    ivlCurrRange_ -= ivlLpsRange;

    int binVal;
    if (ivlOffset_ >= ivlCurrRange_) {
        // LPS path
        binVal = 1 - valMps;
        ivlOffset_ -= ivlCurrRange_;
        ivlCurrRange_ = ivlLpsRange;

        if (pStateIdx == 0)
            contexts_[ctxIdx].valMps = 1 - valMps;

        contexts_[ctxIdx].pStateIdx = transIdxLps[pStateIdx];
    } else {
        // MPS path
        binVal = valMps;
        contexts_[ctxIdx].pStateIdx = transIdxMps[pStateIdx];
    }

    renormalize();
    return binVal;
}

// §9.3.4.3.4 — Bypass decoding
int CabacEngine::decode_bypass() {
    ivlOffset_ = static_cast<uint16_t>((ivlOffset_ << 1) | bs_->read_bits(1));

    if (ivlOffset_ >= ivlCurrRange_) {
        ivlOffset_ -= ivlCurrRange_;
        return 1;
    }
    return 0;
}

// §9.3.4.3.5 — Terminate decoding
int CabacEngine::decode_terminate() {
    ivlCurrRange_ -= 2;

    if (ivlOffset_ >= ivlCurrRange_) {
        return 1;
    }

    renormalize();
    return 0;
}

// Decode multiple bypass bins, MSB first
int CabacEngine::decode_bypass_bins(int numBins) {
    int value = 0;
    for (int i = 0; i < numBins; i++) {
        value = (value << 1) | decode_bypass();
    }
    return value;
}

// §9.3.4.3.3 — Renormalization
void CabacEngine::renormalize() {
    while (ivlCurrRange_ < 256) {
        ivlCurrRange_ <<= 1;
        ivlOffset_ = static_cast<uint16_t>((ivlOffset_ << 1) | bs_->read_bits(1));
    }
}

} // namespace hevc
