#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>

#include "bitstream/nal_unit.h"
#include "syntax/parameter_sets.h"

using namespace hevc;

#ifndef FIXTURES_DIR
#define FIXTURES_DIR "tests/conformance/fixtures"
#endif

static std::string fixture(const char* name) {
    return std::string(FIXTURES_DIR) + "/" + name;
}

// Helper: read a file into a byte vector
static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// Helper: parse a bitstream file into NALs and process parameter sets
static bool setup_from_file(const std::string& path, ParameterSetManager& mgr, std::vector<NalUnit>& nals) {
    auto data = read_file(path);
    if (data.empty()) return false;
    NalParser parser;
    nals = parser.parse(data.data(), data.size());
    for (const auto& nal : nals) {
        if (nal.header.nal_unit_type == NalUnitType::VPS_NUT ||
            nal.header.nal_unit_type == NalUnitType::SPS_NUT ||
            nal.header.nal_unit_type == NalUnitType::PPS_NUT) {
            mgr.process_nal(nal);
        }
    }
    return true;
}

// ---- ProfileTierLevel tests ----

TEST(ProfileTierLevel, ParseMainProfile) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("toy_qp30.265"), mgr, nals));

    const SPS* sps = mgr.get_sps(0);
    ASSERT_NE(sps, nullptr);
    EXPECT_EQ(sps->profile_tier_level.general_profile_idc, 3);
    EXPECT_EQ(sps->profile_tier_level.general_tier_flag, false);
    EXPECT_EQ(sps->profile_tier_level.general_level_idc, 30);
}

// ---- VPS tests ----

TEST(VPS, ParseToy) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("toy_qp30.265"), mgr, nals));

    const VPS* vps = mgr.get_vps(0);
    ASSERT_NE(vps, nullptr);
    EXPECT_EQ(vps->vps_video_parameter_set_id, 0);
    EXPECT_EQ(vps->vps_max_layers_minus1, 0);
    EXPECT_EQ(vps->vps_max_sub_layers_minus1, 0);
    EXPECT_TRUE(vps->vps_temporal_id_nesting_flag);
}

// ---- SPS tests ----

TEST(SPS, ParseDimensions64x64) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("toy_qp30.265"), mgr, nals));

    const SPS* sps = mgr.get_sps(0);
    ASSERT_NE(sps, nullptr);
    EXPECT_EQ(sps->pic_width_in_luma_samples, 64u);
    EXPECT_EQ(sps->pic_height_in_luma_samples, 64u);
    EXPECT_EQ(sps->chroma_format_idc, 1u);
    EXPECT_EQ(sps->BitDepthY, 8);
    EXPECT_EQ(sps->BitDepthC, 8);
}

TEST(SPS, DerivedValues) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("toy_qp30.265"), mgr, nals));

    const SPS* sps = mgr.get_sps(0);
    ASSERT_NE(sps, nullptr);

    EXPECT_EQ(sps->ChromaArrayType, 1);
    EXPECT_EQ(sps->QpBdOffsetY, 0);
    EXPECT_EQ(sps->QpBdOffsetC, 0);
    EXPECT_EQ(sps->CtbSizeY, 32);
    EXPECT_EQ(sps->MinCbSizeY, 16);
    EXPECT_EQ(sps->PicWidthInCtbsY, 2);
    EXPECT_EQ(sps->PicHeightInCtbsY, 2);
    EXPECT_EQ(sps->PicSizeInCtbsY, 4);
    EXPECT_GT(sps->MaxPicOrderCntLsb, 0);
    EXPECT_EQ(sps->SubWidthC, 2);
    EXPECT_EQ(sps->SubHeightC, 2);
}

