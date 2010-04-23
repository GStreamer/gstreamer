/*
 *  gstvaapidecoder_ffmpeg.c - FFmpeg-based decoder
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * SECTION:gstvaapidecoder_ffmpeg
 * @short_description: FFmpeg-based decoder
 */

#include "config.h"
#include <libavcodec/avcodec.h>
#include <libavcodec/vaapi.h>
#include <libavformat/avformat.h>
#include "gstvaapidecoder_ffmpeg.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiobject_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDecoderFfmpeg,
              gst_vaapi_decoder_ffmpeg,
              GST_VAAPI_TYPE_DECODER);

#define GST_VAAPI_DECODER_FFMPEG_GET_PRIVATE(obj)               \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DECODER_FFMPEG, \
                                 GstVaapiDecoderFfmpegPrivate))

/** Default I/O buffer size (32 KB) */
#define DEFAULT_IOBUF_SIZE (32 * 1024)

typedef struct _GstVaapiContextFfmpeg GstVaapiContextFfmpeg;
struct _GstVaapiContextFfmpeg {
    struct vaapi_context        base;
    GstVaapiDecoderFfmpeg      *decoder;
};

struct _GstVaapiDecoderFfmpegPrivate {
    AVPacket                    packet;
    AVFrame                    *frame;
    guchar                     *iobuf;
    guint                       iobuf_pos;
    guint                       iobuf_size;
    ByteIOContext               ioctx;
    AVFormatContext            *fmtctx;
    AVCodecContext             *avctx;
    GstVaapiContextFfmpeg      *vactx;
    guint                       video_stream_index;
    guint                       is_constructed  : 1;
};

/** Converts codec to FFmpeg codec id */
static enum CodecID
get_codec_id_from_codec(GstVaapiCodec codec)
{
    switch (codec) {
    case GST_VAAPI_CODEC_MPEG1: return CODEC_ID_MPEG1VIDEO;
    case GST_VAAPI_CODEC_MPEG2: return CODEC_ID_MPEG2VIDEO;
    case GST_VAAPI_CODEC_MPEG4: return CODEC_ID_MPEG4;
    case GST_VAAPI_CODEC_H263:  return CODEC_ID_H263;
    case GST_VAAPI_CODEC_H264:  return CODEC_ID_H264;
    case GST_VAAPI_CODEC_VC1:   return CODEC_ID_VC1;
    }
    return CODEC_ID_NONE;
}

/** Converts codec to FFmpeg raw bitstream format */
static const gchar *
get_raw_format_from_codec(GstVaapiCodec codec)
{
    switch (codec) {
    case GST_VAAPI_CODEC_MPEG1: return "mpegvideo";
    case GST_VAAPI_CODEC_MPEG2: return "mpegvideo";
    case GST_VAAPI_CODEC_MPEG4: return "m4v";
    case GST_VAAPI_CODEC_H263:  return "h263";
    case GST_VAAPI_CODEC_H264:  return "h264";
    case GST_VAAPI_CODEC_VC1:   return "vc1";
    }
    return NULL;
}

/** Finds a suitable profile from FFmpeg context */
static GstVaapiProfile
get_profile(AVCodecContext *avctx)
{
    GstVaapiContextFfmpeg * const vactx = avctx->hwaccel_context;
    GstVaapiDisplay *display;
    GstVaapiProfile test_profiles[4];
    guint i, n_profiles = 0;

#define ADD_PROFILE(profile) do {                                       \
        test_profiles[n_profiles++] = GST_VAAPI_PROFILE_##profile;      \
    } while (0)

    switch (avctx->codec_id) {
    case CODEC_ID_MPEG1VIDEO:
        ADD_PROFILE(MPEG1);
        break;
    case CODEC_ID_MPEG2VIDEO:
        ADD_PROFILE(MPEG2_MAIN);
        ADD_PROFILE(MPEG2_SIMPLE);
        break;
    case CODEC_ID_H263:
        ADD_PROFILE(H263_BASELINE);
        /* fall-through */
    case CODEC_ID_MPEG4:
        ADD_PROFILE(MPEG4_MAIN);
        ADD_PROFILE(MPEG4_ADVANCED_SIMPLE);
        ADD_PROFILE(MPEG4_SIMPLE);
        break;
    case CODEC_ID_H264:
        if (avctx->profile == 66) /* baseline */
            ADD_PROFILE(H264_BASELINE);
        else {
            if (avctx->profile == 77) /* main */
                ADD_PROFILE(H264_MAIN);
            ADD_PROFILE(H264_HIGH);
        }
        break;
    case CODEC_ID_WMV3:
        if (avctx->profile == 0) /* simple */
            ADD_PROFILE(VC1_SIMPLE);
        ADD_PROFILE(VC1_MAIN);
        break;
    case CODEC_ID_VC1:
        ADD_PROFILE(VC1_ADVANCED);
        break;
    default:
        break;
    }

