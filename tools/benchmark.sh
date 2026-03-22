#!/bin/bash
# benchmark.sh — Measure decode performance across resolutions
#
# Usage: ./tools/benchmark.sh [decoder_path]
#
# Outputs a table comparable to libde265 benchmarks.

set -e

DECODER="${1:-./build-rel/hevc-decode}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DEMO_DIR="$PROJECT_DIR/demo"

if [ ! -x "$DECODER" ]; then
    echo "Decoder not found at $DECODER"
    echo "Build with: cmake -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel"
    exit 1
fi

echo ""
echo "hevc-decode benchmark"
echo "====================="
echo "Decoder: $DECODER"
echo "Date:    $(date '+%Y-%m-%d %H:%M')"
echo ""

printf "%-14s %8s %10s %10s %10s\n" "Resolution" "Frames" "Time (ms)" "FPS" "ms/frame"
printf "%-14s %8s %10s %10s %10s\n" "-----------" "------" "---------" "-------" "--------"

for STREAM in \
    "$DEMO_DIR/bbb720_singleslice.265:1280x720" \
    "$DEMO_DIR/bbb1080_singleslice.265:1920x1080" \
    "$DEMO_DIR/bbb4k_singleslice.265:3840x2160"; do

    FILE="${STREAM%%:*}"
    RES="${STREAM##*:}"

    if [ ! -f "$FILE" ]; then
        printf "%-14s %8s %10s %10s %10s\n" "$RES" "—" "—" "—" "(missing)"
        continue
    fi

    # Run 3 times, take the best
    BEST_LINE=""
    BEST_FPS_VAL=0
    for run in 1 2 3; do
        OUTPUT=$("$DECODER" "$FILE" -o /dev/null 2>/dev/null)
        LINE=$(echo "$OUTPUT" | grep "pictures in")
        # Extract fps value for comparison (use awk to handle locale)
        CUR_FPS=$(echo "$LINE" | sed 's/.*(\([0-9.]*\) fps.*/\1/')
        # Compare as strings via awk (locale-safe)
        if echo "$CUR_FPS $BEST_FPS_VAL" | awk '{exit !($1 > $2)}'; then
            BEST_FPS_VAL=$CUR_FPS
            BEST_LINE=$LINE
        fi
    done

    # Parse the best line: "Decoded N pictures in X.Xms (Y.Y fps, Z.Z ms/frame)"
    FRAMES=$(echo "$BEST_LINE" | sed 's/Decoded \([0-9]*\).*/\1/')
    TOTAL_MS=$(echo "$BEST_LINE" | sed 's/.*in \([0-9.]*\)ms.*/\1/')
    FPS=$(echo "$BEST_LINE" | sed 's/.*(\([0-9.]*\) fps.*/\1/')
    MSFRAME=$(echo "$BEST_LINE" | sed 's/.*, \([0-9.]*\) ms\/frame.*/\1/')

    printf "%-14s %8s %10s %10s %10s\n" "$RES" "$FRAMES" "${TOTAL_MS}" "${FPS}" "${MSFRAME}"
done

echo ""
echo "Notes:"
echo "  - Single-slice, no WPP, Main profile 8-bit 4:2:0"
echo "  - Best of 3 runs, single-threaded"
echo "  - Source: Big Buck Bunny, x265 medium preset, QP 26-28"
echo ""