TEST(SPS, ParseQCIF) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("p_qcif_10f.265"), mgr, nals));

    const SPS* sps = mgr.get_sps(0);
    ASSERT_NE(sps, nullptr);
    EXPECT_EQ(sps->pic_width_in_luma_samples, 176u);
    EXPECT_EQ(sps->pic_height_in_luma_samples, 144u);
    EXPECT_EQ(sps->CtbSizeY, 64);
    EXPECT_EQ(sps->PicWidthInCtbsY, 3);
    EXPECT_EQ(sps->PicHeightInCtbsY, 3);
    EXPECT_EQ(sps->MinCbSizeY, 8);
}

TEST(SPS, Parse1080p) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("bbb1080_50f.265"), mgr, nals));

    const SPS* sps = mgr.get_sps(0);
    ASSERT_NE(sps, nullptr);
    EXPECT_EQ(sps->pic_width_in_luma_samples, 1920u);
    EXPECT_EQ(sps->pic_height_in_luma_samples, 1088u);
    EXPECT_EQ(sps->profile_tier_level.general_profile_idc, 1);
    EXPECT_EQ(sps->profile_tier_level.general_level_idc, 120);
    EXPECT_TRUE(sps->conformance_window_flag);
    EXPECT_EQ(sps->conf_win_bottom_offset, 4u);
    EXPECT_TRUE(sps->scaling_list_enabled_flag);
    EXPECT_TRUE(sps->sample_adaptive_offset_enabled_flag);
}

TEST(SPS, ShortTermRPS) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("bbb1080_50f.265"), mgr, nals));

    const SPS* sps = mgr.get_sps(0);
    ASSERT_NE(sps, nullptr);
    EXPECT_EQ(sps->num_short_term_ref_pic_sets, 4u);
    EXPECT_EQ(sps->st_ref_pic_sets[0].NumNegativePics, 4u);
    EXPECT_EQ(sps->st_ref_pic_sets[0].NumPositivePics, 0u);
    EXPECT_EQ(sps->st_ref_pic_sets[1].NumNegativePics, 1u);
}

// ---- PPS tests ----

TEST(PPS, ParseToy) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("toy_qp30.265"), mgr, nals));

    const PPS* pps = mgr.get_pps(0);
    ASSERT_NE(pps, nullptr);
    EXPECT_EQ(pps->pps_pic_parameter_set_id, 0u);
    EXPECT_EQ(pps->pps_seq_parameter_set_id, 0u);
    EXPECT_TRUE(pps->pps_deblocking_filter_disabled_flag);
    EXPECT_FALSE(pps->tiles_enabled_flag);
}

TEST(PPS, TileScanNoTiles) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("toy_qp30.265"), mgr, nals));

    const PPS* pps = mgr.get_pps(0);
    const SPS* sps = mgr.get_sps(0);
    ASSERT_NE(pps, nullptr);
    ASSERT_NE(sps, nullptr);

    EXPECT_EQ(static_cast<int>(pps->CtbAddrRsToTs.size()), sps->PicSizeInCtbsY);
    for (int i = 0; i < sps->PicSizeInCtbsY; i++) {
        EXPECT_EQ(pps->CtbAddrRsToTs[i], i);
        EXPECT_EQ(pps->CtbAddrTsToRs[i], i);
        EXPECT_EQ(pps->TileId[i], 0);
    }
}

TEST(PPS, WPP) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("bbb1080_50f.265"), mgr, nals));

    const PPS* pps = mgr.get_pps(0);
    ASSERT_NE(pps, nullptr);
    EXPECT_TRUE(pps->entropy_coding_sync_enabled_flag);
    EXPECT_FALSE(pps->tiles_enabled_flag);
}

// ---- Slice Header tests ----

TEST(SliceHeader, ParseIDRSlice) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("toy_qp30.265"), mgr, nals));

    for (const auto& nal : nals) {
        if (is_vcl(nal.header.nal_unit_type)) {
            SliceHeader sh;
            ASSERT_TRUE(mgr.parse_slice_header(sh, nal));
            EXPECT_TRUE(sh.first_slice_segment_in_pic_flag);
            EXPECT_EQ(sh.slice_type, SliceType::I);
            EXPECT_EQ(sh.slice_pic_order_cnt_lsb, 0u);
            EXPECT_EQ(sh.SliceQpY, 27);
            EXPECT_FALSE(sh.dependent_slice_segment_flag);
            break;
        }
    }
}

