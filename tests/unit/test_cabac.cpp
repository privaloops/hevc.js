#include <gtest/gtest.h>
#include <vector>

#include "decoding/cabac.h"

using namespace hevc;

// Helper: create a BitstreamReader from raw bytes
static BitstreamReader make_bs(const std::vector<uint8_t>& data) {
    return BitstreamReader(data.data(), data.size());
}

// ============================================================
// Context initialization tests
// ============================================================

TEST(CabacContextInit, InitValueFormula) {
    // Test the init formula manually for a known context
    // split_cu_flag[0], I-slice, initValue=139, QP=26
    // slope = (139 >> 4) * 5 - 45 = 8 * 5 - 45 = -5
    // offset = ((139 & 15) << 3) - 16 = (11 << 3) - 16 = 88 - 16 = 72
    // preCtxState = Clip3(1, 126, ((-5 * 26) >> 4) + 72)
    //            = Clip3(1, 126, (-130 >> 4) + 72) = Clip3(1, 126, -9 + 72) = 63
    // preCtxState=63 <= 63 => pStateIdx=63-63=0, valMps=0

    CabacEngine engine;
    engine.init_contexts(2, 26, false); // I-slice (SliceType::I=2), QP=26

    auto ctx = engine.context(CTX_SPLIT_CU_FLAG);
    EXPECT_EQ(ctx.pStateIdx, 0);
    EXPECT_EQ(ctx.valMps, 0);
}

TEST(CabacContextInit, AllContextsInRange) {
    // Verify all contexts are initialized with valid ranges
    CabacEngine engine;

    for (int st = 0; st < 3; st++) {
        for (int qp = 0; qp <= 51; qp++) {
            engine.init_contexts(st, qp, false);
            for (int i = 0; i < NUM_CABAC_CONTEXTS; i++) {
                EXPECT_LT(engine.context(i).pStateIdx, 64)
                    << "sliceType=" << st << " QP=" << qp << " ctxIdx=" << i;
                EXPECT_LE(engine.context(i).valMps, 1)
                    << "sliceType=" << st << " QP=" << qp << " ctxIdx=" << i;
            }
        }
    }
}

TEST(CabacContextInit, CabacInitFlagPermutation) {
    // P-slice with cabac_init_flag=1 should use B-slice init values
    CabacEngine engine_p_normal, engine_p_swapped, engine_b_normal;

    // SliceType enum: B=0, P=1, I=2
    engine_p_normal.init_contexts(1, 26, false);  // P, no swap
    engine_p_swapped.init_contexts(1, 26, true);  // P, swap -> uses B
    engine_b_normal.init_contexts(0, 26, false);  // B, no swap

    // The swapped P should match normal B
    for (int i = 0; i < NUM_CABAC_CONTEXTS; i++) {
        EXPECT_EQ(engine_p_swapped.context(i).pStateIdx,
                  engine_b_normal.context(i).pStateIdx) << "ctxIdx=" << i;
        EXPECT_EQ(engine_p_swapped.context(i).valMps,
                  engine_b_normal.context(i).valMps) << "ctxIdx=" << i;
    }
}

// ============================================================
// Arithmetic decoder tests
// ============================================================

TEST(CabacDecode, DecodeTerminate_EndOfSlice) {
    // Construct a bitstream where decode_terminate returns 1
    // ivlCurrRange=510, ivlOffset from 9 bits
    // decode_terminate: ivlCurrRange -= 2 = 508
    // If ivlOffset >= 508, terminate returns 1
    // So we need ivlOffset >= 508 => 9-bit value >= 508
    // 508 = 0b111111100 => let's use 0b111111111 = 511
    // Bytes: 0xFF, 0x80 (first 9 bits = 111111110 = 510... let's be precise)

    // ivlOffset = read_bits(9). To get 510: binary 111111110
    // byte 0: 0xFF = 11111111, byte 1: MSB = 0 => 0b111111110 = 510
    // decode_terminate: range -= 2 -> 508. offset=510 >= 508 -> return 1
    std::vector<uint8_t> data = { 0xFF, 0x00 };
    auto bs = make_bs(data);

    CabacEngine engine;
    engine.init_decoder(bs);
    EXPECT_EQ(engine.decode_terminate(), 1);
}

TEST(CabacDecode, DecodeTerminate_NotEnd) {
    // ivlOffset must be < ivlCurrRange - 2 = 508
    // Use ivlOffset = 0 (9 bits of zero)
    std::vector<uint8_t> data = { 0x00, 0x00, 0x00, 0x00 };
    auto bs = make_bs(data);

    CabacEngine engine;
    engine.init_decoder(bs);
    EXPECT_EQ(engine.decode_terminate(), 0);
}

TEST(CabacDecode, DecodeBypass_Zero) {
    // ivlOffset = 0 initially, read one bit = 0
    // new ivlOffset = (0 << 1) | 0 = 0 < ivlCurrRange -> return 0
    std::vector<uint8_t> data = { 0x00, 0x00, 0x00, 0x00 };
    auto bs = make_bs(data);

    CabacEngine engine;
    engine.init_decoder(bs);
    EXPECT_EQ(engine.decode_bypass(), 0);
}

TEST(CabacDecode, DecodeBypassBins_MultipleZeros) {
    std::vector<uint8_t> data = { 0x00, 0x00, 0x00, 0x00 };
    auto bs = make_bs(data);

    CabacEngine engine;
    engine.init_decoder(bs);
    EXPECT_EQ(engine.decode_bypass_bins(8), 0);
}

TEST(CabacDecode, DecodeDecision_MPS) {
    // With ivlOffset = 0 (very low), we should always get MPS path
    std::vector<uint8_t> data = { 0x00, 0x00, 0x00, 0x00, 0x00 };
    auto bs = make_bs(data);

    CabacEngine engine;
    engine.init_contexts(0, 26, false);
    engine.init_decoder(bs);

    // Decode a decision — with offset=0, we always hit MPS
    int ctxIdx = CTX_SPLIT_CU_FLAG;
    int bin = engine.decode_decision(ctxIdx);
    EXPECT_EQ(bin, engine.context(ctxIdx).valMps);
}

TEST(CabacDecode, SaveRestoreContexts) {
    CabacEngine engine;
    engine.init_contexts(0, 26, false);

    CabacContext saved[NUM_CABAC_CONTEXTS];
    engine.save_contexts(saved);

    // Modify a context
    engine.context(0).pStateIdx = 42;
    engine.context(0).valMps = 0;

    // Restore
    engine.load_contexts(saved);
    EXPECT_NE(engine.context(0).pStateIdx, 42);
}
