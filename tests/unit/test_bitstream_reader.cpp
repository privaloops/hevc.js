#include <gtest/gtest.h>
#include "bitstream/bitstream_reader.h"

using namespace hevc;

// --- read_bits ---

TEST(BitstreamReader, ReadBits_SingleByte) {
    uint8_t data[] = { 0b10110100 };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_EQ(bs.read_bits(1), 1u);
    EXPECT_EQ(bs.read_bits(1), 0u);
    EXPECT_EQ(bs.read_bits(3), 0b110u);
    EXPECT_EQ(bs.read_bits(3), 0b100u);
}

TEST(BitstreamReader, ReadBits_CrossByte) {
    uint8_t data[] = { 0xFF, 0x00 };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_EQ(bs.read_bits(4), 0x0Fu);
    EXPECT_EQ(bs.read_bits(8), 0xF0u);  // crosses byte boundary
    EXPECT_EQ(bs.read_bits(4), 0x00u);
}

TEST(BitstreamReader, ReadBits_32Bits) {
    uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_EQ(bs.read_bits(32), 0xDEADBEEFu);
}

// --- read_flag ---

TEST(BitstreamReader, ReadFlag) {
    uint8_t data[] = { 0b10000000 };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_TRUE(bs.read_flag());
    EXPECT_FALSE(bs.read_flag());
}

// --- Exp-Golomb ---

TEST(BitstreamReader, ReadUE_Zero) {
    // ue(0) = 1 (single bit '1')
    uint8_t data[] = { 0b10000000 };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_EQ(bs.read_ue(), 0u);
}

TEST(BitstreamReader, ReadUE_Small) {
    // ue(1) = 010 -> value 1
    // ue(2) = 011 -> value 2
    // ue(3) = 00100 -> value 3
    uint8_t data[] = { 0b01001100, 0b10000000 };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_EQ(bs.read_ue(), 1u);  // 010
    EXPECT_EQ(bs.read_ue(), 2u);  // 011
    EXPECT_EQ(bs.read_ue(), 3u);  // 00100
}

TEST(BitstreamReader, ReadUE_Large) {
    // ue(7) = 0001000 -> 7
    uint8_t data[] = { 0b00010000, 0b00000000 };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_EQ(bs.read_ue(), 7u);
}

TEST(BitstreamReader, ReadSE) {
    // se maps: ue(0)→0, ue(1)→1, ue(2)→-1, ue(3)→2, ue(4)→-2
    // Binary: 1 | 010 | 011 | 00100 | 00101
    // = 1 010 011 0 | 0100 0010 | 1xxx xxxx
    // = 0b10100110  | 0b01000010 | 0b10000000
    uint8_t data[] = { 0b10100110, 0b01000010, 0b10000000 };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_EQ(bs.read_se(), 0);   // ue=0 : 1
    EXPECT_EQ(bs.read_se(), 1);   // ue=1 : 010
    EXPECT_EQ(bs.read_se(), -1);  // ue=2 : 011
    EXPECT_EQ(bs.read_se(), 2);   // ue=3 : 00100
    EXPECT_EQ(bs.read_se(), -2);  // ue=4 : 00101
}

// --- byte_aligned ---

TEST(BitstreamReader, ByteAligned) {
    uint8_t data[] = { 0xFF };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_TRUE(bs.byte_aligned());
    bs.read_bits(1);
    EXPECT_FALSE(bs.byte_aligned());
    bs.read_bits(7);
    EXPECT_TRUE(bs.byte_aligned());
}

// --- more_rbsp_data ---

TEST(BitstreamReader, MoreRbspData_WithData) {
    // Data: 0xAB = 10101011
    // Stop bit is the last '1' (bit 7 = position 0 from MSB... let's think)
    // 0xAB = 1010 1011
    // Last 1 bit is at position 7 (0-indexed from MSB)
    // more_rbsp_data should return true while we're before that last 1
    uint8_t data[] = { 0xAB };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_TRUE(bs.more_rbsp_data());
}

TEST(BitstreamReader, MoreRbspData_StopBitOnly) {
    // 0x80 = 10000000 — just the stop bit + 7 alignment zeros
    uint8_t data[] = { 0x80 };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_FALSE(bs.more_rbsp_data());
}

