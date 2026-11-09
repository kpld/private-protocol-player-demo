#!/usr/bin/env bash
# Host-side functional tests for the KDSP protocol implementation
# (player/ffmpeg-patch/kdsp.c), run against the real Python server.
#
# The harness compiles the *unmodified* kdsp.c with minimal FFmpeg
# API stand-ins (stub/) and a POSIX-socket ffurl_* layer, so the frame
# parsing / resync / statistics logic can be verified on any Linux
# host — no Android device required. Three scenarios:
#
#   1. clean stream   -> payload output must be byte-identical to the
#                        source elementary stream; 0 dropped, 0 resyncs
#   2. --drop 0.5     -> sequence-gap counter must report dropped > 0,
#                        output must differ from the source stream
#   3. garbage prefix -> 33 junk bytes (ending in a partial "KD" magic
#                        bait) are injected before the stream; the
#                        receiver must resync (discarded == 33) and the
#                        output must equal the source stream prefix
#
# Requirements: gcc, python3, ffmpeg (only to generate the 3 s sample).
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)
WORK=$(mktemp -d)
trap 'pkill -f "^python3 kdsp_server.py" 2>/dev/null || true; pkill -f garbage_proxy.py 2>/dev/null || true' EXIT

echo "== generate 3 s sample =="
ffmpeg -y -v error -f lavfi -i "testsrc2=size=1280x720:rate=25:duration=3" \
  -c:v libx264 -preset veryfast -tune zerolatency \
  -x264-params "aud=1:keyint=50:min-keyint=50" \
  -pix_fmt yuv420p -f h264 "$WORK/sample.h264"
SIZE=$(stat -c %s "$WORK/sample.h264")

echo "== build harness (real kdsp.c, stub FFmpeg API) =="
cp "$ROOT/player/ffmpeg-patch/kdsp.c" "$ROOT/player/ffmpeg-patch/kdsp.h" "$WORK/"
gcc -Wall -Wextra -Wno-unused-parameter -I stub -I "$WORK" -o "$WORK/harness" harness.c "$WORK/kdsp.c"

serve() { # port, extra args...
  local port=$1; shift
  (cd "$ROOT/server" && python3 kdsp_server.py --file "$WORK/sample.h264" --port "$port" "$@" > "$WORK/server_$port.log" 2>&1 &)
  sleep 1
}

echo "== test 1: clean stream =="
serve 19361
timeout 30 "$WORK/harness" kdsp://127.0.0.1:19361 "$WORK/out1.h264" "$SIZE" > "$WORK/t1.log" 2>&1
cmp "$WORK/sample.h264" "$WORK/out1.h264"
grep -q "frames received 75, dropped 0, resyncs 0" "$WORK/t1.log"
echo "PASS: byte-identical, 75 frames, 0 dropped, 0 resyncs"

echo "== test 2: server --drop 0.5 =="
serve 19362 --drop 0.5
timeout 30 "$WORK/harness" kdsp://127.0.0.1:19362 "$WORK/out2.h264" "$SIZE" > "$WORK/t2.log" 2>&1
! cmp -s "$WORK/sample.h264" "$WORK/out2.h264"
grep -Eq "dropped [1-9][0-9]*" "$WORK/t2.log"
echo "PASS: $(grep -o 'frames received [0-9]*, dropped [0-9]*' "$WORK/t2.log")"

echo "== test 3: garbage prefix resync =="
python3 garbage_proxy.py > "$WORK/proxy.log" 2>&1 &   # listens on 19363, relays 19361
sleep 1
timeout 20 "$WORK/harness" kdsp://127.0.0.1:19363 "$WORK/out3.h264" 100000 > "$WORK/t3.log" 2>&1
grep -q "discarded 33 bytes" "$WORK/t3.log"
head -c 100000 "$WORK/sample.h264" | cmp - "$WORK/out3.h264"
echo "PASS: resync discarded exactly 33 bytes, output == sample prefix"

echo "ALL HOST TESTS PASSED"
