#include "decoder/decoder.h"

namespace hevc {

DecodeStatus Decoder::feed(const uint8_t* data, size_t size) {
    if (!data || size == 0) return DecodeStatus::NEED_MORE_DATA;

    // Accumulate incoming data
    pending_data_.insert(pending_data_.end(), data, data + size);

    // TODO Phase 2: NAL unit parsing
    // - Scan for start codes (0x000001 or 0x00000001)
    // - Extract NAL units
    // - Route by nal_unit_type to VPS/SPS/PPS/slice parsers

    // TODO Phase 3: Parameter set & slice header parsing
    // TODO Phase 4+: Slice decoding (CABAC, prediction, transform, reconstruction)

    return DecodeStatus::NEED_MORE_DATA;
}

const Picture* Decoder::get_frame() {
    if (output_read_index_ >= output_queue_.size()) return nullptr;
    return &output_queue_[output_read_index_++];
}

DecodeStatus Decoder::flush() {
    // TODO: flush DPB, output remaining frames in display order
    pending_data_.clear();
    return DecodeStatus::OK;
}

bool Decoder::has_info() const {
    return active_sps() != nullptr;
}

int Decoder::width() const {
    auto* sps = active_sps();
    return sps ? static_cast<int>(sps->pic_width_in_luma_samples) : 0;
}

int Decoder::height() const {
    auto* sps = active_sps();
    return sps ? static_cast<int>(sps->pic_height_in_luma_samples) : 0;
}

int Decoder::bit_depth_luma() const {
    auto* sps = active_sps();
    return sps ? sps->BitDepthY : 8;
}

int Decoder::bit_depth_chroma() const {
    auto* sps = active_sps();
    return sps ? sps->BitDepthC : 8;
}

ChromaFormat Decoder::chroma_format() const {
    auto* sps = active_sps();
    return sps ? static_cast<ChromaFormat>(sps->chroma_format_idc) : ChromaFormat::YUV420;
}

const SPS* Decoder::active_sps() const {
    if (active_sps_id_ < 0 || active_sps_id_ >= 16) return nullptr;
    if (!sps_[active_sps_id_].has_value()) return nullptr;
    return &sps_[active_sps_id_].value();
}

} // namespace hevc
