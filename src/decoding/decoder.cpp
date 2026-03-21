#include "decoding/decoder.h"
#include "bitstream/nal_unit.h"
#include "bitstream/bitstream_reader.h"
#include "common/debug.h"

#include <cstring>
#include <cstdio>

namespace hevc {

DecodeStatus Decoder::decode(const uint8_t* data, size_t size) {
    NalParser parser;
    auto nals = parser.parse(data, size);

    HEVC_LOG(PARSE, "Decoded %zu NAL units", nals.size());

    // Process NALs: parameter sets first, then decode each picture
    size_t i = 0;
    while (i < nals.size()) {
        auto type = nals[i].header.nal_unit_type;

        if (type == NalUnitType::VPS_NUT ||
            type == NalUnitType::SPS_NUT ||
            type == NalUnitType::PPS_NUT) {
            ps_mgr_.process_nal(nals[i]);
            i++;
        } else if (is_vcl(type)) {
            // Found start of a picture — decode it
            auto status = decode_picture(nals, i);
            if (status != DecodeStatus::OK) return status;

            // Skip remaining VCL NALs of this picture
            i++;
            while (i < nals.size() && is_vcl(nals[i].header.nal_unit_type)) {
                // Check if this is a new picture (first_slice_segment_in_pic_flag)
                // For now, treat consecutive VCL NALs as same picture until next param set
                BitstreamReader bs(nals[i].rbsp.data(), nals[i].rbsp.size());
                bool first_slice = bs.read_flag();
                if (first_slice) break; // New picture
                i++;
            }
        } else {
            // Non-VCL, non-parameter-set NAL (SEI, AUD, etc.) — skip
            i++;
        }
    }

    return DecodeStatus::OK;
}

DecodeStatus Decoder::decode_picture(const std::vector<NalUnit>& nals,
                                      size_t first_vcl_idx) {
    const auto& nal = nals[first_vcl_idx];

    // Parse slice header
    SliceHeader sh;
    if (!ps_mgr_.parse_slice_header(sh, nal)) {
        fprintf(stderr, "Error: failed to parse slice header\n");
        return DecodeStatus::ERROR;
    }

    const SPS* sps = ps_mgr_.active_sps();
    const PPS* pps = ps_mgr_.active_pps();
    if (!sps || !pps) {
        fprintf(stderr, "Error: no active SPS/PPS\n");
        return DecodeStatus::ERROR;
    }

    HEVC_LOG(PARSE, "Decoding picture: %dx%d type=%d QP=%d",
             sps->pic_width_in_luma_samples, sps->pic_height_in_luma_samples,
             static_cast<int>(sh.slice_type), sh.SliceQpY);

    // Allocate picture
    Picture pic;
    ChromaFormat fmt = static_cast<ChromaFormat>(sps->chroma_format_idc);
    pic.allocate(static_cast<int>(sps->pic_width_in_luma_samples),
                 static_cast<int>(sps->pic_height_in_luma_samples),
                 fmt, sps->BitDepthY, sps->BitDepthC);

    // Set conformance window
    if (sps->conformance_window_flag) {
        pic.conf_win_left = sps->conf_win_left_offset * sps->SubWidthC;
        pic.conf_win_right = sps->conf_win_right_offset * sps->SubWidthC;
        pic.conf_win_top = sps->conf_win_top_offset * sps->SubHeightC;
        pic.conf_win_bottom = sps->conf_win_bottom_offset * sps->SubHeightC;
    }

    // Allocate CU info grid (min-CB granularity)
    int cuGridW = sps->PicWidthInMinCbsY;
    int cuGridH = sps->PicHeightInMinCbsY;
    cu_info_buf_.resize(cuGridW * cuGridH);
    std::fill(cu_info_buf_.begin(), cu_info_buf_.end(), CUInfo{});

    // Intra mode grid uses min-TB granularity (4x4) to support NxN sub-PU
    int modeGridW = sps->pic_width_in_luma_samples / sps->MinTbSizeY;
    int modeGridH = sps->pic_height_in_luma_samples / sps->MinTbSizeY;
    intra_mode_buf_.resize(modeGridW * modeGridH);
    std::fill(intra_mode_buf_.begin(), intra_mode_buf_.end(), 1); // DC default
    chroma_mode_buf_.resize(modeGridW * modeGridH);
    std::fill(chroma_mode_buf_.begin(), chroma_mode_buf_.end(), 0); // Planar default

    // Setup decoding context
    CabacEngine cabac;
    DecodingContext ctx;
    ctx.sps = sps;
    ctx.pps = pps;
    ctx.sh = &sh;
    ctx.cabac = &cabac;
    ctx.pic = &pic;
    ctx.cu_info = cu_info_buf_.data();
    ctx.cu_info_stride = cuGridW;
    ctx.intra_pred_mode_y = intra_mode_buf_.data();
    ctx.intra_pred_mode_c = chroma_mode_buf_.data();
    ctx.intra_pred_mode_stride = modeGridW;

    // Create bitstream reader for slice data (RBSP already extracted by NalParser)
    BitstreamReader bs(nal.rbsp.data(), nal.rbsp.size());

    // Skip slice header (re-parse to advance the bitstream position)
    {
        SliceHeader sh_skip;
        sh_skip.parse(bs, *sps, *pps, nal.header.nal_unit_type,
                       nal.header.TemporalId());
    }

    // Byte align after slice header
    HEVC_LOG(PARSE, "Slice header consumed %zu bits, byte_aligned=%d",
             bs.bits_read(), bs.byte_aligned());
    if (!bs.byte_aligned())
        bs.byte_alignment();
    HEVC_LOG(PARSE, "Slice data starts at bit %zu (byte %zu), remaining=%zu bits",
             bs.bits_read(), bs.bits_read() / 8, bs.bits_remaining());

    // Decode slice data
    if (!decode_slice_segment_data(ctx, bs)) {
        fprintf(stderr, "Error: failed to decode slice data\n");
        return DecodeStatus::ERROR;
    }

    // Store decoded picture
    pictures_.push_back(std::move(pic));

    return DecodeStatus::OK;
}

} // namespace hevc
