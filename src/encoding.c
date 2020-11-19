
#include "encoding.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include <libswscale/swscale.h>

////////////////////////////////////////////////////////////////////////////////////
// STATIC FUNCTIONS (Only accessible in this file)
////////////////////////////////////////////////////////////////////////////////////

/// Returns 0 on success.
static int add_video_stream(
    OutputStream *ost,
    AVFormatContext *oc,
    enum AVCodecID codec_id,
    int w,
    int h, 
    int fps
) {
    AVCodecContext *c;
    AVCodec *codec;

    /* find the video encoder */
    codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        return 1;
    }

    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        fprintf(stderr, "Could not alloc stream\n");
        return 1;
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        return 1;
    }
    ost->enc = c;

    /* Put sample parameters. */
    c->bit_rate = 400000;
    /* Resolution must be a multiple of two. */
    c->width    = w;
    c->height   = h;
    /* timebase: This is the fundamental unit of time (in seconds) in terms
     * of which frame timestamps are represented. For fixed-fps content,
     * timebase should be 1/framerate and timestamp increments should be
     * identical to 1. */
    ost->st->time_base = (AVRational){ 1, fps };
    c->time_base       = ost->st->time_base;

    c->gop_size      = 12; /* emit one intra frame every twelve frames at most */
    c->pix_fmt       = AV_PIX_FMT_YUV420P;
    if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        /* just for testing, we also add B-frames */
        c->max_b_frames = 2;
    }
    if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
        /* Needed to avoid using macroblocks in which some coeffs overflow.
         * This does not happen with normal video, it just happens here as
         * the motion of the chroma plane does not match the luma plane. */
        c->mb_decision = 2;
    }
    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    
    return 0;
}


static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data.\n");
        exit(1);
    }

    return picture;
}


static void open_video(AVFormatContext *ctx, OutputStream *ost) {
    // Supress unused paramter
    (void)(ctx);

    AVCodecContext *c;
    int ret;

    c = ost->enc;

    /* open the codec */
    if (avcodec_open2(c, NULL, NULL) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }

    /* Allocate the encoded raw picture. */
    ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!ost->frame) {
        fprintf(stderr, "Could not allocate picture\n");
        exit(1);
    }

    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    ost->tmp_frame = NULL;
    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
        if (!ost->tmp_frame) {
            fprintf(stderr, "Could not allocate temporary picture\n");
            exit(1);
        }
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}

/* Prepare a dummy image. Returns 0 on sucess, returns 1 if there's no more frame to write, returns a negative number otherwise */
static int fill_yuv_image(
    AVFrame *pict, int frame_index, int width, int height
) {
    int x, y, i, ret;

    /* when we pass a frame to the encoder, it may keep a reference to it
     * internally;
     * make sure we do not overwrite it here
     */
    ret = av_frame_make_writable(pict);
    if (ret < 0) {
        return ret;
    }

    i = frame_index;

    if (i >= 5 * 24) {
        return 1;
    }

    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
        }
    }
    return 0;
}

static AVFrame *get_video_frame(OutputStream *ost)
{
    AVCodecContext *c = ost->enc;

    /* check if we want to generate more frames */
    //if (av_compare_ts(ost->next_pts, c->time_base,
    //                  STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
    //    return NULL;

    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        /* as we only generate a YUV420P picture, we must convert it
         * to the codec pixel format if needed */
        if (!ost->sws_ctx) {
            ost->sws_ctx = sws_getContext(c->width, c->height,
                                          AV_PIX_FMT_YUV420P,
                                          c->width, c->height,
                                          c->pix_fmt,
                                          SWS_BICUBIC, NULL, NULL, NULL);
            if (!ost->sws_ctx) {
                fprintf(stderr,
                        "Cannot initialize the conversion context\n");
                exit(1);
            }
        }
        if (0 != fill_yuv_image(ost->tmp_frame, ost->next_pts, c->width, c->height)) {
            return NULL;
        }
        sws_scale(ost->sws_ctx, (const uint8_t* const*)ost->tmp_frame->data, ost->tmp_frame->linesize,
                  0, c->height, ost->frame->data, ost->frame->linesize);
    } else {
        if (0 != fill_yuv_image(ost->frame, ost->next_pts, c->width, c->height)) {
            return NULL;
        }
    }

    ost->frame->pts = ost->next_pts++;

    return ost->frame;
}

/*
 * encode one video frame and send it to the muxer
 * return -1 on fatal error, 1 when encoding is finished, 0 otherwise
 */
static int write_video_frame(AVFormatContext *ctx, OutputStream *ost) {
    int ret;
    AVCodecContext *c;
    AVFrame *frame;
    AVPacket pkt   = { 0 };
    int got_packet = 0;

    c = ost->enc;

    frame = get_video_frame(ost);

    av_init_packet(&pkt);

    /* encode the image */
    ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding a video frame\n");
        return -1;
    }

    if (got_packet) {
        av_packet_rescale_ts(&pkt, c->time_base, ost->st->time_base);
        pkt.stream_index = ost->st->index;

        /* Write the compressed frame to the media file. */
        ret = av_interleaved_write_frame(ctx, &pkt);
    }

    if (ret != 0) {
        fprintf(stderr, "Error while writing video frame\n");
        return -1;
    }

    return (frame || got_packet) ? 0 : 1;
}

static int process_audio_stream(AVFormatContext *ctx, OutputStream *ost) {
    (void)ctx;
    (void)ost;

    // TODO change this to 0 when implemented.
    return 1;
}

static void close_stream(OutputStream *ost)
{
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    sws_freeContext(ost->sws_ctx);
    avresample_free(&ost->avr);
}

// END OF STATIC FUNCTIONS
////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////