#undef ADD_PROFILE

    display = GST_VAAPI_DECODER_DISPLAY(vactx->decoder);
    if (!display)
        return 0;

    for (i = 0; i < n_profiles; i++)
        if (gst_vaapi_display_has_decoder(display, test_profiles[i]))
            return test_profiles[i];
    return 0;
}

/** Probes FFmpeg format from input stream */
static AVInputFormat *
get_probed_format(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderFfmpegPrivate * const priv =
        GST_VAAPI_DECODER_FFMPEG(decoder)->priv;

    AVProbeData pd;
    pd.filename = "";
    pd.buf      = priv->iobuf;
    pd.buf_size = MIN(gst_vaapi_decoder_read_avail(decoder), priv->iobuf_size);
    if (!gst_vaapi_decoder_copy(decoder, 0, pd.buf, pd.buf_size))
        return FALSE;

    GST_DEBUG("probing format from buffer %p [%d bytes]", pd.buf, pd.buf_size);
    return av_probe_input_format(&pd, 1);
}

/** Tries to get an FFmpeg format from the raw bitstream */
static AVInputFormat *
get_raw_format(GstVaapiDecoder *decoder)
{
    const gchar *raw_format;

    raw_format = get_raw_format_from_codec(GST_VAAPI_DECODER_CODEC(decoder));
    if (!raw_format)
        return NULL;

    GST_DEBUG("trying raw format %s", raw_format);
    return av_find_input_format(raw_format);
}

/** Reads one packet */
static int
stream_read(void *opaque, uint8_t *buf, int buf_size)
{
    GstVaapiDecoder * const decoder = GST_VAAPI_DECODER(opaque);
    GstVaapiDecoderFfmpegPrivate * const priv =
        GST_VAAPI_DECODER_FFMPEG(decoder)->priv;

    if (buf_size > 0) {
        if (priv->is_constructed) {
            buf_size = gst_vaapi_decoder_read(decoder, buf, buf_size);
        }
        else {
            buf_size = gst_vaapi_decoder_copy(
                decoder,
                priv->iobuf_pos,
                buf, buf_size
            );
            priv->iobuf_pos += buf_size;
        }
    }
    return buf_size;
}

/** Seeks into stream */
static int64_t
stream_seek(void *opaque, int64_t offset, int whence)
{
    GstVaapiDecoder * const decoder = GST_VAAPI_DECODER(opaque);
    GstVaapiDecoderFfmpegPrivate * const priv =
        GST_VAAPI_DECODER_FFMPEG(decoder)->priv;

    /* If we parsed the headers (decoder is constructed), we can no
       longer seek into the stream */
    if (priv->is_constructed &&
        !((whence == SEEK_SET || whence == SEEK_CUR) && offset == 0))
        return -1;

    switch (whence) {
    case SEEK_SET:
        priv->iobuf_pos = offset;
        break;
    case SEEK_CUR:
        priv->iobuf_pos += offset;
        break;
    case SEEK_END:
        priv->iobuf_pos = gst_vaapi_decoder_read_avail(decoder) + offset;
        break;
    default:
        return -1;
    }
    return priv->iobuf_pos;
}

