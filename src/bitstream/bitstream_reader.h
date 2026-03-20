#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace hevc {

// BitstreamReader — Reads bits from an RBSP buffer
// Spec refs: §7.2 (more_rbsp_data), §9.1 (parsing)
//
// Uses a 64-bit cache with lazy refill for O(1) read_bits.
// The last-one-bit position (for more_rbsp_data) is computed once at construction.
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
    size_t bits_read() const { return bit_pos_; }
    size_t bits_remaining() const;

    // State
    bool eof() const;
    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;            // in bytes
    size_t bit_pos_ = 0;         // current bit position (logical)

    // 64-bit read cache
    uint64_t cache_ = 0;
    int cache_bits_ = 0;         // valid bits remaining in cache (MSB-aligned)
    size_t byte_pos_ = 0;        // next byte to load into cache

    void refill();

    // Precomputed position of rbsp_stop_one_bit (§7.2)
    size_t last_one_bit_pos_ = 0;
    static size_t find_last_one_bit(const uint8_t* data, size_t size);
};

// RBSP extraction — remove emulation prevention bytes (§7.3.1.1)
std::vector<uint8_t> extract_rbsp(const uint8_t* nal_data, size_t nal_size);

} // namespace hevc
