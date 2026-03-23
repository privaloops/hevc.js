#include "bitstream/nal_unit.h"
#include "bitstream/bitstream_reader.h"
#include "common/debug.h"

#include <cassert>

namespace hevc {

// §7.3.1.2 — NAL unit header (2 bytes, 16 bits)
// forbidden_zero_bit  f(1)
// nal_unit_type       u(6)
// nuh_layer_id        u(6)
// nuh_temporal_id_plus1 u(3)
NalUnitHeader NalParser::parse_header(const uint8_t* data) {
    uint16_t word = (static_cast<uint16_t>(data[0]) << 8) | data[1];

    // bit 15: forbidden_zero_bit (must be 0 per spec)
    if ((word >> 15) & 1) {
        HEVC_LOG(NAL, "WARNING: forbidden_zero_bit is not zero — corrupted NAL%s", "");
    }

    NalUnitHeader h;
    // bits 14-9: nal_unit_type
    h.nal_unit_type = static_cast<NalUnitType>((word >> 9) & 0x3F);
    // bits 8-3: nuh_layer_id
    h.nuh_layer_id = (word >> 3) & 0x3F;
    // bits 2-0: nuh_temporal_id_plus1
    h.nuh_temporal_id_plus1 = word & 0x07;

    return h;
}

// Annex B — Find next start code (0x000001 or 0x00000001)
// Returns the offset of the first byte AFTER the start code prefix.
// Returns size if no start code found.
size_t NalParser::find_start_code(const uint8_t* data, size_t size, size_t offset) {
    for (size_t i = offset; i + 2 < size; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01) {
                return i + 3;  // 3-byte start code
            }
            if (i + 3 < size && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                return i + 4;  // 4-byte start code
            }
        }
    }
    return size;
}

// Annex B — Parse a complete byte stream into NAL units
std::vector<NalUnit> NalParser::parse(const uint8_t* data, size_t size) {
    std::vector<NalUnit> nals;

    // Find the first start code
    size_t nal_start = find_start_code(data, size, 0);
    if (nal_start >= size) {
        return nals;
    }

    while (nal_start < size) {
        // Find the next start code (= end of current NAL)
        // We scan for 0x000001 pattern — the 4-byte variant has a leading 0x00
        // that belongs to the boundary, not the NAL data
        size_t next_sc = find_start_code(data, size, nal_start);

        // Determine end of current NAL data
        size_t nal_end;
        if (next_sc >= size) {
            nal_end = size;
        } else {
            // Rewind past the start code prefix to find the boundary
            // next_sc points AFTER the start code; go back to find 00 00 01 or 00 00 00 01
            nal_end = next_sc - 3;
            if (nal_end > 0 && data[nal_end - 1] == 0x00) {
                nal_end--;  // 4-byte start code: back up one more
            }
        }

        // Remove trailing zero bytes (trailing_zero_8bits per Annex B)
        while (nal_end > nal_start && data[nal_end - 1] == 0x00) {
            nal_end--;
        }

        size_t nal_size = nal_end - nal_start;
        if (nal_size < 2) {
            // NAL too small to contain a header
            nal_start = next_sc;
            continue;
        }

        NalUnit nal;
        nal.offset = nal_start;
        nal.size = nal_size;
        nal.header = parse_header(data + nal_start);

        // Extract RBSP: skip 2-byte header, remove emulation prevention bytes
        nal.rbsp = extract_rbsp(data + nal_start + 2, nal_size - 2, nal.epb_positions);

        HEVC_LOG(NAL, "NAL #%zu: type=%d (%s) size=%zu layer=%d temporal=%d offset=%zu",
                 nals.size(),
                 static_cast<int>(nal.header.nal_unit_type),
                 nal_type_name(nal.header.nal_unit_type),
                 nal.size,
                 nal.header.nuh_layer_id,
                 nal.header.TemporalId(),
                 nal.offset);

        nals.push_back(std::move(nal));
        nal_start = next_sc;
    }

    return nals;
}

