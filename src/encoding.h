
#ifndef _CHUNKY_ENCODING_H_
#define _CHUNKY_ENCODING_H_


#ifdef HAVE_AV_CONFIG_H
#undef HAVE_AV_CONFIG_H
#endif

#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavresample/avresample.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>

//////////////////////////////////////////////////////////
// Global constants
//////////////////////////////////////////////////////////

#define AVIO_WRITE_BUFFER_SIZE (4096 * 4)


typedef struct OutputStream {
    AVStream *st;
    AVCodecContext *enc;

    //AVDictionary* encoder_opts;

    /* pts of the next frame that will be generated */
    int64_t next_pts;

    AVFrame *frame;
    AVFrame *tmp_frame;

    float t, tincr, tincr2;

    struct SwsContext *sws_ctx;
    AVAudioResampleContext *avr;
} OutputStream;

typedef struct EncodingCtx {
    int width; // Pixel width of the video
    int height; // Pixel height of the video

    double fps; // Image frames per second

    int video_bitrate; // in bits per second
    int audio_bitrate; // in bits per second

    int audio_sample_rate; // in Hertz

    OutputStream* video_st; // May be null
    OutputStream* audio_st; // May be null
    AVFormatContext* format_ctx;
    AVIOContext* io_ctx;
    uint8_t* avio_ctx_buffer;

    int64_t file_curr_pos;
    int64_t file_end_pos;

    //uint8_t* rgba_buffer;
    //size_t rgba_buffer_len;

    /// Identifies the callback function that writes the encoded bytes to a file.
    /// An index into the JS `Module.userJsCallbacks` array.
    int writer_id;

    /// Identifies the callback function that fills up a buffer with
    /// the pixel data of the next frame from the video.
    /// An index into the JS `Module.userJsCallbacks` array.
    int get_image_id;

    /// Identifies the callback function that fills up a buffer with
    /// audio samples.
    /// An index into the JS `Module.userJsCallbacks` array.
    int get_audio_id;

    /// Identifies the callback function that gets called when the encoding
    /// is done.
    /// An index into the JS `Module.userJsCallbacks` array.
    int finished_handler_id;

    volatile int encode_stop_requested;
} EncodingCtx;

void init_encoding_context(EncodingCtx* ctx);
int avio_write_packet(void* user_data, uint8_t* target_buf, int buf_size);
int encode_main(EncodingCtx* ctx);


#endif // _CHUNKY_ENCODING_H_
