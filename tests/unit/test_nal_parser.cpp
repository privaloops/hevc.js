#include <gtest/gtest.h>
#include <fstream>
#include "bitstream/nal_unit.h"
#include "bitstream/bitstream_reader.h"

using namespace hevc;

// ============================================================
// Helper: build a byte stream with start codes + NAL data
// ============================================================

// Build a minimal NAL unit header (2 bytes)
// forbidden_zero_bit(1) | nal_unit_type(6) | nuh_layer_id(6) | nuh_temporal_id_plus1(3)
static std::pair<uint8_t, uint8_t> make_nal_header(uint8_t type, uint8_t layer_id = 0, uint8_t temporal_id_plus1 = 1) {
    uint16_t word = 0;
    // forbidden_zero_bit = 0 (bit 15)
    word |= (static_cast<uint16_t>(type) & 0x3F) << 9;
    word |= (static_cast<uint16_t>(layer_id) & 0x3F) << 3;
    word |= (temporal_id_plus1 & 0x07);
    return {static_cast<uint8_t>(word >> 8), static_cast<uint8_t>(word & 0xFF)};
}

// Append a 4-byte start code + NAL header + payload to a buffer
static void append_nal(std::vector<uint8_t>& buf, uint8_t type,
                       const std::vector<uint8_t>& payload = {},
                       uint8_t layer_id = 0, uint8_t temporal_id_plus1 = 1) {
    // 4-byte start code
    buf.push_back(0x00);
    buf.push_back(0x00);
    buf.push_back(0x00);
    buf.push_back(0x01);

    auto [h0, h1] = make_nal_header(type, layer_id, temporal_id_plus1);
    buf.push_back(h0);
    buf.push_back(h1);

    for (auto b : payload) {
        buf.push_back(b);
    }
}

// ============================================================
// 2.1 — Start Code Detection
// ============================================================

TEST(NalParser, ParseSingleNal) {
    std::vector<uint8_t> stream;
    // VPS (type 32) with some dummy payload
    append_nal(stream, 32, {0xAA, 0xBB, 0xCC});

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());

    ASSERT_EQ(nals.size(), 1u);
    EXPECT_EQ(nals[0].header.nal_unit_type, NalUnitType::VPS_NUT);
    EXPECT_EQ(nals[0].size, 5u);  // 2 header + 3 payload
}

TEST(NalParser, ParseMultipleNals) {
    std::vector<uint8_t> stream;
    append_nal(stream, 32, {0xAA});  // VPS
    append_nal(stream, 33, {0xBB});  // SPS
    append_nal(stream, 34, {0xCC});  // PPS

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());

    ASSERT_EQ(nals.size(), 3u);
    EXPECT_EQ(nals[0].header.nal_unit_type, NalUnitType::VPS_NUT);
    EXPECT_EQ(nals[1].header.nal_unit_type, NalUnitType::SPS_NUT);
    EXPECT_EQ(nals[2].header.nal_unit_type, NalUnitType::PPS_NUT);
}

TEST(NalParser, Parse3ByteStartCode) {
    // Use 3-byte start code (0x000001) instead of 4-byte
    std::vector<uint8_t> stream = {0x00, 0x00, 0x01};
    auto [h0, h1] = make_nal_header(32);
    stream.push_back(h0);
    stream.push_back(h1);
    stream.push_back(0xDD);

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());

    ASSERT_EQ(nals.size(), 1u);
    EXPECT_EQ(nals[0].header.nal_unit_type, NalUnitType::VPS_NUT);
}

TEST(NalParser, ParseMixed3And4ByteStartCodes) {
    std::vector<uint8_t> stream;

    // First NAL with 4-byte start code
    append_nal(stream, 32, {0xAA});

    // Second NAL with 3-byte start code
    stream.push_back(0x00);
    stream.push_back(0x00);
    stream.push_back(0x01);
    auto [h0, h1] = make_nal_header(33);
    stream.push_back(h0);
    stream.push_back(h1);
    stream.push_back(0xBB);

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());

    ASSERT_EQ(nals.size(), 2u);
    EXPECT_EQ(nals[0].header.nal_unit_type, NalUnitType::VPS_NUT);
    EXPECT_EQ(nals[1].header.nal_unit_type, NalUnitType::SPS_NUT);
}