/** AVCodecContext.get_format() implementation */
static enum PixelFormat
gst_vaapi_decoder_ffmpeg_get_format(AVCodecContext *avctx, const enum PixelFormat *fmt)
{
    GstVaapiContextFfmpeg * const vactx = avctx->hwaccel_context;
    GstVaapiDecoder * const decoder = GST_VAAPI_DECODER(vactx->decoder);
    GstVaapiProfile profile;
    gboolean success;
    guint i;

    profile = get_profile(avctx);
    if (!profile)
        return PIX_FMT_NONE;

    /* XXX: only VLD entrypoint is supported at this time */
    for (i = 0; fmt[i] != PIX_FMT_NONE; i++)
        if (fmt[i] == PIX_FMT_VAAPI_VLD)
            break;

    success = gst_vaapi_decoder_ensure_context(
        decoder,
        profile,
        GST_VAAPI_ENTRYPOINT_VLD,
        avctx->width, avctx->height
    );
    if (success) {
        GstVaapiDisplay * const display = GST_VAAPI_DECODER_DISPLAY(decoder);
        GstVaapiContext * const context = GST_VAAPI_DECODER_CONTEXT(decoder);
        vactx->base.display    = GST_VAAPI_DISPLAY_VADISPLAY(display);
        vactx->base.context_id = GST_VAAPI_OBJECT_ID(context);
        return fmt[i];
    }
    return PIX_FMT_NONE;
}

