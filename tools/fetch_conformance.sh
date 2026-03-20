#!/bin/bash
# fetch_conformance.sh — Generate edge-case conformance bitstreams per phase
#
# Usage: ./tools/fetch_conformance.sh [phase4|phase5|phase6|all]
#
# Requires: x265, ffmpeg (both available via homebrew)
#
# Generates bitstreams in tests/conformance/edge-cases/<phase>/
# Computes reference MD5 with ffmpeg
# Writes tests/conformance/edge-cases/manifest.cmake for CTest integration

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
EDGE_DIR="$PROJECT_DIR/tests/conformance/edge-cases"
MANIFEST="$EDGE_DIR/manifest.cmake"
TMP_YUV=""

# --- Helpers ---

die() { echo "ERROR: $*" >&2; exit 1; }

check_deps() {
    command -v x265 &>/dev/null || die "x265 not found (brew install x265)"
    command -v ffmpeg &>/dev/null || die "ffmpeg not found (brew install ffmpeg)"
    command -v python3 &>/dev/null || die "python3 not found"
}

# Generate a raw YUV420p test pattern
# Usage: gen_yuv <width> <height> <frames> <output.yuv>
gen_yuv() {
    local w=$1 h=$2 frames=$3 out=$4
    python3 -c "
import sys, struct, math, random
random.seed(42)
w, h, frames = $w, $h, $frames
for f in range(frames):
    # Luma: gradient + noise for varied coefficients
    for y in range(h):
        for x in range(w):
            base = int(128 + 60 * math.sin(x * 0.1 + f * 0.5) * math.cos(y * 0.15))
            noise = random.randint(-10, 10)
            sys.stdout.buffer.write(bytes([max(0, min(255, base + noise))]))
    # Chroma Cb
    for y in range(h // 2):
        for x in range(w // 2):
            val = int(128 + 30 * math.sin(x * 0.2 + f))
            sys.stdout.buffer.write(bytes([max(0, min(255, val))]))
    # Chroma Cr
    for y in range(h // 2):
        for x in range(w // 2):
            val = int(128 + 30 * math.cos(y * 0.2 + f))
            sys.stdout.buffer.write(bytes([max(0, min(255, val))]))
" > "$out"
}

# Encode with x265 and compute reference MD5
# Usage: encode_and_register <name> <yuv> <width> <height> <frames> <phase> <label> <x265_opts...>
encode_and_register() {
    local name=$1 yuv=$2 w=$3 h=$4 frames=$5 phase=$6 label=$7
    shift 7
    local outdir="$EDGE_DIR/$phase"
    local out265="$outdir/${name}.265"

    mkdir -p "$outdir"

    # Encode (redirect stderr to temp file for debug on failure)
    echo "  Encoding $name ($phase, $label)..."
    local x265_log
    x265_log=$(mktemp /tmp/x265_log_XXXXXX.txt)
    x265 --input "$yuv" --input-res "${w}x${h}" --fps 30 --frames "$frames" \
        --output "$out265" "$@" </dev/null 2>"$x265_log" || {
        echo "  WARN: x265 failed for $name:"
        grep -iE "error|warning" "$x265_log" | head -5 || true
        rm -f "$x265_log"
        return 0
    }
    rm -f "$x265_log"

    if [ ! -s "$out265" ]; then
        echo "  WARN: empty output for $name, skipping"
        return 0
    fi

    # Decode reference with ffmpeg
    local ref_yuv
    ref_yuv=$(mktemp /tmp/hevc_ref_XXXXXX.yuv)
    ffmpeg -y -i "$out265" -pix_fmt yuv420p "$ref_yuv" 2>/dev/null || {
        echo "  WARN: ffmpeg failed to decode $name, skipping"
        rm -f "$ref_yuv"
        return 0
    }

    # Compute MD5
    local md5
    if command -v md5sum &>/dev/null; then
        md5=$(md5sum "$ref_yuv" | cut -d' ' -f1)
    else
        md5=$(md5 -q "$ref_yuv")
    fi
    rm -f "$ref_yuv"

    # Register in manifest
    cat >> "$MANIFEST" <<CMAKE_EOF
add_test(NAME conf_${name}
    COMMAND \${ORACLE_SCRIPT} \${CONF_EDGE_DIR}/${phase}/${name}.265 ${w} ${h}
        ${md5} \$<TARGET_FILE:hevc-decode>)
set_tests_properties(conf_${name} PROPERTIES LABELS "oracle;conformance;${phase};${label}"
    SKIP_RETURN_CODE 2)

CMAKE_EOF

    echo "  OK: $name -> $md5"
}

# --- Phase 4: Intra edge cases ---

gen_phase4() {
    echo "=== Phase 4: Intra edge cases ==="
    local tmpdir
    tmpdir=$(mktemp -d)
    local yuv64="$tmpdir/yuv64.yuv"
    local yuv256="$tmpdir/yuv256.yuv"
    gen_yuv 64 64 1 "$yuv64"
    gen_yuv 256 256 1 "$yuv256"

    local common="--keyint 1 --no-wpp --no-info"

    # PCM blocks (piege: byte alignment + CABAC reset)
    # x265 doesn't have a direct --pcm flag, use lossless which can trigger PCM-like paths
    encode_and_register "i_lossless_64" "$yuv64" 64 64 1 phase4 pcm \
        $common --lossless --no-deblock --no-sao

    # Transform skip (piege: shift specifique, scan horizontal)
    # x265 enables transform-skip at --tune fastdecode or explicitly
    encode_and_register "i_tskip_64" "$yuv64" 64 64 1 phase4 tskip \
        $common --no-deblock --no-sao --qp 22 --tu-intra-depth 4

    # Scaling lists non-flat (piege: matrices 8x8+ par defaut != flat 16)
    encode_and_register "i_scaling_64" "$yuv64" 64 64 1 phase4 scaling \
        $common --no-deblock --no-sao --qp 22 --scaling-list default

    # QP tres bas = grands coefficients (piege: cRiceParam adaptatif)
    encode_and_register "i_qp4_64" "$yuv64" 64 64 1 phase4 lowqp \
        $common --no-deblock --no-sao --qp 4

    # QP tres haut = quasi-zero coefficients
    encode_and_register "i_qp50_64" "$yuv64" 64 64 1 phase4 highqp \
        $common --no-deblock --no-sao --qp 50

    # 256x256 = multiple CTUs, tous les modes intra sollicites
    # Strong intra smoothing pour les blocs 32x32
    encode_and_register "i_256x256_qp22" "$yuv256" 256 256 1 phase4 multi-ctu \
        $common --no-deblock --no-sao --qp 22 --preset slow

    # Constrained intra pred (piege: samples reference uniquement des blocs intra)
    encode_and_register "i_constrained_64" "$yuv64" 64 64 1 phase4 constrained \
        $common --no-deblock --no-sao --qp 22 --constrained-intra

    # CU depth max = petits blocs (stress residual_coding, small TUs)
    encode_and_register "i_smallblk_64" "$yuv64" 64 64 1 phase4 smallblocks \
        $common --no-deblock --no-sao --qp 30 -s 16 --min-cu-size 8 --preset ultrafast

    # Multiple slices (piege: CABAC state reset entre slices)
    # x265 requires WPP for multiple slices
    encode_and_register "i_multislice_256" "$yuv256" 256 256 1 phase4 multislice \
        --keyint 1 --no-info --no-deblock --no-sao --qp 22 --slices 4 --wpp

    rm -rf "$tmpdir"
    echo "Phase 4 done."
    echo ""
}

# --- Phase 5: Inter edge cases ---

gen_phase5() {
    echo "=== Phase 5: Inter edge cases ==="
    local tmpdir
    tmpdir=$(mktemp -d)
    local yuv176="$tmpdir/yuv176.yuv"
    local yuv256="$tmpdir/yuv256.yuv"
    gen_yuv 176 144 20 "$yuv176"
    gen_yuv 256 256 10 "$yuv256"

    local common="--no-wpp --no-info"

    # P-only avec weighted prediction (piege: weight table parsing)
    encode_and_register "p_weighted_qcif" "$yuv176" 176 144 20 phase5 weightpred \
        $common --no-deblock --no-sao --qp 22 --bframes 0 --weightp

    # B hierarchique profond (piege: MV scaling distances POC asymetriques)
    encode_and_register "b_hier_qcif" "$yuv176" 176 144 20 phase5 bhier \
        $common --no-deblock --no-sao --qp 22 --bframes 4 --b-pyramid

    # B-frames avec bi-pred weighted (piege: formule bi-pred weighted)
    encode_and_register "b_weighted_qcif" "$yuv176" 176 144 20 phase5 bweighted \
        $common --no-deblock --no-sao --qp 22 --bframes 3 --weightb

    # TMVP active (piege: candidat temporel, MV scaling)
    encode_and_register "b_tmvp_qcif" "$yuv176" 176 144 20 phase5 tmvp \
        $common --no-deblock --no-sao --qp 22 --bframes 3

    # AMP = asymmetric motion partitions (piege: part modes 2NxnU etc)
    encode_and_register "p_amp_256" "$yuv256" 256 256 10 phase5 amp \
        $common --no-deblock --no-sao --qp 22 --bframes 0 --amp --preset slow

    # CRA (piege: HandleCraAsBlaFlag, NoRaslOutputFlag)
    encode_and_register "b_cra_qcif" "$yuv176" 176 144 20 phase5 cra \
        $common --no-deblock --no-sao --qp 22 --bframes 3 --keyint 8 \
        --no-open-gop

    # Open GOP (CRA with RASL pictures)
    encode_and_register "b_opengop_qcif" "$yuv176" 176 144 20 phase5 opengop \
        $common --no-deblock --no-sao --qp 22 --bframes 3 --keyint 8 \
        --open-gop

    # cabac_init_flag permutation (piege: P/B table swap)
    # Using cabac-init-present-flag forces cabac_init_flag usage
    encode_and_register "b_cabacinit_qcif" "$yuv176" 176 144 20 phase5 cabacinit \
        $common --no-deblock --no-sao --qp 22 --bframes 3 --preset slow

    rm -rf "$tmpdir"
    echo "Phase 5 done."
    echo ""
}

# --- Phase 6: Loop filter edge cases ---

gen_phase6() {
    echo "=== Phase 6: Loop filter edge cases ==="
    local tmpdir
    tmpdir=$(mktemp -d)
    local yuv176="$tmpdir/yuv176.yuv"
    local yuv256="$tmpdir/yuv256.yuv"
    gen_yuv 176 144 10 "$yuv176"
    gen_yuv 256 256 5 "$yuv256"

    local common="--no-wpp --no-info"

    # Deblocking seul avec B-frames (piege: Bs bi-pred combinatoire)
    encode_and_register "b_deblock_qcif" "$yuv176" 176 144 10 phase6 deblock-bipred \
        $common --no-sao --qp 22 --bframes 3

    # SAO edge offset (4 classes)
    encode_and_register "i_sao_edge_256" "$yuv256" 256 256 5 phase6 sao-edge \
        $common --no-deblock --qp 28 --keyint 1 --sao

    # SAO band offset
    encode_and_register "i_sao_band_256" "$yuv256" 256 256 5 phase6 sao-band \
        $common --no-deblock --qp 35 --keyint 1 --sao

    # Full pipeline: deblocking + SAO + B-frames
    encode_and_register "b_full_qcif" "$yuv176" 176 144 10 phase6 full \
        $common --qp 22 --bframes 3

    # Multi-slice + deblocking cross-slice (piege: loop_filter_across_slices)
    # x265 requires WPP for multiple slices
    encode_and_register "b_xslice_256" "$yuv256" 256 256 5 phase6 cross-slice \
        --no-info --qp 22 --bframes 2 --slices 4 --wpp

    # QP variable per CU (piege: QP delta derivation dans deblocking)
    encode_and_register "b_aqmode_qcif" "$yuv176" 176 144 10 phase6 aqmode \
        $common --qp 22 --bframes 3 --aq-mode 2

    # Deblocking avec offsets beta/tc (piege: slice_beta_offset, slice_tc_offset)
    encode_and_register "b_dboffset_qcif" "$yuv176" 176 144 10 phase6 db-offset \
        $common --no-sao --qp 22 --bframes 3 --deblock -2:-2

    # Full pipeline preset slow (toutes les features RD on)
    encode_and_register "b_slow_qcif" "$yuv176" 176 144 10 phase6 full-slow \
        $common --qp 22 --bframes 3 --preset slow

    rm -rf "$tmpdir"
    echo "Phase 6 done."
    echo ""
}

# --- Main ---

main() {
    check_deps

    local phase="${1:-all}"

    # Init manifest header
    mkdir -p "$EDGE_DIR"

    # Write manifest header (append per phase, so we re-create only on 'all')
    if [ "$phase" = "all" ]; then
        cat > "$MANIFEST" <<'CMAKE_HEADER'
# Auto-generated by tools/fetch_conformance.sh — DO NOT EDIT
# Re-run the script to regenerate after x265/ffmpeg updates
#
# Usage in CMakeLists.txt:
#   if(EXISTS "${CMAKE_SOURCE_DIR}/tests/conformance/edge-cases/manifest.cmake")
#       include("${CMAKE_SOURCE_DIR}/tests/conformance/edge-cases/manifest.cmake")
#   endif()

set(CONF_EDGE_DIR "${CMAKE_SOURCE_DIR}/tests/conformance/edge-cases")

CMAKE_HEADER
    else
        # Single phase: preserve existing entries for other phases, rewrite this phase
        if [ -f "$MANIFEST" ]; then
            # Remove existing entries for this phase
            local tmp_manifest
            tmp_manifest=$(mktemp)
            grep -v ";${phase};" "$MANIFEST" > "$tmp_manifest" || true
            mv "$tmp_manifest" "$MANIFEST"
        else
            cat > "$MANIFEST" <<'CMAKE_HEADER'
# Auto-generated by tools/fetch_conformance.sh — DO NOT EDIT
set(CONF_EDGE_DIR "${CMAKE_SOURCE_DIR}/tests/conformance/edge-cases")

CMAKE_HEADER
        fi
    fi

    case "$phase" in
        phase4)  gen_phase4 ;;
        phase5)  gen_phase5 ;;
        phase6)  gen_phase6 ;;
        all)
            gen_phase4
            gen_phase5
            gen_phase6
            ;;
        *)
            die "Unknown phase: $phase (use phase4, phase5, phase6, or all)"
            ;;
    esac

    # Count generated tests
    local count
    count=$(grep -c "^add_test" "$MANIFEST" 2>/dev/null || echo 0)
    echo "=== Done: $count conformance tests registered in $MANIFEST ==="
    echo ""
    echo "To activate in CTest, rebuild:"
    echo "  cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build"
    echo "  cd build && ctest -L conformance --output-on-failure"
}

main "$@"