#ifdef __EMSCRIPTEN__
EM_JS(int, call_js_writer, (int callback_id, uint8_t* buffer, int length, int64_t position), {
    //const callback = Module.userJsCallbacks[callback_id];
    //const retval = callback(buffer, length);
    //return retval;
    return Asyncify.handleSleep(function(wakeUp) {
        const callback = Module.userJsCallbacks[callback_id];
        callback(buffer, length, position).then(bytesWritten => {
            wakeUp(bytesWritten);
        });
    });
})

EM_JS(int64_t, call_js_seeker, (int callback_id, int64_t offset, int whence), {
    //const callback = Module.userJsCallbacks[callback_id];
    //const retval = callback(buffer, length);
    //return retval;
    const callback = Module.userJsCallbacks[callback_id];
    return callback(offset, whence);
})
#endif

int avio_write_packet(void* user_data, uint8_t* target_buf, int buf_size) {
    EncodingCtx* ctx = user_data;
    int bytes_written = call_js_writer(ctx->writer_id, target_buf, buf_size, ctx->file_curr_pos);
    ctx->file_curr_pos += bytes_written;
    ctx->file_end_pos = FFMAX(ctx->file_end_pos, ctx->file_curr_pos);
    if (buf_size > 0 && bytes_written == 0) {
        return AVERROR_UNKNOWN;
    }
    return bytes_written;
}

int64_t avio_seek_stream(void* user_data, int64_t offset, int whence) {
    EncodingCtx* ctx = user_data;
    if (whence == SEEK_SET) {
        ctx->file_curr_pos = offset;
    } else if (whence == SEEK_CUR) {
        ctx->file_curr_pos += offset;
    } else if (whence == SEEK_END) {
        ctx->file_curr_pos = ctx->file_end_pos + offset;
    } else {
        return -1;
    }
    return ctx->file_curr_pos;
}

int encode_main(EncodingCtx* ctx) {
    int retval = 1;
    int initialized = EM_ASM_INT({return Module.PRIVATE_INITIALIZED ? 1 : 0;});
    if (!initialized) {
        fprintf(stderr, "ERROR: chunky boy hasn't finished initializing yet.\n");
        retval = 1;
        goto cleanup;
    }
    // IMPORTANT Reallocating the buffer because it gets freed with the `io_ctx`.
    // Fun fact: without this `avformat_open_input` just hangs without an error message.
    ctx->avio_ctx_buffer = av_malloc(AVIO_WRITE_BUFFER_SIZE);
    if (!ctx->avio_ctx_buffer) {
        printf("Could not allocate context\n");
        retval = 1;
        goto cleanup;
    }
    //ctx->avread_user_data.reader_callback_id = ctx->reader_id;

    // Find the appropriate muxer format
    AVOutputFormat* format = NULL;
    for (AVOutputFormat* of = av_oformat_next(NULL); of != NULL; of = av_oformat_next(of)) {
        if (strcmp(of->name, "mp4") == 0) {
            format = of;
            break;
        }
    }
    {
        AVCodec* encoder = avcodec_find_encoder(format->video_codec);
        printf("Selected video codec: '%s'\n", encoder->name);
    }
    ctx->format_ctx = avformat_alloc_context();
    if (!ctx->format_ctx) {
        fprintf(stderr, "Memory error\n");
        retval = 1;
        goto cleanup;
    }
    ctx->format_ctx->oformat = format;

    ctx->video_st = malloc(sizeof(OutputStream));
    if (!ctx->video_st) {
        fprintf(stderr, "Memory error\n");
        retval = 1;
        goto cleanup;
    }
    *(ctx->video_st) = (OutputStream){0};
    if (0 != add_video_stream(ctx->video_st, ctx->format_ctx, format->video_codec, 352, 288, 24)) {
        retval = 1;
        goto cleanup;
    }
    int encode_video = 1;

    // add_audio_stream(&audio_st, oc, fmt->audio_codec);
    int encode_audio = 0;

    open_video(ctx->format_ctx, ctx->video_st);
    av_dump_format(ctx->format_ctx, 0, NULL, 1);

    ctx->io_ctx = avio_alloc_context(ctx->avio_ctx_buffer, AVIO_WRITE_BUFFER_SIZE, 1, ctx, NULL, &avio_write_packet, &avio_seek_stream);
    ctx->format_ctx->pb = ctx->io_ctx;

    avformat_write_header(ctx->format_ctx, NULL);

    while (!ctx->encode_stop_requested && (encode_video || encode_audio)) {
        /* select the stream to encode */
        if (encode_video &&
            (!encode_audio || av_compare_ts(ctx->video_st->next_pts, ctx->video_st->enc->time_base,
                                            ctx->audio_st->next_pts, ctx->audio_st->enc->time_base) <= 0)) {
            encode_video = !write_video_frame(ctx->format_ctx, ctx->video_st);
        } else {
            encode_audio = !process_audio_stream(ctx->format_ctx, ctx->audio_st);
        }
    }

    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(ctx->format_ctx);

    // Success!
    retval = 0;

    cleanup:
    if (ctx->io_ctx) { 
        // This frees the avio_ctx_buffer
        av_free(ctx->io_ctx);
        ctx->io_ctx = NULL;
    }

    /* Close each codec. */
    if (ctx->video_st) {
        close_stream(ctx->video_st);
        ctx->video_st = NULL;
    }
    if (ctx->audio_st) {
        close_stream(ctx->audio_st);
        ctx->audio_st = NULL;
    }

    if (ctx->format_ctx) {
        avformat_free_context(ctx->format_ctx);
        ctx->format_ctx = NULL;
    }

    EM_ASM({
        let callback = Module.userJsCallbacks[$0];
        callback($1);
    }, ctx->finished_handler_id, retval);
    return retval;
}
