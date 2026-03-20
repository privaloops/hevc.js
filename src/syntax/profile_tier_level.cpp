#include "syntax/profile_tier_level.h"
#include "bitstream/bitstream_reader.h"
#include "common/debug.h"

namespace hevc {

// Skip sub-layer profile data: same structure as general profile
// profile_space(2) + tier_flag(1) + profile_idc(5) + compatibility(32)
// + progressive/interlaced/non_packed/frame_only(4) + constraint(43) + inbld(1) = 88 bits
static void skip_sub_layer_profile(BitstreamReader& bs) {
    bs.read_bits(32);  // profile_space(2) + tier_flag(1) + profile_idc(5) + 24 compat flags
    bs.read_bits(32);  // remaining 8 compat flags + progressive(1) + interlaced(1) + non_packed(1) + frame_only(1) + 20 constraint bits
    bs.read_bits(24);  // remaining 23 constraint bits + inbld(1)
}

// Spec §7.3.3 — profile_tier_level(profilePresentFlag, maxNumSubLayersMinus1)
bool ProfileTierLevel::parse(BitstreamReader& bs, bool profilePresentFlag, int maxNumSubLayersMinus1) {
    if (profilePresentFlag) {
        general_profile_space = bs.read_bits(2);
        general_tier_flag = bs.read_flag();
        general_profile_idc = bs.read_bits(5);

        for (int j = 0; j < 32; j++) {
            general_profile_compatibility_flag[j] = bs.read_flag();
        }

        general_progressive_source_flag = bs.read_flag();
        general_interlaced_source_flag = bs.read_flag();
        general_non_packed_constraint_flag = bs.read_flag();
        general_frame_only_constraint_flag = bs.read_flag();

        // Constraint flags — always 43 bits total regardless of branch, then 1 bit inbld
        // §7.3.3: the bit count is constant across all branches
        bool is_rext_profile = false;
        for (int p : {4, 5, 6, 7, 8, 9, 10, 11}) {
            if (general_profile_idc == p || general_profile_compatibility_flag[p]) {
                is_rext_profile = true;
                break;
            }
        }

        if (is_rext_profile) {
            general_max_12bit_constraint_flag = bs.read_flag();
            general_max_10bit_constraint_flag = bs.read_flag();
            general_max_8bit_constraint_flag = bs.read_flag();
            general_max_422chroma_constraint_flag = bs.read_flag();
            general_max_420chroma_constraint_flag = bs.read_flag();
            general_max_monochrome_constraint_flag = bs.read_flag();
            general_intra_constraint_flag = bs.read_flag();
            general_one_picture_only_constraint_flag = bs.read_flag();
            general_lower_bit_rate_constraint_flag = bs.read_flag();

            bool is_5_9_10_11 = false;
            for (int p : {5, 9, 10, 11}) {
                if (general_profile_idc == p || general_profile_compatibility_flag[p]) {
                    is_5_9_10_11 = true;
                    break;
                }
            }
            if (is_5_9_10_11) {
                bs.read_bits(1);   // general_max_14bit_constraint_flag
                bs.read_bits(32);  // general_reserved_zero_33bits (first 32)
                bs.read_bits(1);   // general_reserved_zero_33bits (last 1)
            } else {
                bs.read_bits(32);  // general_reserved_zero_34bits (first 32)
                bs.read_bits(2);   // general_reserved_zero_34bits (last 2)
            }
        } else if (general_profile_idc == 2 || general_profile_compatibility_flag[2]) {
            bs.read_bits(7);   // general_reserved_zero_7bits
            general_one_picture_only_constraint_flag = bs.read_flag();
            bs.read_bits(32);  // general_reserved_zero_35bits (first 32)
            bs.read_bits(3);   // general_reserved_zero_35bits (last 3)
        } else {
            bs.read_bits(32);  // general_reserved_zero_43bits (first 32)
            bs.read_bits(11);  // general_reserved_zero_43bits (last 11)
        }

        // general_inbld_flag or general_reserved_zero_bit (1 bit)
        bs.read_bits(1);
    }

    general_level_idc = bs.read_bits(8);

    // Sub-layer flags
    for (int i = 0; i < maxNumSubLayersMinus1; i++) {
        sub_layer_profile_present_flag[i] = bs.read_flag();
        sub_layer_level_present_flag[i] = bs.read_flag();
    }

    // Reserved zero 2-bit padding when maxNumSubLayersMinus1 > 0
    if (maxNumSubLayersMinus1 > 0) {
        for (int i = maxNumSubLayersMinus1; i < 8; i++) {
            bs.read_bits(2);  // reserved_zero_2bits
        }
    }

    // Sub-layer profile/tier/level data
    for (int i = 0; i < maxNumSubLayersMinus1; i++) {
        if (sub_layer_profile_present_flag[i]) {
            // Same structure as general profile: 88 bits total
            skip_sub_layer_profile(bs);
        }
        if (sub_layer_level_present_flag[i]) {
            sub_layer_level_idc[i] = bs.read_bits(8);
        }
    }

    HEVC_LOG(PARSE, "PTL: profile_idc=%d tier=%d level=%d.%d",
             general_profile_idc, general_tier_flag ? 1 : 0,
             general_level_idc / 30, (general_level_idc % 30) / 3);

    return true;
}

} // namespace hevc
