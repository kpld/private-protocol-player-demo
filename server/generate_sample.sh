#!/usr/bin/env bash
# Generate the synthetic sample stream for the KDSP demo server.
# testsrc2 has a running timecode burned in, which makes latency and A/V
# behaviour easy to eyeball. No copyrighted material is involved.
# AUD NAL units are inserted (-x264-params aud=1) because kdsp_server.py
# packetises the elementary stream into access units at AUD boundaries.
set -euo pipefail
cd "$(dirname "$0")"

ffmpeg -y \
  -f lavfi -i "testsrc2=size=1280x720:rate=25:duration=60" \
  -c:v libx264 -preset veryfast -tune zerolatency \
  -x264-params "aud=1:keyint=50:min-keyint=50" \
  -pix_fmt yuv420p \
  -f h264 sample.h264

echo "Wrote sample.h264"