/** AVCodecContext.get_buffer() implementation */
static int
gst_vaapi_decoder_ffmpeg_get_buffer(AVCodecContext *avctx, AVFrame *pic)
{
    GstVaapiContextFfmpeg * const vactx = avctx->hwaccel_context;
    GstVaapiContext * const context = GST_VAAPI_DECODER_CONTEXT(vactx->decoder);
    GstVaapiSurface *surface;
    GstVaapiID surface_id;

    surface = gst_vaapi_context_get_surface(context);
    if (!surface) {
        GST_DEBUG("failed to get a free VA surface");
        return -1;
    }

    surface_id = GST_VAAPI_OBJECT_ID(surface);
    GST_DEBUG("surface %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS(surface_id));

    pic->type        = FF_BUFFER_TYPE_USER;
    pic->age         = 1;
    pic->data[0]     = (uint8_t *)surface;
    pic->data[1]     = NULL;
    pic->data[2]     = NULL;
    pic->data[3]     = (uint8_t *)(uintptr_t)surface_id;
    pic->linesize[0] = 0;
    pic->linesize[1] = 0;
    pic->linesize[2] = 0;
    pic->linesize[3] = 0;
    return 0;
}

/** AVCodecContext.reget_buffer() implementation */
static int
gst_vaapi_decoder_ffmpeg_reget_buffer(AVCodecContext *avctx, AVFrame *pic)
{
    GST_DEBUG("UNIMPLEMENTED");
    return -1;
}

/** AVCodecContext.release_buffer() implementation */
static void
gst_vaapi_decoder_ffmpeg_release_buffer(AVCodecContext *avctx, AVFrame *pic)
{
    GstVaapiID surface_id = GST_VAAPI_ID(GPOINTER_TO_UINT(pic->data[3]));

    GST_DEBUG("surface %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS(surface_id));

    pic->data[0] = NULL;
    pic->data[1] = NULL;
    pic->data[2] = NULL;
    pic->data[3] = NULL;
}

static void
gst_vaapi_decoder_ffmpeg_destroy(GstVaapiDecoderFfmpeg *ffdecoder)
{
    GstVaapiDecoderFfmpegPrivate * const priv = ffdecoder->priv;

    if (priv->avctx) {
        avcodec_close(priv->avctx);
        priv->avctx = NULL;
    }

    if (priv->vactx) {
        g_free(priv->vactx);
        priv->vactx = NULL;
    }

    if (priv->fmtctx) {
        av_close_input_stream(priv->fmtctx);
        priv->fmtctx = NULL;
    }

    if (priv->iobuf) {
        g_free(priv->iobuf);
        priv->iobuf = NULL;
        priv->iobuf_pos = 0;
    }

    av_freep(&priv->frame);
    av_free_packet(&priv->packet);
}

static gboolean
gst_vaapi_decoder_ffmpeg_create(GstVaapiDecoderFfmpeg *ffdecoder)
{
    GstVaapiDecoder * const decoder = GST_VAAPI_DECODER(ffdecoder);
    GstVaapiDecoderFfmpegPrivate * const priv = ffdecoder->priv;
    GstVaapiCodec codec = GST_VAAPI_DECODER_CODEC(decoder);
    enum CodecID codec_id = get_codec_id_from_codec(codec);
    typedef AVInputFormat *(*GetFormatFunc)(GstVaapiDecoder *);
    GetFormatFunc get_format[2];
    AVInputFormat *format;
    AVStream *video_stream;
    AVCodec *ffcodec;
    guint i;

    if (!priv->vactx) {
        priv->vactx = g_new(GstVaapiContextFfmpeg, 1);
        if (!priv->vactx)
            return FALSE;
    }
    memset(&priv->vactx->base, 0, sizeof(priv->vactx->base));
    priv->vactx->decoder = ffdecoder;

    if (!priv->frame) {
        priv->frame = avcodec_alloc_frame();
        if (!priv->frame)
            return FALSE;
    }

    if (!priv->iobuf) {
        priv->iobuf = g_malloc0(priv->iobuf_size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!priv->iobuf)
            return FALSE;
    }

    get_format[ !codec] = get_raw_format;
    get_format[!!codec] = get_probed_format;
    for (i = 0; i < 2; i++) {
        format = get_format[i](decoder);
        if (!format)
            continue;

        priv->iobuf_pos = 0;
        init_put_byte(
            &priv->ioctx,
            priv->iobuf,
            priv->iobuf_size,
            0,                  /* write flags */
            ffdecoder,
            stream_read,
            NULL,               /* no packet writer callback */
            stream_seek
        );
        priv->ioctx.is_streamed = 1;

        if (av_open_input_stream(&priv->fmtctx, &priv->ioctx, "", format, NULL) < 0)
            continue;

        if (av_find_stream_info(priv->fmtctx) >= 0)
            break;

        av_close_input_stream(priv->fmtctx);
        priv->fmtctx = NULL;
    }
    if (!priv->fmtctx)
        return FALSE;

    if (av_find_stream_info(priv->fmtctx) < 0)
        return FALSE;
    dump_format(priv->fmtctx, 0, "", 0);

    video_stream = NULL;
    for (i = 0; i < priv->fmtctx->nb_streams; i++) {
        AVStream * const stream = priv->fmtctx->streams[i];
        if (!video_stream &&
            stream->codec->codec_type == CODEC_TYPE_VIDEO &&
            (codec ? (stream->codec->codec_id == codec_id) : 1)) {
            video_stream = stream;
        }
        else
            stream->discard = AVDISCARD_ALL;
    }
    if (!video_stream)
        return FALSE;

    priv->video_stream_index     = video_stream->index;
    priv->avctx                  = video_stream->codec;
    priv->avctx->hwaccel_context = priv->vactx;
    priv->avctx->get_format      = gst_vaapi_decoder_ffmpeg_get_format;
    priv->avctx->get_buffer      = gst_vaapi_decoder_ffmpeg_get_buffer;
    priv->avctx->reget_buffer    = gst_vaapi_decoder_ffmpeg_reget_buffer;
    priv->avctx->release_buffer  = gst_vaapi_decoder_ffmpeg_release_buffer;
    priv->avctx->thread_count    = 1;
    priv->avctx->draw_horiz_band = NULL;
    priv->avctx->slice_flags     = SLICE_FLAG_CODED_ORDER|SLICE_FLAG_ALLOW_FIELD;

    ffcodec = avcodec_find_decoder(priv->avctx->codec_id);
    if (!ffcodec || avcodec_open(priv->avctx, ffcodec) < 0)
        return FALSE;

    av_init_packet(&priv->packet);
    return TRUE;
}

static GstVaapiSurface *
decode_frame(GstVaapiDecoderFfmpeg *ffdecoder, guchar *buf, guint buf_size)
{
    GstVaapiDecoderFfmpegPrivate * const priv = ffdecoder->priv;
    GstVaapiDisplay * const display = GST_VAAPI_DECODER_DISPLAY(ffdecoder);
    GstVaapiSurface *surface = NULL;
    int got_picture = 0;

    GST_VAAPI_DISPLAY_LOCK(display);
    avcodec_decode_video(
        priv->avctx,
        priv->frame,
        &got_picture,
        buf, buf_size
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);

    if (got_picture) {
        surface = gst_vaapi_context_find_surface_by_id(
            GST_VAAPI_DECODER_CONTEXT(ffdecoder),
            GPOINTER_TO_UINT(priv->frame->data[3])
        );
    }
    return surface;
}

GstVaapiDecoderStatus
gst_vaapi_decoder_ffmpeg_decode(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderFfmpeg * const ffdecoder = GST_VAAPI_DECODER_FFMPEG(decoder);
    GstVaapiDecoderFfmpegPrivate * const priv = ffdecoder->priv;
    GstVaapiSurface *surface = NULL;
    AVPacket packet;

    if (!priv->is_constructed) {
        priv->is_constructed = gst_vaapi_decoder_ffmpeg_create(ffdecoder);
        if (!priv->is_constructed) {
            gst_vaapi_decoder_ffmpeg_destroy(ffdecoder);
            return GST_VAAPI_DECODER_STATUS_ERROR_INIT_FAILED;
        }
    }

    av_init_packet(&packet);
    while (av_read_frame(priv->fmtctx, &packet) == 0) {
        if (packet.stream_index != priv->video_stream_index)
            continue;

        surface = decode_frame(ffdecoder, packet.data, packet.size);
        if (surface) /* decode a single frame only */
            break;
    }
    if (!surface)
        surface = decode_frame(ffdecoder, NULL, 0);
    av_free_packet(&packet);

    if (surface && gst_vaapi_decoder_push_surface(decoder, surface))
        return GST_VAAPI_DECODER_STATUS_SUCCESS;
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
}

static void
gst_vaapi_decoder_ffmpeg_finalize(GObject *object)
{
    GstVaapiDecoderFfmpeg * const ffdecoder = GST_VAAPI_DECODER_FFMPEG(object);

    gst_vaapi_decoder_ffmpeg_destroy(ffdecoder);

    G_OBJECT_CLASS(gst_vaapi_decoder_ffmpeg_parent_class)->finalize(object);
}

static void
gst_vaapi_decoder_ffmpeg_class_init(GstVaapiDecoderFfmpegClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDecoderClass * const decoder_class = GST_VAAPI_DECODER_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDecoderFfmpegPrivate));

    object_class->finalize      = gst_vaapi_decoder_ffmpeg_finalize;

    decoder_class->decode       = gst_vaapi_decoder_ffmpeg_decode;
}

