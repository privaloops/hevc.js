#include "bitstream/bitstream_reader.h"
#include <cassert>

namespace hevc {

BitstreamReader::BitstreamReader(const uint8_t* data, size_t size)
    : data_(data), size_(size), buffer_(0), bits_left_(0), byte_pos_(0), error_(false)
{
    refill();
}

// Load bytes into the 64-bit buffer until full (up to 56 bits loaded per refill,
// leaving room for at least one more byte on next refill)
void BitstreamReader::refill() {
    while (bits_left_ <= 56 && byte_pos_ < size_) {
        buffer_ |= static_cast<uint64_t>(data_[byte_pos_++]) << (56 - bits_left_);
        bits_left_ += 8;
    }
}

// O(1) bit extraction via shift/mask from the 64-bit buffer
uint32_t BitstreamReader::read_bits(int n) {
    assert(n >= 0 && n <= 32);
    if (n == 0) return 0;
    if (error_) return 0;

    if (bits_left_ < n) refill();
    if (bits_left_ < n) {
        error_ = true;
        return 0;
    }

    uint32_t result = static_cast<uint32_t>(buffer_ >> (64 - n));
    buffer_ <<= n;
    bits_left_ -= n;
    return result;
}

int32_t BitstreamReader::read_i(int n) {
    uint32_t val = read_bits(n);
    if (error_) return 0;
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
    if (error_) return 0;

    int leading_zeros = 0;
    while (!eof() && !error_ && read_bits(1) == 0) {
        leading_zeros++;
        if (leading_zeros > 31) {
            error_ = true;
            return 0;
        }
    }

    if (error_) return 0;
    if (leading_zeros == 0) return 0;

    uint32_t suffix = read_bits(leading_zeros);
    return (1u << leading_zeros) - 1 + suffix;
}

// Exp-Golomb signed (§9.2)
// Maps: 0->0, 1->1, 2->-1, 3->2, 4->-2, ...
int32_t BitstreamReader::read_se() {
    uint32_t code = read_ue();
    int32_t value = static_cast<int32_t>((code + 1) / 2);
    return (code & 1) ? value : -value;
}

bool BitstreamReader::byte_aligned() const {
    return (bits_read() % 8) == 0;
}

void BitstreamReader::byte_alignment() {
    if (!byte_aligned()) {
        read_bits(1);  // alignment_bit_equal_to_one
    }
    while (!byte_aligned() && !error_) {
        read_bits(1);  // alignment_bit_equal_to_zero
    }
}

// §7.2 — more_rbsp_data()
// Returns true if current position is before the rbsp_stop_one_bit
bool BitstreamReader::more_rbsp_data() const {
    size_t pos = bits_read();
    if (pos >= size_ * 8) return false;

    size_t last_one = find_last_one_bit();
    return pos < last_one;
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
    size_t pos = bits_read();
    return (size_ * 8 > pos) ? (size_ * 8 - pos) : 0;
}

bool BitstreamReader::eof() const {
    return bits_left_ == 0 && byte_pos_ >= size_;
}

// Emulation prevention byte removal (§7.3.1.1)
// Reusable version — clears `out` and fills it, returns output size
size_t extract_rbsp_to(const uint8_t* nal_data, size_t nal_size, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(nal_size);

    size_t i = 0;
    while (i < nal_size) {
        if (i + 2 < nal_size &&
            nal_data[i] == 0x00 &&
            nal_data[i + 1] == 0x00 &&
            nal_data[i + 2] == 0x03)
        {
            out.push_back(0x00);
            out.push_back(0x00);
            i += 3;  // skip emulation_prevention_three_byte
        } else {
            out.push_back(nal_data[i]);
            i++;
        }
    }

    return out.size();
}

// Convenience version — allocates a new vector
std::vector<uint8_t> extract_rbsp(const uint8_t* nal_data, size_t nal_size) {
    std::vector<uint8_t> out;
    extract_rbsp_to(nal_data, nal_size, out);
    return out;
}

} // namespace hevc
