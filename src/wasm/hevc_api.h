// HEVC Decoder C API — for embedding and WASM bindings
// Pure C interface wrapping the C++ Decoder

#ifndef HEVC_API_H
#define HEVC_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque decoder handle
typedef struct HEVCDecoder HEVCDecoder;

// Decoded frame — planes point into decoder-owned memory (valid until next decode call)
typedef struct {
    const uint16_t* y;     // Luma plane
    const uint16_t* cb;    // Chroma Cb plane
    const uint16_t* cr;    // Chroma Cr plane
    int width;             // Luma width (after conformance window crop)
    int height;            // Luma height (after conformance window crop)
    int stride_y;          // Luma stride (in samples)
    int stride_c;          // Chroma stride (in samples)
    int chroma_width;      // Chroma plane width
    int chroma_height;     // Chroma plane height
    int bit_depth;         // Bit depth (8 or 10)
    int poc;               // Picture Order Count (display order)
} HEVCFrame;

// Stream info — available after first frame is decoded
typedef struct {
    int width;             // Luma width
    int height;            // Luma height
    int bit_depth;         // Bit depth (8 or 10)
    int chroma_format;     // 0=mono, 1=4:2:0, 2=4:2:2, 3=4:4:4
    int profile;           // Profile IDC (1=Main, 2=Main10)
    int level;             // Level IDC (e.g. 93 = Level 3.1)
} HEVCStreamInfo;

// Return codes
#define HEVC_OK             0
#define HEVC_ERROR         -1
#define HEVC_NEED_MORE     -2

// Create a decoder instance
HEVCDecoder* hevc_decoder_create(void);

// Destroy a decoder instance
void hevc_decoder_destroy(HEVCDecoder* dec);

// Feed a complete bitstream and decode all pictures
// Returns HEVC_OK on success, HEVC_ERROR on failure
int hevc_decoder_decode(HEVCDecoder* dec, const uint8_t* data, size_t size);

// Get number of decoded frames available
int hevc_decoder_get_frame_count(HEVCDecoder* dec);

// Get a decoded frame by index (0-based, display order)
// Returns HEVC_OK and fills frame, or HEVC_ERROR if index out of range
// Frame data is valid until decoder is destroyed or next decode call
int hevc_decoder_get_frame(HEVCDecoder* dec, int index, HEVCFrame* frame);

// Get stream info (available after decode)
// Returns HEVC_OK on success, HEVC_ERROR if no stream decoded yet
int hevc_decoder_get_info(HEVCDecoder* dec, HEVCStreamInfo* info);

#ifdef __cplusplus
}
#endif

#endif // HEVC_API_H
