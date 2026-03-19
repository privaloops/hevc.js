#pragma once

// HEVC_DEBUG logging infrastructure
//
// Usage:
//   HEVC_LOG(CABAC, "decode_decision ctxIdx=%d bin=%d", ctx_idx, bin_val);
//   HEVC_LOG(INTRA, "mode=%d block=%dx%d", mode, w, h);
//
// Enable at build: cmake -DHEVC_DEBUG=ON (or Debug build type)
// Filter at runtime: set HEVC_DEBUG_FILTER env var (comma-separated categories)
//   export HEVC_DEBUG_FILTER="CABAC,INTRA"
//   If unset, all categories are logged.

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hevc {

enum class DebugCategory : uint8_t {
    BITSTREAM = 0,
    NAL,
    PARSE,
    CABAC,
    TREE,
    INTRA,
    INTER,
    TRANSFORM,
    QUANT,
    FILTER,
    RECON,
    DPB,
    CATEGORY_COUNT
};

inline const char* debug_category_name(DebugCategory cat) {
    static const char* names[] = {
        "BITSTREAM", "NAL", "PARSE", "CABAC", "TREE",
        "INTRA", "INTER", "TRANSFORM", "QUANT", "FILTER",
        "RECON", "DPB"
    };
    return names[static_cast<int>(cat)];
}

#ifdef HEVC_DEBUG

inline bool debug_category_enabled(DebugCategory cat) {
    // Lazy init: parse HEVC_DEBUG_FILTER once
    static bool initialized = false;
    static bool all_enabled = true;
    static bool enabled[static_cast<int>(DebugCategory::CATEGORY_COUNT)] = {};

    if (!initialized) {
        initialized = true;
        const char* filter = getenv("HEVC_DEBUG_FILTER");
        if (filter && filter[0] != '\0') {
            all_enabled = false;
            // Parse comma-separated list
            char buf[256];
            strncpy(buf, filter, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            char* tok = strtok(buf, ",");
            while (tok) {
                for (int i = 0; i < static_cast<int>(DebugCategory::CATEGORY_COUNT); i++) {
                    if (strcmp(tok, debug_category_name(static_cast<DebugCategory>(i))) == 0) {
                        enabled[i] = true;
                    }
                }
                tok = strtok(nullptr, ",");
            }
        }
    }

    return all_enabled || enabled[static_cast<int>(cat)];
}

#define HEVC_LOG(category, fmt, ...) \
    do { \
        if (::hevc::debug_category_enabled(::hevc::DebugCategory::category)) { \
            fprintf(stderr, "[%s] " fmt "\n", \
                ::hevc::debug_category_name(::hevc::DebugCategory::category), \
                ##__VA_ARGS__); \
        } \
    } while (0)

#else

#define HEVC_LOG(category, fmt, ...) ((void)0)

#endif

} // namespace hevc
