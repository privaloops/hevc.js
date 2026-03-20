#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "decoder/decoder.h"

static void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s [options] input.265\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --dump-nals      List all NAL units\n");
    fprintf(stderr, "  --dump-headers   Dump parameter sets and slice headers\n");
    fprintf(stderr, "  -o output.yuv    Decode and write YUV output\n");
    fprintf(stderr, "  -h, --help       Show this help\n");
}

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        exit(1);
    }
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* input_path = nullptr;
    const char* output_path = nullptr;
    bool dump_nals = false;
    bool dump_headers = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-nals") == 0) {
            dump_nals = true;
        } else if (strcmp(argv[i], "--dump-headers") == 0) {
            dump_headers = true;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            input_path = argv[i];
        }
    }

    if (!input_path) {
        fprintf(stderr, "Error: no input file specified\n");
        return 1;
    }

    auto data = read_file(input_path);
    printf("Read %zu bytes from %s\n", data.size(), input_path);

    // Create decoder and feed entire file
    hevc::Decoder decoder;
    decoder.feed(data.data(), data.size());

    // TODO: NAL unit parsing (Phase 2)
    if (dump_nals) {
        printf("--dump-nals: not yet implemented\n");
    }

    if (dump_headers) {
        printf("--dump-headers: not yet implemented\n");
    }

    // Decode pipeline: feed -> get_frame -> write
    if (output_path) {
        decoder.flush();

        const hevc::Picture* frame = decoder.get_frame();
        if (!frame) {
            fprintf(stderr, "Decoding to YUV not yet implemented\n");
            return 2;  // Exit code 2 = SKIP for oracle tests
        }

        int frame_count = 0;
        while (frame) {
            if (frame_count == 0) {
                if (!frame->write_yuv(output_path)) {
                    fprintf(stderr, "Error: cannot write to '%s'\n", output_path);
                    return 1;
                }
            }
            // TODO: append mode for multi-frame output
            frame_count++;
            frame = decoder.get_frame();
        }

        printf("Decoded %d frames\n", frame_count);
    }

    return 0;
}
