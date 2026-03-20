#include "syntax/parameter_sets.h"
#include "common/debug.h"

namespace hevc {

bool ParameterSetManager::process_nal(const NalUnit& nal) {
    BitstreamReader bs(nal.rbsp.data(), nal.rbsp.size());

    switch (nal.header.nal_unit_type) {
        case NalUnitType::VPS_NUT: {
            VPS vps;
            if (!vps.parse(bs)) return false;
            int id = vps.vps_video_parameter_set_id;
            if (id > 15) return false;
            vps_[id] = std::move(vps);
            return true;
        }
        case NalUnitType::SPS_NUT: {
            SPS sps;
            if (!sps.parse(bs)) return false;
            int id = static_cast<int>(sps.sps_seq_parameter_set_id);
            if (id > 15) return false;
            sps_[id] = std::move(sps);
            return true;
        }
        case NalUnitType::PPS_NUT: {
            // PPS needs the referenced SPS for parsing
            // First, peek the PPS and SPS IDs
            uint32_t pps_id = bs.read_ue();
            uint32_t sps_id = bs.read_ue();
            if (pps_id > 63 || sps_id > 15) return false;

            const SPS* sps = get_sps(static_cast<int>(sps_id));
            if (!sps) {
                fprintf(stderr, "PPS references SPS %u which is not available\n", sps_id);
                return false;
            }

            // Re-parse from the beginning
            BitstreamReader bs2(nal.rbsp.data(), nal.rbsp.size());
            PPS pps;
            if (!pps.parse(bs2, *sps)) return false;
            pps_[pps.pps_pic_parameter_set_id] = std::move(pps);
            return true;
        }
        default:
            return false;
    }
}

bool ParameterSetManager::parse_slice_header(SliceHeader& sh, const NalUnit& nal) {
    BitstreamReader bs(nal.rbsp.data(), nal.rbsp.size());

    // Peek first_slice_segment_in_pic_flag and slice_pic_parameter_set_id
    bool first_slice = bs.read_flag();

    // Skip no_output_of_prior_pics_flag for IRAP
    if (is_irap(nal.header.nal_unit_type)) {
        bs.read_flag();
    }

    uint32_t pps_id = bs.read_ue();
    if (pps_id > 63) return false;

    const PPS* pps = get_pps(static_cast<int>(pps_id));
    if (!pps) {
        fprintf(stderr, "Slice references PPS %u which is not available\n", pps_id);
        return false;
    }

    const SPS* sps = get_sps(static_cast<int>(pps->pps_seq_parameter_set_id));
    if (!sps) {
        fprintf(stderr, "PPS %u references SPS %u which is not available\n",
                pps_id, pps->pps_seq_parameter_set_id);
        return false;
    }

    // Update active parameter set IDs (safe against PS replacement)
    active_sps_id_ = static_cast<int>(pps->pps_seq_parameter_set_id);
    active_pps_id_ = static_cast<int>(pps_id);

    // Re-parse the full slice header from the beginning
    BitstreamReader bs2(nal.rbsp.data(), nal.rbsp.size());
    (void)first_slice;
    return sh.parse(bs2, *sps, *pps, nal.header.nal_unit_type, nal.header.TemporalId());
}

} // namespace hevc