TEST(NalParser, SkipLeadingZeros) {
    // Leading zeros before first start code
    std::vector<uint8_t> stream = {0x00, 0x00, 0x00, 0x00, 0x00};
    append_nal(stream, 32, {0xAA});

    // Remove the first 4-byte SC that append_nal added, we already have zeros
    // Actually, the leading zeros + start code is fine — parser should handle it
    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());

    ASSERT_EQ(nals.size(), 1u);
    EXPECT_EQ(nals[0].header.nal_unit_type, NalUnitType::VPS_NUT);
}

TEST(NalParser, EmptyStream) {
    NalParser parser;
    auto nals = parser.parse(nullptr, 0);
    EXPECT_TRUE(nals.empty());
}

TEST(NalParser, NoStartCode) {
    uint8_t data[] = {0xAA, 0xBB, 0xCC};
    NalParser parser;
    auto nals = parser.parse(data, sizeof(data));
    EXPECT_TRUE(nals.empty());
}

// ============================================================
// 2.2 — Emulation Prevention Byte Removal in Pipeline
// ============================================================

TEST(NalParser, EmulationPreventionInPipeline) {
    std::vector<uint8_t> stream;
    // NAL with EP bytes in payload: 00 00 03 01 should become 00 00 01 in RBSP
    append_nal(stream, 32, {0x00, 0x00, 0x03, 0x01, 0xFF});

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());

    ASSERT_EQ(nals.size(), 1u);
    // RBSP should have EP byte removed: 00 00 01 FF
    ASSERT_EQ(nals[0].rbsp.size(), 4u);
    EXPECT_EQ(nals[0].rbsp[0], 0x00);
    EXPECT_EQ(nals[0].rbsp[1], 0x00);
    EXPECT_EQ(nals[0].rbsp[2], 0x01);
    EXPECT_EQ(nals[0].rbsp[3], 0xFF);
}

// ============================================================
// 2.3 — NAL Unit Header Parsing
// ============================================================

TEST(NalParser, HeaderParsing_TypeAndIds) {
    std::vector<uint8_t> stream;
    // IDR_W_RADL (type 19), layer 0, temporal_id_plus1 = 1
    append_nal(stream, 19, {0x80}, 0, 1);

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());

    ASSERT_EQ(nals.size(), 1u);
    EXPECT_EQ(nals[0].header.nal_unit_type, NalUnitType::IDR_W_RADL);
    EXPECT_EQ(nals[0].header.nuh_layer_id, 0);
    EXPECT_EQ(nals[0].header.nuh_temporal_id_plus1, 1);
    EXPECT_EQ(nals[0].header.TemporalId(), 0);
}

TEST(NalParser, HeaderParsing_TemporalId) {
    std::vector<uint8_t> stream;
    // TRAIL_R (type 1), layer 0, temporal_id_plus1 = 3 -> TemporalId = 2
    append_nal(stream, 1, {0x80}, 0, 3);

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());

    ASSERT_EQ(nals.size(), 1u);
    EXPECT_EQ(nals[0].header.nal_unit_type, NalUnitType::TRAIL_R);
    EXPECT_EQ(nals[0].header.nuh_temporal_id_plus1, 3);
    EXPECT_EQ(nals[0].header.TemporalId(), 2);
}

TEST(NalParser, HeaderParsing_AllVCLTypes) {
    // Verify is_vcl works for boundary values
    EXPECT_TRUE(is_vcl(NalUnitType::TRAIL_N));    // 0
    EXPECT_TRUE(is_vcl(NalUnitType::RSV_VCL_R15)); // 15
    EXPECT_TRUE(is_vcl(NalUnitType::CRA_NUT));    // 21
    EXPECT_FALSE(is_vcl(NalUnitType::VPS_NUT));    // 32
    EXPECT_FALSE(is_vcl(NalUnitType::PREFIX_SEI)); // 39
}