TEST(BitstreamReader, MoreRbspData_DataThenStop) {
    // Two bytes: 0xFF 0x80
    // Byte 0: 11111111 (data)
    // Byte 1: 10000000 (stop bit + alignment)
    // Last '1' is at bit 8 (first bit of second byte)
    // So bits 0-7 are data, bit 8 is stop bit
    uint8_t data[] = { 0xFF, 0x80 };
    BitstreamReader bs(data, sizeof(data));

    // Reading 8 bits of data
    for (int i = 0; i < 8; i++) {
        EXPECT_TRUE(bs.more_rbsp_data());
        bs.read_bits(1);
    }
    // Now at bit 8, which is the stop bit
    EXPECT_FALSE(bs.more_rbsp_data());
}

// --- extract_rbsp ---

TEST(ExtractRbsp, NoEmulationPrevention) {
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    auto rbsp = extract_rbsp(data, sizeof(data));

    EXPECT_EQ(rbsp.size(), 4u);
    EXPECT_EQ(rbsp[0], 0x01);
    EXPECT_EQ(rbsp[3], 0x04);
}

TEST(ExtractRbsp, SingleEmulationPrevention) {
    // 00 00 03 01 -> should become 00 00 01
    uint8_t data[] = { 0x00, 0x00, 0x03, 0x01 };
    auto rbsp = extract_rbsp(data, sizeof(data));

    EXPECT_EQ(rbsp.size(), 3u);
    EXPECT_EQ(rbsp[0], 0x00);
    EXPECT_EQ(rbsp[1], 0x00);
    EXPECT_EQ(rbsp[2], 0x01);
}

TEST(ExtractRbsp, MultipleEmulationPrevention) {
    // 00 00 03 00 00 00 03 01 -> 00 00 00 00 00 01
    uint8_t data[] = { 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x01 };
    auto rbsp = extract_rbsp(data, sizeof(data));

    EXPECT_EQ(rbsp.size(), 6u);
    EXPECT_EQ(rbsp[0], 0x00);
    EXPECT_EQ(rbsp[1], 0x00);
    EXPECT_EQ(rbsp[2], 0x00);
    EXPECT_EQ(rbsp[3], 0x00);
    EXPECT_EQ(rbsp[4], 0x00);
    EXPECT_EQ(rbsp[5], 0x01);
}

TEST(ExtractRbsp, PreserveNonEmulation) {
    // 00 00 04 should NOT be treated as emulation prevention
    uint8_t data[] = { 0x00, 0x00, 0x04 };
    auto rbsp = extract_rbsp(data, sizeof(data));

    EXPECT_EQ(rbsp.size(), 3u);
}

// --- Error state (AD-004: no exceptions, WASM-safe) ---

TEST(BitstreamReader, ErrorOnReadPastEnd) {
    uint8_t data[] = { 0xFF };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_FALSE(bs.has_error());
    bs.read_bits(8);  // consume all
    EXPECT_FALSE(bs.has_error());

    bs.read_bits(1);  // past end
    EXPECT_TRUE(bs.has_error());
    EXPECT_EQ(bs.read_bits(1), 0u);  // subsequent reads return 0
}

TEST(BitstreamReader, ErrorOnExpGolombOverflow) {
    // 32 leading zeros + no terminating 1 -> overflow
    uint8_t data[] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
    BitstreamReader bs(data, sizeof(data));

    bs.read_ue();
    EXPECT_TRUE(bs.has_error());
}

TEST(BitstreamReader, BitsRead) {
    uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    BitstreamReader bs(data, sizeof(data));

    EXPECT_EQ(bs.bits_read(), 0u);
    bs.read_bits(4);
    EXPECT_EQ(bs.bits_read(), 4u);
    bs.read_bits(12);
    EXPECT_EQ(bs.bits_read(), 16u);
}

// --- extract_rbsp_to (reusable buffer) ---

TEST(ExtractRbsp, ReusableBuffer) {
    uint8_t data1[] = { 0x00, 0x00, 0x03, 0x01 };
    uint8_t data2[] = { 0x01, 0x02, 0x03 };

    std::vector<uint8_t> buf;

    // First call
    size_t sz1 = extract_rbsp_to(data1, sizeof(data1), buf);
    EXPECT_EQ(sz1, 3u);
    EXPECT_EQ(buf[2], 0x01);

    // Second call reuses buffer (no new allocation if capacity sufficient)
    size_t sz2 = extract_rbsp_to(data2, sizeof(data2), buf);
    EXPECT_EQ(sz2, 3u);
    EXPECT_EQ(buf[0], 0x01);
    EXPECT_EQ(buf[1], 0x02);
    EXPECT_EQ(buf[2], 0x03);
}