static gpointer
gst_vaapi_decoder_ffmpeg_init_once_cb(gpointer user_data)
{
    av_register_all();
    return NULL;
}

static inline void
gst_vaapi_decoder_ffmpeg_init_once(void)
{
    static GOnce once = G_ONCE_INIT;

    g_once(&once, gst_vaapi_decoder_ffmpeg_init_once_cb, NULL);
}

static void
gst_vaapi_decoder_ffmpeg_init(GstVaapiDecoderFfmpeg *decoder)
{
    GstVaapiDecoderFfmpegPrivate *priv;

    gst_vaapi_decoder_ffmpeg_init_once();

    priv                        = GST_VAAPI_DECODER_FFMPEG_GET_PRIVATE(decoder);
    decoder->priv               = priv;
    priv->frame                 = NULL;
    priv->iobuf                 = NULL;
    priv->iobuf_pos             = 0;
    priv->iobuf_size            = DEFAULT_IOBUF_SIZE;
    priv->fmtctx                = NULL;
    priv->avctx                 = NULL;
    priv->vactx                 = NULL;
    priv->video_stream_index    = 0;
    priv->is_constructed        = FALSE;

    av_init_packet(&priv->packet);
}

/**
 * gst_vaapi_decoder_ffmpeg_new:
 * @display: a #GstVaapiDisplay
 * @codec: a #GstVaapiCodec
 *
 * Creates a new #GstVaapiDecoder with the specified @codec bound to
 * @display. If @codec is zero, the first video stream will be
 * selected. Otherwise, the first video stream matching @codec is
 * used, if any.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_ffmpeg_new(GstVaapiDisplay *display, GstVaapiCodec codec)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    return g_object_new(GST_VAAPI_TYPE_DECODER_FFMPEG,
                        "display", display,
                        "codec",   codec,
                        NULL);
}
