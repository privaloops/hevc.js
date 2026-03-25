#!/bin/bash
# Generate HEVC DASH test streams (animated mires + audio)
# Usage: ./tools/gen_dash_streams.sh
# Output: demo/streams/dash_{720p,1080p,4k}/

set -e
STREAMS="demo/streams"

gen() {
  local name=$1 w=$2 h=$3 dur=$4 seg=$5
  local dir="$STREAMS/dash_${name}"
  rm -rf "$dir" && mkdir -p "$dir"

  ffmpeg -y \
    -f lavfi -i "testsrc2=size=${w}x${h}:rate=25:duration=${dur}" \
    -f lavfi -i "sine=frequency=1000:sample_rate=48000:duration=${dur}" \
    -c:v libx265 -preset fast -x265-params "keyint=25:min-keyint=25" -pix_fmt yuv420p \
    -c:a aac -b:a 128k \
    -f dash -seg_duration "$seg" \
    -adaptation_sets "id=0,streams=v id=1,streams=a" \
    -init_seg_name 'init_$RepresentationID$.mp4' \
    -media_seg_name 'seg_$RepresentationID$_$Number%03d$.m4s' \
    "$dir/manifest.mpd" 2>&1 | tail -3

  echo "✓ $name: ${w}x${h}, ${dur}s, seg=${seg}s → $dir/"
}

gen 720p  1280  720 10 2
gen 1080p 1920 1080  5 1
gen 4k    3840 2160  5 1

echo "Done. Serve with: npx serve demo/"