TEST(NalParser, HeaderParsing_IRAPHelpers) {
    EXPECT_TRUE(is_irap(NalUnitType::BLA_W_LP));    // 16
    EXPECT_TRUE(is_irap(NalUnitType::IDR_W_RADL));  // 19
    EXPECT_TRUE(is_irap(NalUnitType::IDR_N_LP));     // 20
    EXPECT_TRUE(is_irap(NalUnitType::CRA_NUT));      // 21
    EXPECT_FALSE(is_irap(NalUnitType::TRAIL_R));     // 1
    EXPECT_FALSE(is_irap(NalUnitType::VPS_NUT));     // 32

    EXPECT_TRUE(is_idr(NalUnitType::IDR_W_RADL));
    EXPECT_TRUE(is_idr(NalUnitType::IDR_N_LP));
    EXPECT_FALSE(is_idr(NalUnitType::CRA_NUT));

    EXPECT_TRUE(is_bla(NalUnitType::BLA_W_LP));
    EXPECT_TRUE(is_bla(NalUnitType::BLA_W_RADL));
    EXPECT_TRUE(is_bla(NalUnitType::BLA_N_LP));
    EXPECT_FALSE(is_bla(NalUnitType::IDR_W_RADL));

    EXPECT_TRUE(is_rasl(NalUnitType::RASL_N));
    EXPECT_TRUE(is_rasl(NalUnitType::RASL_R));
    EXPECT_FALSE(is_rasl(NalUnitType::RADL_N));

    EXPECT_TRUE(is_radl(NalUnitType::RADL_N));
    EXPECT_TRUE(is_radl(NalUnitType::RADL_R));
    EXPECT_FALSE(is_radl(NalUnitType::RASL_N));
}

// ============================================================
// 2.4 — Exp-Golomb edge cases (additional vectors)
// ============================================================

TEST(BitstreamReader, ReadUE_LargeValue) {
    // ue(14) = 0001111 (3 leading zeros, then 1111)
    //        = 000 1111 -> (1<<3) - 1 + 7 = 14
    uint8_t data[] = {0b00011110, 0x00};
    BitstreamReader bs(data, sizeof(data));
    EXPECT_EQ(bs.read_ue(), 14u);
}

TEST(BitstreamReader, ReadUE_MaxReasonable) {
    // ue(30) = 00000 11111 (5 leading zeros, then 11111 = 0b11111 = 31)
    //        = (1<<5) - 1 + 31 = 31 + 31 = 62... wait
    // Actually ue(30): codeNum=30 = (1<<5) - 1 + suffix
    // (1<<5)-1 = 31, so 30 < 31 means fewer leading zeros
    // ue(30) = 0000 11111 (4 leading zeros, 1+4 bits read = 11111)
    // (1<<4) - 1 + suffix = 15 + suffix = 30 -> suffix = 15 = 0b1111
    // code = 0000 1 1111 = 9 bits
    uint8_t data[] = {0b00001111, 0b10000000};
    BitstreamReader bs(data, sizeof(data));
    EXPECT_EQ(bs.read_ue(), 30u);
}

TEST(BitstreamReader, ReadSE_Negative3) {
    // se(-3) = ue(6) = 00111 = 2 leading zeros, suffix = 11
    // (1<<2)-1+3 = 3+3 = 6, code=6, se = -(6/2) = -3
    uint8_t data[] = {0b00111000};
    BitstreamReader bs(data, sizeof(data));
    EXPECT_EQ(bs.read_se(), -3);
}

// ============================================================
// 2.5 — nal_type_name helper
// ============================================================

TEST(NalParser, TypeNameHelper) {
    EXPECT_STREQ(nal_type_name(NalUnitType::VPS_NUT), "VPS");
    EXPECT_STREQ(nal_type_name(NalUnitType::SPS_NUT), "SPS");
    EXPECT_STREQ(nal_type_name(NalUnitType::PPS_NUT), "PPS");
    EXPECT_STREQ(nal_type_name(NalUnitType::IDR_W_RADL), "IDR_W_RADL");
    EXPECT_STREQ(nal_type_name(NalUnitType::IDR_N_LP), "IDR_N_LP");
    EXPECT_STREQ(nal_type_name(NalUnitType::CRA_NUT), "CRA_NUT");
    EXPECT_STREQ(nal_type_name(NalUnitType::TRAIL_R), "TRAIL_R");
    EXPECT_STREQ(nal_type_name(NalUnitType::PREFIX_SEI), "PREFIX_SEI");
    EXPECT_STREQ(nal_type_name(NalUnitType::SUFFIX_SEI), "SUFFIX_SEI");
}

// ============================================================
// 2.6 — Access Unit Boundary Detection
// ============================================================

TEST(NalParser, AccessUnit_SingleIFrame) {
    // Typical I-frame: VPS + SPS + PPS + IDR (first_slice=1)
    std::vector<uint8_t> stream;
    append_nal(stream, 32, {0xAA});        // VPS
    append_nal(stream, 33, {0xBB});        // SPS
    append_nal(stream, 34, {0xCC});        // PPS
    append_nal(stream, 19, {0x80, 0xDD});  // IDR_W_RADL, first_slice_segment_in_pic_flag=1

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());
    auto aus = parser.group_access_units(std::move(nals));

    // All belong to one AU (no previous VCL before VPS/SPS/PPS)
    ASSERT_EQ(aus.size(), 1u);
    EXPECT_EQ(aus[0].nal_units.size(), 4u);
}