TEST(SliceHeader, ParsePSlice) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("p_qcif_10f.265"), mgr, nals));

    int p_count = 0;
    for (const auto& nal : nals) {
        if (is_vcl(nal.header.nal_unit_type)) {
            SliceHeader sh;
            ASSERT_TRUE(mgr.parse_slice_header(sh, nal));
            if (sh.slice_type == SliceType::P) {
                p_count++;
                EXPECT_GT(sh.slice_pic_order_cnt_lsb, 0u);
                EXPECT_GE(sh.MaxNumMergeCand, 1);
                EXPECT_LE(sh.MaxNumMergeCand, 5);
            }
        }
    }
    EXPECT_GT(p_count, 0);
}

TEST(SliceHeader, ParseAllSlicesB) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("b_qcif_10f.265"), mgr, nals));

    int slice_count = 0;
    for (const auto& nal : nals) {
        if (is_vcl(nal.header.nal_unit_type)) {
            SliceHeader sh;
            ASSERT_TRUE(mgr.parse_slice_header(sh, nal));
            slice_count++;
        }
    }
    EXPECT_EQ(slice_count, 10);
}

TEST(SliceHeader, ParseAllSlices1080p) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("bbb1080_50f.265"), mgr, nals));

    int slice_count = 0;
    for (const auto& nal : nals) {
        if (is_vcl(nal.header.nal_unit_type)) {
            SliceHeader sh;
            ASSERT_TRUE(mgr.parse_slice_header(sh, nal));
            slice_count++;
            if (slice_count == 1) {
                EXPECT_EQ(sh.num_entry_point_offsets, 33u);
            }
        }
    }
    EXPECT_EQ(slice_count, 50);
}

TEST(SliceHeader, ParseAllSlices4K) {
    ParameterSetManager mgr;
    std::vector<NalUnit> nals;
    ASSERT_TRUE(setup_from_file(fixture("bbb4k_25f.265"), mgr, nals));

    int slice_count = 0;
    for (const auto& nal : nals) {
        if (is_vcl(nal.header.nal_unit_type)) {
            SliceHeader sh;
            ASSERT_TRUE(mgr.parse_slice_header(sh, nal));
            slice_count++;
        }
    }
    EXPECT_EQ(slice_count, 25);
}

// ---- ScalingListData tests ----

TEST(ScalingListData, DefaultValues) {
    ScalingListData sld;
    sld.set_defaults();

    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(sld.scaling_list[0][0][i], 16);
    }
    EXPECT_EQ(sld.scaling_list[1][0][0], 16);
    EXPECT_EQ(sld.scaling_list[1][0][63], 115);
    EXPECT_EQ(sld.scaling_list[1][3][63], 91);
    EXPECT_EQ(sld.scaling_list_dc[0][0], 16);
    EXPECT_EQ(sld.scaling_list_dc[1][0], 16);
}

// ---- Edge case conformance tests ----

TEST(SliceHeader, ParseConformanceEdgeCases) {
    const std::string bitstreams[] = {
        fixture("i_64x64_qp22.265"),
        fixture("i_64x64_deblock.265"),
        fixture("i_64x64_sao.265"),
        fixture("i_64x64_full.265"),
        fixture("full_qcif_10f.265"),
    };

    for (const auto& path : bitstreams) {
        ParameterSetManager mgr;
        std::vector<NalUnit> nals;
        if (!setup_from_file(path, mgr, nals)) {
            continue;
        }

        for (const auto& nal : nals) {
            if (is_vcl(nal.header.nal_unit_type)) {
                SliceHeader sh;
                EXPECT_TRUE(mgr.parse_slice_header(sh, nal))
                    << "Failed to parse slice in " << path;
            }
        }
    }
}
