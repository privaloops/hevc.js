#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "common/types.h"

namespace hevc {

// A single NAL unit extracted from the byte stream
struct NalUnit {
    NalUnitHeader header;
    std::vector<uint8_t> rbsp;  // RBSP data (emulation prevention removed)
    size_t offset;              // byte offset in the original stream
    size_t size;                // original NAL size (before EP removal)
};

// An Access Unit = one frame = group of NAL units
struct AccessUnit {
    std::vector<NalUnit> nal_units;
    int poc = -1;  // filled after slice header parsing
};

// Byte stream parser — extracts NAL units from Annex B byte stream
// Spec refs: Annex B, §7.3.1
class NalParser {
public:
    // Parse a complete byte stream into NAL units
    std::vector<NalUnit> parse(const uint8_t* data, size_t size);

    // Group NAL units into Access Units (§7.4.2.4.4)
    std::vector<AccessUnit> group_access_units(const std::vector<NalUnit>& nals);

private:
    // Find next start code (0x000001 or 0x00000001)
    // Returns offset of first byte after start code, or -1 if not found
    int64_t find_start_code(const uint8_t* data, size_t size, size_t offset);

    // Parse NAL unit header (2 bytes)
    NalUnitHeader parse_header(const uint8_t* data);

    // Check if this NAL starts a new Access Unit
    bool starts_new_access_unit(const NalUnit& nal, bool prev_was_vcl);
};

} // namespace hevc
