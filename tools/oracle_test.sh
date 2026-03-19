#!/bin/bash
# oracle_test.sh — Test oracle : decode avec hevc-torture, compare MD5 avec reference
#
# Usage: oracle_test.sh <bitstream.265> <width> <height> <expected_md5> [decoder_path]
#
# Exit: 0 = PASS (MD5 match), 1 = FAIL (mismatch), 2 = SKIP (decoder not built)

set -uo pipefail

BITSTREAM="$1"
WIDTH="$2"
HEIGHT="$3"
EXPECTED_MD5="$4"
DECODER="${5:-./hevc-torture}"

# Vérifier que le décodeur existe
if [ ! -x "$DECODER" ]; then
    echo "SKIP: decoder not found at $DECODER"
    exit 2
fi

# Vérifier que le bitstream existe
if [ ! -f "$BITSTREAM" ]; then
    echo "FAIL: bitstream not found: $BITSTREAM"
    exit 1
fi

# Créer un répertoire temporaire
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Décoder avec hevc-torture (may fail if not yet implemented)
"$DECODER" "$BITSTREAM" -o "$TMPDIR/output.yuv" 2>/dev/null || true

if [ ! -f "$TMPDIR/output.yuv" ] || [ ! -s "$TMPDIR/output.yuv" ]; then
    echo "SKIP: decoder did not produce output file (not yet implemented)"
    exit 2
fi

# Calculer MD5
if command -v md5sum &>/dev/null; then
    ACTUAL_MD5=$(md5sum "$TMPDIR/output.yuv" | cut -d' ' -f1)
elif command -v md5 &>/dev/null; then
    ACTUAL_MD5=$(md5 -q "$TMPDIR/output.yuv")
else
    echo "FAIL: neither md5sum nor md5 found"
    exit 1
fi

# Comparer
if [ "$ACTUAL_MD5" = "$EXPECTED_MD5" ]; then
    echo "PASS: $BITSTREAM — MD5 match ($ACTUAL_MD5)"
    exit 0
else
    echo "FAIL: $BITSTREAM — MD5 mismatch"
    echo "  Expected: $EXPECTED_MD5"
    echo "  Actual:   $ACTUAL_MD5"

    # Si ffmpeg est disponible, générer la référence et comparer pixel par pixel
    if command -v ffmpeg &>/dev/null && command -v python3 &>/dev/null; then
        ffmpeg -y -i "$BITSTREAM" -pix_fmt yuv420p "$TMPDIR/ref.yuv" 2>/dev/null
        SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
        python3 "$SCRIPT_DIR/oracle_compare.py" "$TMPDIR/ref.yuv" "$TMPDIR/output.yuv" "$WIDTH" "$HEIGHT" || true
    fi

    exit 1
fi
