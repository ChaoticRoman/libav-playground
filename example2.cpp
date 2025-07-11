extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}
#include <iostream>
#include <cstdlib>

static void checkError(int err, const char* msg) {
    if (err < 0) {
        char buf[128];
        av_strerror(err, buf, sizeof(buf));
        std::cerr << msg << " error=" << err << " (" << buf << ")\n";
        std::exit(1);
    }
}

int main(int argc, char* argv[]) {
    const char* outFilename = "out.mp4";
    const int width      = 320;
    const int height     = 240;
    const int fps        = 2;
    const int numFrames  = 20;  // Generates 10 seconds of video

    // 1) Allocate the output format context
    AVFormatContext* fmtCtx = nullptr;
    int ret = avformat_alloc_output_context2(&fmtCtx, nullptr, "mp4", outFilename);
    checkError(ret, "avformat_alloc_output_context2");

    // 2) Find and open the video encoder
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!codec) {
        std::cerr << "mpeg4 encoder not found\n";
        return 1;
    }
    AVStream* stream = avformat_new_stream(fmtCtx, nullptr);
    if (!stream) {
        std::cerr << "cannot create stream\n";
        return 1;
    }
    stream->id = fmtCtx->nb_streams - 1;

    AVCodecContext* cctx = avcodec_alloc_context3(codec);
    if (!cctx) {
        std::cerr << "could not alloc codec context\n";
        return 1;
    }
    // Encoder settings
    cctx->codec_id     = AV_CODEC_ID_MPEG4;
    cctx->bit_rate     = 400000;
    cctx->width        = width;
    cctx->height       = height;
    cctx->time_base    = AVRational{1, fps};
    cctx->framerate    = AVRational{fps, 1};
    cctx->gop_size     = 1;     // all frames keyframes
    cctx->max_b_frames = 0;     // no b-frames
    cctx->pix_fmt      = AV_PIX_FMT_YUV420P;

    // open codec
    ret = avcodec_open2(cctx, codec, nullptr);
    checkError(ret, "avcodec_open2");

    // copy codec parameters to stream
    ret = avcodec_parameters_from_context(stream->codecpar, cctx);
    checkError(ret, "avcodec_parameters_from_context");
    stream->time_base = cctx->time_base;

    // 3) Open the output IO
    if (!(fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmtCtx->pb, outFilename, AVIO_FLAG_WRITE);
        checkError(ret, "avio_open");
    }

    // 4) Set fragmentation flags
    AVDictionary* opts = nullptr;
    // "empty_moov" writes an empty MOOV atom at the front so player can start quickly
    // "frag_keyframe" fragments at every keyframe
    // "default_base_moof" makes the fragments self-contained
    av_dict_set(&opts, "movflags", "empty_moov+frag_keyframe+default_base_moof", 0);

    // 5) Write header
    ret = avformat_write_header(fmtCtx, &opts);
    checkError(ret, "avformat_write_header");

    // 6) Allocate a reusable frame and packet
    AVFrame* frame = av_frame_alloc();
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = nullptr;
    pkt.size = 0;

    frame->format = cctx->pix_fmt;
    frame->width  = cctx->width;
    frame->height = cctx->height;
    ret = av_frame_get_buffer(frame, 32);
    checkError(ret, "av_frame_get_buffer");

    // 7) Encode loop
    for (int i = 0; i < numFrames; ++i) {
        // make sure frame is writable
        ret = av_frame_make_writable(frame);
        checkError(ret, "av_frame_make_writable");

        // fill YUV with a simple pattern: Y ramps up, U/V = 128 (gray)
        uint8_t gray = uint8_t((i * 256 / numFrames) & 0xFF);
        // Y plane
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                frame->data[0][y * frame->linesize[0] + x] = gray;
            }
        }
        // U and V planes
        for (int y = 0; y < height/2; ++y) {
            for (int x = 0; x < width/2; ++x) {
                frame->data[1][y * frame->linesize[1] + x] = 128;
                frame->data[2][y * frame->linesize[2] + x] = 128;
            }
        }

        frame->pts = i;

        // send to encoder
        ret = avcodec_send_frame(cctx, frame);
        checkError(ret, "avcodec_send_frame");

        // receive all available packets
        while (ret >= 0) {
            ret = avcodec_receive_packet(cctx, &pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            checkError(ret, "avcodec_receive_packet");

            // rescale timestamps from codec to stream timebase
            av_packet_rescale_ts(&pkt, cctx->time_base, stream->time_base);
            pkt.stream_index = stream->index;
            // write
            ret = av_interleaved_write_frame(fmtCtx, &pkt);
            checkError(ret, "av_interleaved_write_frame");
            av_packet_unref(&pkt);
        }
    }

    // 8) Flush encoder
    ret = avcodec_send_frame(cctx, nullptr);
    checkError(ret, "avcodec_send_frame(flush)");
    while (ret >= 0) {
        ret = avcodec_receive_packet(cctx, &pkt);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
            break;
        checkError(ret, "avcodec_receive_packet(flush)");
        av_packet_rescale_ts(&pkt, cctx->time_base, stream->time_base);
        pkt.stream_index = stream->index;
        ret = av_interleaved_write_frame(fmtCtx, &pkt);
        checkError(ret, "av_interleaved_write_frame(flush)");
        av_packet_unref(&pkt);
    }

    // 9) Write trailer and cleanup
    av_write_trailer(fmtCtx);

    if (!(fmtCtx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&fmtCtx->pb);
    av_frame_free(&frame);
    avcodec_free_context(&cctx);
    avformat_free_context(fmtCtx);

    std::cout << "Wrote " << outFilename << "\n";
    return 0;
}
