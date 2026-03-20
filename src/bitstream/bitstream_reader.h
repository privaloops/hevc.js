#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace hevc {

// BitstreamReader — Reads bits from an RBSP buffer using a 64-bit sliding window
// Spec refs: §7.2 (more_rbsp_data), §9.1 (parsing)
//
// Performance: O(1) per read_bits() call via buffered extraction (shift/mask),
// critical for CABAC which calls read_bits(1) millions of times per frame.
//
// Error handling: No exceptions (WASM-safe, AD-004). Uses internal error state
// queryable via has_error(). All reads after an error return 0.
class BitstreamReader {
public:
    BitstreamReader() = default;
    BitstreamReader(const uint8_t* data, size_t size);

    // Fixed-length reads
    uint32_t read_bits(int n);
    uint32_t read_u(int n) { return read_bits(n); }
    int32_t  read_i(int n);
    bool     read_flag() { return read_bits(1) != 0; }
    uint8_t  read_byte();

    // Exp-Golomb (§9.2)
    uint32_t read_ue();
    int32_t  read_se();

    // Alignment (§7.2)
    bool byte_aligned() const;
    void byte_alignment();

    // RBSP trailing (§7.2)
    bool more_rbsp_data() const;

    // Position
    size_t bits_read() const { return byte_pos_ * 8 - bits_left_; }
    size_t bits_remaining() const;

    // Error state (replaces exceptions, WASM-safe per AD-004)
    bool has_error() const { return error_; }
    bool eof() const;

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;          // in bytes

    // 64-bit sliding window buffer
    uint64_t buffer_ = 0;     // bits are MSB-aligned
    int bits_left_ = 0;       // number of valid bits in buffer
    size_t byte_pos_ = 0;     // next byte to load from data_

    bool error_ = false;

    void refill();
    size_t find_last_one_bit() const;
};

// RBSP extraction — remove emulation prevention bytes (§7.3.1.1)
// Reusable version: clears and fills `out`, returns output size
size_t extract_rbsp_to(const uint8_t* nal_data, size_t nal_size, std::vector<uint8_t>& out);

// Convenience version (allocates)
std::vector<uint8_t> extract_rbsp(const uint8_t* nal_data, size_t nal_size);

} // namespace hevc
