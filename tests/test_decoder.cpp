// Standalone harness for FFmpegDecoder: decodes an input with the app's decoder class
// (hardware path if available) and compares each frame's luma against a pure software decode.
#include "ffmpegDecode.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct RefFrame { int w, h; std::vector<uint8_t> y; };

static std::vector<RefFrame> swDecode(const std::string &path) {
    std::vector<RefFrame> out;
    AVFormatContext *fmt = nullptr;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "protocol_whitelist", "file,udp,rtp", 0);
    if (avformat_open_input(&fmt, path.c_str(), nullptr, &opts) < 0) return out;
    avformat_find_stream_info(fmt, nullptr);
    int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const AVCodec *codec = avcodec_find_decoder(fmt->streams[vs]->codecpar->codec_id);
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[vs]->codecpar);
    avcodec_open2(ctx, codec, nullptr);
    AVPacket *pkt = av_packet_alloc();
    AVFrame *f = av_frame_alloc();
    SwsContext *sws = nullptr;
    auto drain = [&]() {
        while (avcodec_receive_frame(ctx, f) == 0) {
            RefFrame r { f->width, f->height, {} };
            r.y.resize((size_t)f->width * f->height);
            // Reference luma as 8-bit yuv420p (converts 10-bit / 4:4:4 sources like the decoder does).
            sws = sws_getCachedContext(sws, f->width, f->height, (AVPixelFormat)f->format, f->width, f->height,
                                       AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
            uint8_t *dst[4] = { r.y.data(), nullptr, nullptr, nullptr };
            int dstStride[4] = { f->width, 0, 0, 0 };
            if (f->format == AV_PIX_FMT_YUV420P || f->format == AV_PIX_FMT_YUVJ420P) {
                for (int row = 0; row < f->height; row++)
                    memcpy(r.y.data() + (size_t)row * f->width, f->data[0] + (size_t)row * f->linesize[0], f->width);
            } else {
                std::vector<uint8_t> u((size_t)f->width * f->height / 4), v(u.size());
                dst[1] = u.data(); dst[2] = v.data(); dstStride[1] = dstStride[2] = f->width / 2;
                sws_scale(sws, f->data, f->linesize, 0, f->height, dst, dstStride);
            }
            out.push_back(std::move(r));
        }
    };
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vs && avcodec_send_packet(ctx, pkt) == 0) drain();
        av_packet_unref(pkt);
    }
    avcodec_send_packet(ctx, nullptr);
    drain();
    if (sws) sws_freeContext(sws);
    av_frame_free(&f); av_packet_free(&pkt); avcodec_free_context(&ctx); avformat_close_input(&fmt);
    return out;
}

static double psnrY(const RefFrame &r, const AVFrame *f) {
    if (r.w != f->width || r.h != f->height) return -1;
    double mse = 0;
    for (int row = 0; row < f->height; row++) {
        const uint8_t *a = r.y.data() + (size_t)row * f->width;
        const uint8_t *b = f->data[0] + (size_t)row * f->linesize[0];
        for (int x = 0; x < f->width; x++) { double d = (double)a[x] - b[x]; mse += d * d; }
    }
    mse /= (double)f->width * f->height;
    return mse == 0 ? 99.0 : 10 * log10(255.0 * 255.0 / mse);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <input> [--no-hw] [--min-frames N] [--no-ref]\n", argv[0]); return 2; }
    std::string input = argv[1];
    bool wantHw = true, useRef = true, expectFallback = false; int minFrames = 1, maxFrames = 0, maxSeconds = 0;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--no-hw") wantHw = false;
        else if (a == "--no-ref") useRef = false;
        else if (a == "--expect-fallback") expectFallback = true;
        else if (a == "--min-frames") minFrames = atoi(argv[++i]);
        else if (a == "--max-frames") maxFrames = atoi(argv[++i]);
        else if (a == "--max-seconds") maxSeconds = atoi(argv[++i]);
    }
    av_log_set_level(AV_LOG_WARNING);

    std::vector<RefFrame> ref;
    if (useRef) { ref = swDecode(input); printf("reference sw frames: %zu\n", ref.size()); }

    FFmpegDecoder dec;

    dec.EnableHwDecoder(wantHw);

    if (!dec.OpenInput(input)) { printf("FAIL: OpenInput\n"); return 1; }
    printf("opened: %dx%d fps=%.2f hasVideo=%d fmt=%d(%s)\n", dec.GetWidth(), dec.GetHeight(), dec.GetFps(),
           dec.HasVideo(), dec.GetVideoFrameFormat(), av_get_pix_fmt_name(dec.GetVideoFrameFormat()));

    printf("hw decoder enabled: %d\n", dec.IsHwDecoderEnabled());


    int frames = 0, badFmt = 0, mismatch = 0, refIdx = 0;
    double minPsnr = 1e9;
    std::string stopReason = "eof";
    auto t0 = std::chrono::steady_clock::now();
    while (true) {
        if (maxSeconds && std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count() >= maxSeconds) { stopReason = "max seconds reached"; break; }
        std::shared_ptr<AVFrame> f;
        try { f = dec.GetNextFrame(); }
        catch (const std::exception &e) { stopReason = e.what(); break; }
        if (!f) continue;
        frames++;
        if (f->format != dec.GetVideoFrameFormat() || !f->data[0] || f->width <= 0) badFmt++;
        if (useRef && refIdx < (int)ref.size()) {
            double p = psnrY(ref[refIdx++], f.get());
            if (p < 0) mismatch++; else if (p < minPsnr) minPsnr = p;
        }
        if (maxFrames && frames >= maxFrames) { stopReason = "max frames reached"; break; }
        if (frames == 1)
            printf("first frame: %dx%d fmt=%s linesize=[%d,%d,%d] pts=%lld\n", f->width, f->height,
                   av_get_pix_fmt_name((AVPixelFormat)f->format), f->linesize[0], f->linesize[1], f->linesize[2],
                   (long long)f->pts);
    }
    printf("decoded frames: %d  dropped packets: %llu  stop: %s\n", frames, (unsigned long long)dec.GetDroppedPacketCount(), stopReason.c_str());
    bool eof = stopReason.find("End of file") != std::string::npos || stopReason.find("Immediate exit") != std::string::npos || stopReason == "max frames reached" || stopReason == "max seconds reached";
    bool ok = eof && frames >= minFrames && badFmt == 0 && mismatch == 0;

    if ((wantHw && !expectFallback) != dec.IsHwDecoderEnabled()) { printf("hw decoder state %d != expected %d\n", dec.IsHwDecoderEnabled(), wantHw && !expectFallback); ok = false; }

    if (useRef) {
        printf("min luma PSNR vs software decode: %.2f dB (%d compared)\n", minPsnr, refIdx);
        // VideoToolbox and the software decoder are both conformant so output should be (near) bit-exact.
        if (refIdx == 0 || minPsnr < 45.0) ok = false;
        if (!ref.empty() && frames < (int)ref.size()) printf("NOTE: hw path produced %d frames, sw reference %zu\n", frames, ref.size());
    }
    if (badFmt) printf("frames with unexpected format/empty data: %d\n", badFmt);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
