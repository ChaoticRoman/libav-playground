// compile with:
// g++ -std=c++11 -o make2fps make2fps.cpp `pkg-config --cflags --libs libavformat libavcodec libavutil`

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

#include <iostream>

int main(int argc, char* argv[]) {
    const char* filename = "output.mp4";
    const int width  = 320;
    const int height = 240;
    const AVRational framerate  = { 2, 1 };  // 2 fps
    const AVRational time_base  = { 1, 2 };  // must match 1/framerate

    avformat_network_init();

    // 1) allocate format context
    AVFormatContext* oc = nullptr;
    avformat_alloc_output_context2(&oc, nullptr, nullptr, filename);
    if (!oc) {
        std::cerr << "Could not alloc output context\n";
        return -1;
    }
    const AVOutputFormat* ofmt = oc->oformat;

    // 2) find video encoder
    AVCodecID codec_id = ofmt->video_codec;
    const AVCodec* codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        std::cerr << "Could not find encoder for " << avcodec_get_name(codec_id) << "\n";
        return -1;
    }

    // 3) create new video stream
    AVStream* st = avformat_new_stream(oc, nullptr);
    if (!st) {
        std::cerr << "Could not create stream\n";
        return -1;
    }

    // 4) allocate and set up codec context
    AVCodecContext* cctx = avcodec_alloc_context3(codec);
    cctx->codec_id     = codec_id;
    cctx->width        = width;
    cctx->height       = height;
    cctx->time_base    = time_base;
    cctx->framerate    = framerate;
    cctx->pix_fmt      = AV_PIX_FMT_YUV420P;
    if (ofmt->flags & AVFMT_GLOBALHEADER)
        cctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // 5) open codec
    if (avcodec_open2(cctx, codec, nullptr) < 0) {
        std::cerr << "Could not open video codec\n";
        return -1;
    }

    // 6) copy codec parameters to stream
    avcodec_parameters_from_context(st->codecpar, cctx);

    // 7) open output file
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        if (avio_open(&oc->pb, filename, AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open " << filename << "\n";
            return -1;
        }
    }

    // 8) write header
    if (avformat_write_header(oc, nullptr) < 0) {
        std::cerr << "Error occurred when writing header\n";
        return -1;
    }

    // 9) allocate a frame
    AVFrame* frame = av_frame_alloc();
    frame->format = cctx->pix_fmt;
    frame->width  = cctx->width;
    frame->height = cctx->height;
    av_frame_get_buffer(frame, 32);

    AVPacket* pkt = av_packet_alloc();

    // 10) generate 10 frames, alternating black/white
    for (int i = 0; i < 10; i++) {
        // make sure frame data is writable
        av_frame_make_writable(frame);

        // fill Y plane: 255 for white frame, 0 for black frame
        uint8_t y_val = (i % 2 == 0) ? 255 : 0;
        for (int y = 0; y < height; y++) {
            memset(frame->data[0] + y * frame->linesize[0], y_val, width);
        }
        // fill U and V planes with middle gray (128)
        for (int y = 0; y < height/2; y++) {
            memset(frame->data[1] + y * frame->linesize[1], 128, width/2);
            memset(frame->data[2] + y * frame->linesize[2], 128, width/2);
        }

        frame->pts = i;  // PTS in time_base units

        // send frame to encoder
        if (avcodec_send_frame(cctx, frame) < 0) {
            std::cerr << "Error sending a frame for encoding\n";
            break;
        }

        // read all available packets
        while (avcodec_receive_packet(cctx, pkt) == 0) {
            // rescale packet timestamps
            av_packet_rescale_ts(pkt, cctx->time_base, st->time_base);
            pkt->stream_index = st->index;

            // write packet
            av_interleaved_write_frame(oc, pkt);
            av_packet_unref(pkt);
        }
    }

    // 11) flush encoder
    avcodec_send_frame(cctx, nullptr);
    while (avcodec_receive_packet(cctx, pkt) == 0) {
        av_packet_rescale_ts(pkt, cctx->time_base, st->time_base);
        pkt->stream_index = st->index;
        av_interleaved_write_frame(oc, pkt);
        av_packet_unref(pkt);
    }

    // 12) write trailer
    av_write_trailer(oc);

    // 13) cleanup
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&cctx);
    if (!(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&oc->pb);
    avformat_free_context(oc);

    std::cout << "output written to " << filename << "\n";
    return 0;
}
