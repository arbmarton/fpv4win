#!/bin/sh
# Decoder and renderer tests for fpv4win (macOS / VideoToolbox).
#
#   cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DFPV4WIN_BUILD_TESTS=ON
#   cmake --build build-mac -j8
#   tests/run_tests.sh build-mac
#
# Needs the ffmpeg CLI (brew install ffmpeg) to generate clips and to act as an RTP sender.
# File tests compare every VideoToolbox frame against a software decode (must be bit-exact).
# RTP tests mirror the app's real input path: SDP file + RTP over UDP on 127.0.0.1, joining
# mid-GOP, with and without simulated packet loss.
set -u
BUILD=${1:-build-mac}
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=$(cd "$BUILD" && pwd)
[ -x "$BIN/tests/test_decoder" ] && BIN="$BIN/tests"
W=${TMPDIR:-/tmp}/fpv4win-tests
mkdir -p "$W/clips"
cd "$W"
fail=0
check() { if "$@"; then echo "ok"; else echo "FAILED: $*"; fail=1; fi; }

echo "## generating clips"
Q="-hide_banner -loglevel error -y"
[ -f clips/h264_720p.mp4 ] || ffmpeg $Q -f lavfi -i testsrc2=size=1280x720:rate=25 -t 3 -c:v libx264 -preset veryfast -tune zerolatency -x264-params keyint=25 -pix_fmt yuv420p clips/h264_720p.mp4
[ -f clips/h265_720p.mp4 ] || ffmpeg $Q -f lavfi -i testsrc2=size=1280x720:rate=25 -t 3 -c:v libx265 -preset veryfast -tune zerolatency -x265-params keyint=25:log-level=error -pix_fmt yuv420p clips/h265_720p.mp4
[ -f clips/h264_1080p_bframes.mp4 ] || ffmpeg $Q -f lavfi -i testsrc2=size=1920x1080:rate=30 -t 2 -c:v libx264 -preset veryfast -bf 2 -pix_fmt yuv420p clips/h264_1080p_bframes.mp4
[ -f clips/h265_720p_10bit.mp4 ] || ffmpeg $Q -f lavfi -i testsrc2=size=1280x720:rate=25 -t 2 -c:v libx265 -preset veryfast -x265-params keyint=25:log-level=error -pix_fmt yuv420p10le clips/h265_720p_10bit.mp4
[ -f clips/vp8_360p.webm ] || ffmpeg $Q -f lavfi -i testsrc2=size=640x360:rate=25 -t 2 -c:v libvpx -deadline realtime -cpu-used 8 clips/vp8_360p.webm
# MPEG-TS repeats SPS/PPS before every IDR, like the in-band parameter sets OpenIPC sends.
[ -f clips/h264_720p.ts ] || ffmpeg $Q -i clips/h264_720p.mp4 -c copy -f mpegts clips/h264_720p.ts
[ -f clips/h265_720p.ts ] || ffmpeg $Q -i clips/h265_720p.mp4 -c copy -f mpegts clips/h265_720p.ts

echo "## file decode (VideoToolbox vs software reference)"
check timeout 60 "$BIN/test_decoder" clips/h264_720p.mp4 --min-frames 70
check timeout 60 "$BIN/test_decoder" clips/h265_720p.mp4 --min-frames 70
check timeout 60 "$BIN/test_decoder" clips/h264_1080p_bframes.mp4 --min-frames 55
check timeout 60 "$BIN/test_decoder" clips/h265_720p_10bit.mp4 --min-frames 45
check timeout 60 "$BIN/test_decoder" clips/vp8_360p.webm --min-frames 45 --expect-fallback
check timeout 60 "$BIN/test_decoder" clips/h264_720p.mp4 --min-frames 70 --no-hw

send() { nohup ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -i clips/$1 -c copy -f rtp -payload_type 97 "rtp://127.0.0.1:$2?pkt_size=1200" </dev/null >/dev/null 2>&1 & echo $!; }
relay() { nohup python3 "$HERE/lossy_relay.py" $1 $2 $3 </dev/null >/dev/null 2>&1 & echo $!; }
sdp() { printf 'v=0\no=- 0 0 IN IP4 127.0.0.1\ns=No Name\nc=IN IP4 127.0.0.1\nt=0 0\nm=video %s RTP/AVP 97\na=rtpmap:97 %s/90000\n' $2 $1 > $1.sdp; }
sdp H264 52356; sdp H265 52358
rtp() { codec=$1; clip=$2; port=$3; shift 3; SP=$(send $clip $port); sleep 1.3; check timeout 40 "$BIN/test_decoder" $codec.sdp --no-ref "$@"; kill $SP 2>/dev/null; }

echo "## live RTP via SDP (mid-GOP join)"
rtp H264 h264_720p.ts 52356 --max-frames 150 --min-frames 150
rtp H265 h265_720p.ts 52358 --max-frames 150 --min-frames 150
# VideoToolbox cannot conceal errors: after a hit it rejects pictures until the next IDR, so the
# frame yield under loss is far below the software decoder's. What must hold is that playback
# never stops and keeps resyncing on IDR frames (frames keep arriving over the 8 s window).
echo "## live RTP with 0.5% packet loss (must keep resyncing)"
RP=$(relay 52360 52356 0.005); rtp H264 h264_720p.ts 52360 --max-seconds 8 --min-frames 20; kill $RP
RP=$(relay 52362 52358 0.005); rtp H265 h265_720p.ts 52362 --max-seconds 8 --min-frames 20; kill $RP
echo "## live RTP with 3% packet loss (must keep running)"
RP=$(relay 52360 52356 0.03); rtp H264 h264_720p.ts 52360 --max-seconds 8 --min-frames 1; kill $RP
RP=$(relay 52362 52358 0.03); rtp H265 h265_720p.ts 52362 --max-seconds 8 --min-frames 1; kill $RP

if [ -x "$BIN/test_render" ]; then
  echo "## GL render over live RTP (opens a window, saves render_*.png)"
  SP=$(send h264_720p.ts 52356); sleep 1.3; check timeout 40 "$BIN/test_render" H264.sdp render_h264.png 4; kill $SP 2>/dev/null
  SP=$(send h265_720p.ts 52358); sleep 1.3; check timeout 40 "$BIN/test_render" H265.sdp render_h265.png 4; kill $SP 2>/dev/null
fi
[ $fail = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $fail