TEST(NalParser, AccessUnit_TwoFrames) {
    // Frame 0: VPS + SPS + PPS + IDR(first_slice=1)
    // Frame 1: TRAIL_R(first_slice=1)
    std::vector<uint8_t> stream;
    append_nal(stream, 32, {0xAA});        // VPS
    append_nal(stream, 33, {0xBB});        // SPS
    append_nal(stream, 34, {0xCC});        // PPS
    append_nal(stream, 19, {0x80, 0xDD});  // IDR first_slice=1
    append_nal(stream, 1, {0x80, 0xEE});   // TRAIL_R first_slice=1 -> new AU

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());
    auto aus = parser.group_access_units(std::move(nals));

    ASSERT_EQ(aus.size(), 2u);
    EXPECT_EQ(aus[0].nal_units.size(), 4u);  // VPS+SPS+PPS+IDR
    EXPECT_EQ(aus[1].nal_units.size(), 1u);  // TRAIL_R
}

TEST(NalParser, AccessUnit_SuffixSEI_SameAU) {
    // Suffix SEI after VCL should stay in the same AU (piege spec)
    std::vector<uint8_t> stream;
    append_nal(stream, 32, {0xAA});        // VPS
    append_nal(stream, 33, {0xBB});        // SPS
    append_nal(stream, 34, {0xCC});        // PPS
    append_nal(stream, 19, {0x80, 0xDD});  // IDR first_slice=1
    append_nal(stream, 40, {0xEE, 0xFF});  // SUFFIX_SEI -> same AU

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());
    auto aus = parser.group_access_units(std::move(nals));

    ASSERT_EQ(aus.size(), 1u);
    EXPECT_EQ(aus[0].nal_units.size(), 5u);  // VPS+SPS+PPS+IDR+SUFFIX_SEI
}

TEST(NalParser, AccessUnit_PrefixSEI_NewAU) {
    // Prefix SEI after VCL starts a new AU
    std::vector<uint8_t> stream;
    append_nal(stream, 19, {0x80, 0xDD});  // IDR first_slice=1
    append_nal(stream, 39, {0xEE, 0xFF});  // PREFIX_SEI -> new AU (after VCL)
    append_nal(stream, 1, {0x80, 0xAA});   // TRAIL_R first_slice=1

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());
    auto aus = parser.group_access_units(std::move(nals));

    ASSERT_EQ(aus.size(), 2u);
    EXPECT_EQ(aus[0].nal_units.size(), 1u);  // IDR
    EXPECT_EQ(aus[1].nal_units.size(), 2u);  // PREFIX_SEI + TRAIL_R
}

TEST(NalParser, AccessUnit_AUD_StartsNewAU) {
    // AUD always starts a new AU
    std::vector<uint8_t> stream;
    append_nal(stream, 35, {0x50});        // AUD (pic_type in payload)
    append_nal(stream, 32, {0xAA});        // VPS
    append_nal(stream, 33, {0xBB});        // SPS
    append_nal(stream, 34, {0xCC});        // PPS
    append_nal(stream, 19, {0x80, 0xDD});  // IDR first_slice=1
    append_nal(stream, 35, {0x50});        // AUD -> new AU
    append_nal(stream, 1, {0x80, 0xEE});   // TRAIL_R first_slice=1

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());
    auto aus = parser.group_access_units(std::move(nals));

    ASSERT_EQ(aus.size(), 2u);
    EXPECT_EQ(aus[0].nal_units.size(), 5u);  // AUD+VPS+SPS+PPS+IDR
    EXPECT_EQ(aus[1].nal_units.size(), 2u);  // AUD+TRAIL_R
}

TEST(NalParser, AccessUnit_VPSAfterVCL_NewAU) {
    // VPS/SPS/PPS after VCL start a new AU
    std::vector<uint8_t> stream;
    append_nal(stream, 19, {0x80, 0xDD});  // IDR first_slice=1
    append_nal(stream, 32, {0xAA});        // VPS -> new AU (after VCL)
    append_nal(stream, 33, {0xBB});        // SPS
    append_nal(stream, 34, {0xCC});        // PPS
    append_nal(stream, 1, {0x80, 0xEE});   // TRAIL_R first_slice=1

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());
    auto aus = parser.group_access_units(std::move(nals));

    ASSERT_EQ(aus.size(), 2u);
    EXPECT_EQ(aus[0].nal_units.size(), 1u);  // IDR
    EXPECT_EQ(aus[1].nal_units.size(), 4u);  // VPS+SPS+PPS+TRAIL_R
}