// §7.4.2.4.4 — Check if this NAL starts a new Access Unit
// current_au_has_vcl: true if the current AU already contains a VCL NAL
// prev_was_vcl: true if the immediately previous NAL was VCL
bool NalParser::starts_new_access_unit(const NalUnit& nal, bool current_au_has_vcl, bool prev_was_vcl) {
    auto type = nal.header.nal_unit_type;
    auto t = static_cast<uint8_t>(type);

    // VCL NAL with first_slice_segment_in_pic_flag == 1 starts a new AU
    // only if the current AU already has a VCL NAL
    if (is_vcl(type) && current_au_has_vcl) {
        if (nal.rbsp.empty()) return false;
        // first_slice_segment_in_pic_flag is the first bit of the slice header RBSP
        bool first_slice = (nal.rbsp[0] & 0x80) != 0;
        return first_slice;
    }

    // Non-VCL NALs that start a new AU when they appear after the last VCL of a picture
    // "after a VCL NAL" means prev_was_vcl (suffix SEI is excluded below)
    if (prev_was_vcl) {
        // AUD
        if (type == NalUnitType::AUD_NUT) return true;
        // VPS, SPS, PPS
        if (type == NalUnitType::VPS_NUT || type == NalUnitType::SPS_NUT ||
            type == NalUnitType::PPS_NUT) {
            return true;
        }
        // Prefix SEI (NOT suffix SEI — suffix belongs to current AU)
        if (type == NalUnitType::PREFIX_SEI) {
            return true;
        }
        // EOS, EOB
        if (type == NalUnitType::EOS_NUT || type == NalUnitType::EOB_NUT) {
            return true;
        }
        // RSV_NVCL (41..47) and UNSPEC (48..63)
        if (t >= 41 && t <= 47) return true;
        if (t >= 48 && t <= 63) return true;
    }

    return false;
}

// §7.4.2.4.4 — Group NAL units into Access Units
std::vector<AccessUnit> NalParser::group_access_units(std::vector<NalUnit>&& nals) {
    std::vector<AccessUnit> aus;
    if (nals.empty()) return aus;

    AccessUnit current_au;
    bool current_au_has_vcl = false;  // does the current AU have a VCL NAL?
    bool prev_was_vcl = false;        // was the immediately previous NAL a VCL?

    for (auto& nal : nals) {
        if (starts_new_access_unit(nal, current_au_has_vcl, prev_was_vcl)) {
            // Flush current AU if it has NALs
            if (!current_au.nal_units.empty()) {
                aus.push_back(std::move(current_au));
                current_au = AccessUnit{};
                current_au_has_vcl = false;
            }
        }

        bool vcl = is_vcl(nal.header.nal_unit_type);
        current_au.nal_units.push_back(std::move(nal));
        if (vcl) {
            current_au_has_vcl = true;
        }
        prev_was_vcl = vcl;
    }

    // Flush last AU
    if (!current_au.nal_units.empty()) {
        aus.push_back(std::move(current_au));
    }

    HEVC_LOG(NAL, "Grouped %zu NAL units into %zu Access Units", nals.size(), aus.size());
    return aus;
}

// Human-readable NAL type names (Table 7-1)
const char* nal_type_name(NalUnitType type) {
    switch (type) {
        case NalUnitType::TRAIL_N:     return "TRAIL_N";
        case NalUnitType::TRAIL_R:     return "TRAIL_R";
        case NalUnitType::TSA_N:       return "TSA_N";
        case NalUnitType::TSA_R:       return "TSA_R";
        case NalUnitType::STSA_N:      return "STSA_N";
        case NalUnitType::STSA_R:      return "STSA_R";
        case NalUnitType::RADL_N:      return "RADL_N";
        case NalUnitType::RADL_R:      return "RADL_R";
        case NalUnitType::RASL_N:      return "RASL_N";
        case NalUnitType::RASL_R:      return "RASL_R";
        case NalUnitType::RSV_VCL_N10: return "RSV_VCL_N10";
        case NalUnitType::RSV_VCL_R11: return "RSV_VCL_R11";
        case NalUnitType::RSV_VCL_N12: return "RSV_VCL_N12";
        case NalUnitType::RSV_VCL_R13: return "RSV_VCL_R13";
        case NalUnitType::RSV_VCL_N14: return "RSV_VCL_N14";
        case NalUnitType::RSV_VCL_R15: return "RSV_VCL_R15";
        case NalUnitType::BLA_W_LP:    return "BLA_W_LP";
        case NalUnitType::BLA_W_RADL:  return "BLA_W_RADL";
        case NalUnitType::BLA_N_LP:    return "BLA_N_LP";
        case NalUnitType::IDR_W_RADL:  return "IDR_W_RADL";
        case NalUnitType::IDR_N_LP:    return "IDR_N_LP";
        case NalUnitType::CRA_NUT:     return "CRA_NUT";
        case NalUnitType::RSV_IRAP_22: return "RSV_IRAP_22";
        case NalUnitType::RSV_IRAP_23: return "RSV_IRAP_23";
        case NalUnitType::VPS_NUT:     return "VPS";
        case NalUnitType::SPS_NUT:     return "SPS";
        case NalUnitType::PPS_NUT:     return "PPS";
        case NalUnitType::AUD_NUT:     return "AUD";
        case NalUnitType::EOS_NUT:     return "EOS";
        case NalUnitType::EOB_NUT:     return "EOB";
        case NalUnitType::FD_NUT:      return "FD";
        case NalUnitType::PREFIX_SEI:  return "PREFIX_SEI";
        case NalUnitType::SUFFIX_SEI:  return "SUFFIX_SEI";
        default:                       return "UNKNOWN";
    }
}

} // namespace hevc
