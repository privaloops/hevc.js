#include "decoding/decoder.h"
#include "bitstream/nal_unit.h"
#include "bitstream/bitstream_reader.h"
#include "filters/deblocking.h"
#include "filters/sao.h"
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

    // §8.3.1 — POC derivation
    int32_t poc = dpb_.derive_poc(sh, *sps, nal.header.nal_unit_type,
                                   nal.header.TemporalId());

    // Allocate picture in DPB
    ChromaFormat fmt = static_cast<ChromaFormat>(sps->chroma_format_idc);
    Picture* pic = dpb_.alloc_picture(
        static_cast<int>(sps->pic_width_in_luma_samples),
        static_cast<int>(sps->pic_height_in_luma_samples),
        fmt, sps->BitDepthY, sps->BitDepthC);
    pic->poc = poc;
    pic->needed_for_output = sh.pic_output_flag;

    // §8.1: IRAP with NoRaslOutputFlag starts a new CVS
    if (is_irap(nal.header.nal_unit_type)) {
        bool isIDR = (nal.header.nal_unit_type == NalUnitType::IDR_W_RADL ||
                      nal.header.nal_unit_type == NalUnitType::IDR_N_LP);
        bool isBLA = (nal.header.nal_unit_type == NalUnitType::BLA_W_LP ||
                      nal.header.nal_unit_type == NalUnitType::BLA_W_RADL ||
                      nal.header.nal_unit_type == NalUnitType::BLA_N_LP);
        if (isIDR || isBLA)
            cvs_id_++;
    }
    pic->cvs_id = cvs_id_;

    // Set conformance window
    if (sps->conformance_window_flag) {
        pic->conf_win_left = sps->conf_win_left_offset * sps->SubWidthC;
        pic->conf_win_right = sps->conf_win_right_offset * sps->SubWidthC;
        pic->conf_win_top = sps->conf_win_top_offset * sps->SubHeightC;
        pic->conf_win_bottom = sps->conf_win_bottom_offset * sps->SubHeightC;
    }

    // §8.3.2 — RPS derivation and picture marking
    dpb_.derive_rps(sh, *sps, nal.header.nal_unit_type, poc);

    // §8.3.4 — Reference picture list construction (P and B slices)
    if (sh.slice_type != SliceType::I) {
        dpb_.construct_ref_pic_lists(sh, *sps, *pps);
        // §8.3.5 — Collocated picture derivation
        dpb_.derive_colpic(sh);
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

    // Motion info grid at min-PU (4x4) granularity for inter MV storage
    motion_info_buf_.resize(modeGridW * modeGridH);
    std::fill(motion_info_buf_.begin(), motion_info_buf_.end(), PUMotionInfo{});
    // Allocate compact motion info in the Picture itself (for TMVP by future frames)
    pic->motion_info_buf.resize(modeGridW * modeGridH);
    std::fill(pic->motion_info_buf.begin(), pic->motion_info_buf.end(), Picture::PUMotionInfoCompact{});
    pic->motion_info_stride = modeGridW;

    // Phase 6: filter grids at min-TB (4x4) granularity
    cbf_luma_buf_.resize(modeGridW * modeGridH, 0);
    std::fill(cbf_luma_buf_.begin(), cbf_luma_buf_.end(), 0);
    log2_tu_size_buf_.resize(modeGridW * modeGridH, sps->CtbLog2SizeY);
    std::fill(log2_tu_size_buf_.begin(), log2_tu_size_buf_.end(),
              static_cast<uint8_t>(sps->CtbLog2SizeY));
    edge_flags_v_buf_.resize(modeGridW * modeGridH, 0);
    std::fill(edge_flags_v_buf_.begin(), edge_flags_v_buf_.end(), 0);
    edge_flags_h_buf_.resize(modeGridW * modeGridH, 0);
    std::fill(edge_flags_h_buf_.begin(), edge_flags_h_buf_.end(), 0);

    // Phase 6: SAO params per CTU
    int ctbGridSize = sps->PicWidthInCtbsY * sps->PicHeightInCtbsY;
    sao_params_buf_.resize(ctbGridSize);
    std::fill(sao_params_buf_.begin(), sao_params_buf_.end(), DecodingContext::SaoParams{});

    // Setup decoding context
    CabacEngine cabac;
    DecodingContext ctx;
    ctx.sps = sps;
    ctx.pps = pps;
    ctx.sh = &sh;
    ctx.cabac = &cabac;
    ctx.pic = pic;
    ctx.dpb = &dpb_;
    ctx.cu_info = cu_info_buf_.data();
    ctx.cu_info_stride = cuGridW;
    ctx.intra_pred_mode_y = intra_mode_buf_.data();
    ctx.intra_pred_mode_c = chroma_mode_buf_.data();
    ctx.intra_pred_mode_stride = modeGridW;
    ctx.motion_info = motion_info_buf_.data();
    ctx.motion_info_stride = modeGridW;
    ctx.cbf_luma_grid = cbf_luma_buf_.data();
    ctx.log2_tu_size_grid = log2_tu_size_buf_.data();
    ctx.edge_flags_v = edge_flags_v_buf_.data();
    ctx.edge_flags_h = edge_flags_h_buf_.data();
    ctx.filter_grid_stride = modeGridW;
    ctx.sao_params = sao_params_buf_.data();
    ctx.sao_params_stride = sps->PicWidthInCtbsY;
    ctx.sao_backup = sao_backup_;

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

    // §8.7: In-loop filters — deblocking then SAO
    apply_deblocking(ctx);
    apply_sao(ctx);

    // Store motion info in the Picture for TMVP access by future frames
    for (int i = 0; i < modeGridW * modeGridH; i++) {
        auto& src = motion_info_buf_[i];
        auto& dst = pic->motion_info_buf[i];
        dst.mv_x[0] = src.mv[0].x; dst.mv_y[0] = src.mv[0].y;
        dst.mv_x[1] = src.mv[1].x; dst.mv_y[1] = src.mv[1].y;
        dst.ref_idx[0] = src.ref_idx[0]; dst.ref_idx[1] = src.ref_idx[1];
        dst.pred_flag[0] = src.pred_flag[0]; dst.pred_flag[1] = src.pred_flag[1];
    }
    // Store ref POC lists for TMVP MV scaling
    pic->ref_poc[0].clear();
    pic->ref_poc[1].clear();
    for (int i = 0; i < dpb_.num_ref_list0(); i++) {
        auto* ref = dpb_.ref_pic_list0(i);
        pic->ref_poc[0].push_back(ref ? ref->poc : 0);
    }
    for (int i = 0; i < dpb_.num_ref_list1(); i++) {
        auto* ref = dpb_.ref_pic_list1(i);
        pic->ref_poc[1].push_back(ref ? ref->poc : 0);
    }

    // §8.1 step 4: mark current picture as short-term reference
    dpb_.mark_current_as_short_term_ref();

    // §C.5.2.3: mark current picture as "needed for output"
    // (PicOutputFlag is 1 for all pictures in Main profile)
    if (dpb_.current_pic()) {
        dpb_.current_pic()->needed_for_output = true;
    }

    return DecodeStatus::OK;
}

DecodeStatus Decoder::feed(const uint8_t* data, size_t size) {
    return decode(data, size);
}

std::vector<Picture*> Decoder::drain() {
    const SPS* sps = ps_mgr_.active_sps();
    if (!sps) return {};
    return dpb_.drain(*sps);
}

std::vector<Picture*> Decoder::flush() {
    return dpb_.flush();
}

std::vector<Picture*> Decoder::output_pictures() {
    // Collect all pictures from the DPB, sort by CVS then POC
    std::vector<Picture*> out;
    for (auto& pic : dpb_.pictures()) {
        out.push_back(pic.get());
    }
    std::sort(out.begin(), out.end(),
              [](const Picture* a, const Picture* b) {
                  if (a->cvs_id != b->cvs_id) return a->cvs_id < b->cvs_id;
                  return a->poc < b->poc;
              });
    return out;
}

} // namespace hevc
