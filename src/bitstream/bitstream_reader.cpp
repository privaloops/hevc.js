#include "bitstream/bitstream_reader.h"
#include <cassert>
#include <stdexcept>

namespace hevc {

BitstreamReader::BitstreamReader(const uint8_t* data, size_t size)
    : data_(data), size_(size), bit_pos_(0) {}

uint32_t BitstreamReader::read_bits(int n) {
    assert(n >= 0 && n <= 32);
    assert(!eof());

    uint32_t result = 0;
    for (int i = 0; i < n; i++) {
        size_t byte_idx = bit_pos_ / 8;
        size_t bit_idx  = 7 - (bit_pos_ % 8);

        if (byte_idx >= size_) {
            throw std::runtime_error("BitstreamReader: read past end of data");
        }

        result = (result << 1) | ((data_[byte_idx] >> bit_idx) & 1);
        bit_pos_++;
    }
    return result;
}

int32_t BitstreamReader::read_i(int n) {
    uint32_t val = read_bits(n);
    // Sign extension for 2's complement
    if (val & (1u << (n - 1))) {
        val |= (~0u) << n;
    }
    return static_cast<int32_t>(val);
}

uint8_t BitstreamReader::read_byte() {
    assert(byte_aligned());
    return static_cast<uint8_t>(read_bits(8));
}

// Exp-Golomb unsigned (§9.2)
// Reads leading zeros, then reads (leadingZeros + 1) bits
uint32_t BitstreamReader::read_ue() {
    int leading_zeros = 0;
    while (!eof() && read_bits(1) == 0) {
        leading_zeros++;
        if (leading_zeros > 31) {
            throw std::runtime_error("BitstreamReader: Exp-Golomb overflow");
        }
    }

    if (leading_zeros == 0) return 0;

    uint32_t suffix = read_bits(leading_zeros);
    return (1u << leading_zeros) - 1 + suffix;
}

// Exp-Golomb signed (§9.2)
// Maps: 0→0, 1→1, 2→-1, 3→2, 4→-2, ...
int32_t BitstreamReader::read_se() {
    uint32_t code = read_ue();
    int32_t value = static_cast<int32_t>((code + 1) / 2);
    return (code & 1) ? value : -value;
}

bool BitstreamReader::byte_aligned() const {
    return (bit_pos_ % 8) == 0;
}

void BitstreamReader::byte_alignment() {
    // Read and discard alignment_bit_equal_to_one (1 bit)
    // Then read alignment_bit_equal_to_zero until byte aligned
    if (!byte_aligned()) {
        read_bits(1);  // alignment_bit_equal_to_one
    }
    while (!byte_aligned()) {
        read_bits(1);  // alignment_bit_equal_to_zero
    }
}

// §7.2 — more_rbsp_data()
// Returns true if current position is before the rbsp_stop_one_bit
bool BitstreamReader::more_rbsp_data() const {
    if (bit_pos_ >= size_ * 8) return false;

    size_t last_one = find_last_one_bit();
    return bit_pos_ < last_one;
}

size_t BitstreamReader::find_last_one_bit() const {
    // Scan backward from end to find the last '1' bit (rbsp_stop_one_bit)
    for (size_t i = size_ * 8; i > 0; i--) {
        size_t byte_idx = (i - 1) / 8;
        size_t bit_idx  = 7 - ((i - 1) % 8);
        if ((data_[byte_idx] >> bit_idx) & 1) {
            return i - 1;
        }
    }
    return 0;
}

size_t BitstreamReader::bits_remaining() const {
    return (size_ * 8 > bit_pos_) ? (size_ * 8 - bit_pos_) : 0;
}

bool BitstreamReader::eof() const {
    return bit_pos_ >= size_ * 8;
}

// Emulation prevention byte removal (§7.3.1.1)
// In the byte stream, 0x000003 is an emulation prevention sequence.
// Remove the 0x03 byte to produce the RBSP.
std::vector<uint8_t> extract_rbsp(const uint8_t* nal_data, size_t nal_size) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(nal_size);

    size_t i = 0;
    while (i < nal_size) {
        // Check for emulation prevention: 0x00 0x00 0x03
        if (i + 2 < nal_size &&
            nal_data[i] == 0x00 &&
            nal_data[i + 1] == 0x00 &&
            nal_data[i + 2] == 0x03)
        {
            // Copy the two 0x00 bytes, skip the 0x03
            rbsp.push_back(0x00);
            rbsp.push_back(0x00);
            i += 3;  // skip emulation_prevention_three_byte
        } else {
            rbsp.push_back(nal_data[i]);
            i++;
        }
    }

    return rbsp;
}

} // namespace hevc