TEST(NalParser, AccessUnit_MultiSliceSameFrame) {
    // Multiple slices in same frame: second slice has first_slice=0
    std::vector<uint8_t> stream;
    append_nal(stream, 32, {0xAA});        // VPS
    append_nal(stream, 33, {0xBB});        // SPS
    append_nal(stream, 34, {0xCC});        // PPS
    append_nal(stream, 19, {0x80, 0xDD});  // IDR first_slice=1
    append_nal(stream, 19, {0x00, 0xEE});  // IDR first_slice=0 -> same AU

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());
    auto aus = parser.group_access_units(std::move(nals));

    ASSERT_EQ(aus.size(), 1u);
    EXPECT_EQ(aus[0].nal_units.size(), 5u);
}

TEST(NalParser, AccessUnit_EOS_TerminatesAU) {
    std::vector<uint8_t> stream;
    append_nal(stream, 19, {0x80, 0xDD});  // IDR first_slice=1
    append_nal(stream, 36, {});            // EOS -> terminates AU, starts new AU

    NalParser parser;
    auto nals = parser.parse(stream.data(), stream.size());
    auto aus = parser.group_access_units(std::move(nals));

    ASSERT_EQ(aus.size(), 2u);
    EXPECT_EQ(aus[0].nal_units.size(), 1u);  // IDR
    EXPECT_EQ(aus[1].nal_units.size(), 1u);  // EOS
}

// ============================================================
// 2.7 — more_rbsp_data edge cases
// ============================================================

TEST(BitstreamReader, MoreRbspData_SingleByte_AllOnes) {
    // 0xFF = 11111111 — last 1 bit at position 7
    // Positions 0-6 are data, position 7 is stop bit
    uint8_t data[] = {0xFF};
    BitstreamReader bs(data, sizeof(data));

    for (int i = 0; i < 7; i++) {
        EXPECT_TRUE(bs.more_rbsp_data()) << "at bit " << i;
        bs.read_bits(1);
    }
    EXPECT_FALSE(bs.more_rbsp_data());
}

TEST(BitstreamReader, MoreRbspData_TrailingZeros) {
    // 0x40 = 01000000 — stop bit at position 1
    // position 0 is data, position 1 is stop bit, 2-7 are alignment zeros
    uint8_t data[] = {0x40};
    BitstreamReader bs(data, sizeof(data));

    EXPECT_TRUE(bs.more_rbsp_data());
    bs.read_bits(1);
    EXPECT_FALSE(bs.more_rbsp_data());
}

TEST(BitstreamReader, MoreRbspData_MultiByte) {
    // 0xAB 0xCD 0x80
    // Last byte 0x80 = 10000000 -> stop bit at position (2*8) = 16
    // All bits 0-15 are data
    uint8_t data[] = {0xAB, 0xCD, 0x80};
    BitstreamReader bs(data, sizeof(data));

    // Read 16 bits — all should be data
    for (int i = 0; i < 16; i++) {
        EXPECT_TRUE(bs.more_rbsp_data()) << "at bit " << i;
        bs.read_bits(1);
    }
    EXPECT_FALSE(bs.more_rbsp_data());
}

// ============================================================
// Real bitstream parsing (integration test)
// ============================================================

TEST(NalParser, ParseRealBitstream_ToyQP30) {
    // Try to parse a real bitstream if available
    std::ifstream file("../tests/conformance/fixtures/toy_qp30.265",
                       std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        GTEST_SKIP() << "toy_qp30.265 not available";
    }

    auto size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    NalParser parser;
    auto nals = parser.parse(data.data(), data.size());

    // A valid HEVC bitstream must have at least VPS + SPS + PPS + slice
    EXPECT_GE(nals.size(), 4u);

    // First three should be VPS, SPS, PPS
    EXPECT_EQ(nals[0].header.nal_unit_type, NalUnitType::VPS_NUT);
    EXPECT_EQ(nals[1].header.nal_unit_type, NalUnitType::SPS_NUT);
    EXPECT_EQ(nals[2].header.nal_unit_type, NalUnitType::PPS_NUT);

    // Check AU grouping
    auto aus = parser.group_access_units(std::move(nals));
    EXPECT_GE(aus.size(), 1u);
}
