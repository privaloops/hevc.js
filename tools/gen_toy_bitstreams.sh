#!/bin/bash
# gen_toy_bitstreams.sh — Generate ultra-simple HEVC bitstreams for unit testing
#
# These "toy" bitstreams are designed for step-by-step debugging:
# - 64x64 (1 CTU at default CTU size)
# - Single frame (I-only)
# - No filters (no deblocking, no SAO)
# - Various QP levels
#
# Requires: x265, ffmpeg
# Output: tests/conformance/fixtures/toy_*.265

set -euo pipefail

FIXTURES_DIR="$(cd "$(dirname "$0")/../tests/conformance/fixtures" && pwd)"
WORK=$(mktemp -d)
trap "rm -rf $WORK" EXIT

# Generate a 64x64 raw YUV420 frame (gradient)
python3 -c "
import sys
w, h = 64, 64
luma = bytes((y * w + x) % 256 for y in range(h) for x in range(w))
chroma = b'\x80' * (w // 2 * h // 2)
sys.stdout.buffer.write(luma + chroma + chroma)
" > "$WORK/input.yuv"

echo "=== Generating toy bitstreams (64x64, 1 CTU, I-only, no filters) ==="

encode() {
    local name="$1"
    shift
    local output="$FIXTURES_DIR/$name"

    echo "--- $name ---"

    x265 --input "$WORK/input.yuv" --input-res 64x64 --fps 1 --frames 1 \
        --no-deblock --no-sao --no-wpp --no-info --preset ultrafast --keyint 1 \
        "$@" -o "$output" 2>/dev/null

    if [ ! -s "$output" ]; then
        echo "  SKIP: encoding failed"
        return 1
    fi

    # Generate reference YUV and MD5
    ffmpeg -y -i "$output" -pix_fmt yuv420p "$WORK/ref.yuv" 2>/dev/null

    local MD5
    if command -v md5sum &>/dev/null; then
        MD5=$(md5sum "$WORK/ref.yuv" | cut -d' ' -f1)
    else
        MD5=$(md5 -q "$WORK/ref.yuv")
    fi

    echo "  Size: $(wc -c < "$output" | tr -d ' ') bytes"
    echo "  MD5:  $MD5"
}

encode "toy_qp30.265" --qp 30
encode "toy_qp45.265" --qp 45
encode "toy_qp10.265" --qp 10

echo ""
echo "=== Done ==="
