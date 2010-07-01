/*
 *  gstvaapidecoder_ffmpeg.c - FFmpeg-based decoder
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapidecoder_ffmpeg
 * @short_description: FFmpeg-based decoder
 */

#include "config.h"
#ifdef HAVE_LIBAVCODEC_AVCODEC_H
# include <libavcodec/avcodec.h>
#endif
#ifdef HAVE_FFMPEG_AVCODEC_H
# include <ffmpeg/avcodec.h>
#endif
#ifdef HAVE_LIBAVCODEC_VAAPI_H
# include <libavcodec/vaapi.h>
#endif
#ifdef HAVE_FFMPEG_VAAPI_H
# include <ffmpeg/vaapi.h>
#endif
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

typedef struct _GstVaapiContextFfmpeg GstVaapiContextFfmpeg;
struct _GstVaapiContextFfmpeg {
    struct vaapi_context        base;
    GstVaapiProfile             profile;
    GstVaapiEntrypoint          entrypoint;
    GstVaapiDecoderFfmpeg      *decoder;
};

struct _GstVaapiDecoderFfmpegPrivate {
    GstClockTime                in_timestamp; /* timestamp from the demuxer */
    AVFrame                    *frame;
    AVCodecParserContext       *pctx;
    AVCodecContext             *avctx;
    GstVaapiContextFfmpeg      *vactx;
    guint                       is_constructed  : 1;
    guint                       is_opened       : 1;
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
    case GST_VAAPI_CODEC_WMV3:  return CODEC_ID_WMV3;
    case GST_VAAPI_CODEC_VC1:   return CODEC_ID_VC1;
    }
    return CODEC_ID_NONE;
}

/** Converts PixelFormat to entrypoint */
static GstVaapiEntrypoint
get_entrypoint(enum PixelFormat pix_fmt)
{
    switch (pix_fmt) {
    case PIX_FMT_VAAPI_VLD:     return GST_VAAPI_ENTRYPOINT_VLD;
    case PIX_FMT_VAAPI_IDCT:    return GST_VAAPI_ENTRYPOINT_IDCT;
    case PIX_FMT_VAAPI_MOCO:    return GST_VAAPI_ENTRYPOINT_MOCO;
    default:                    break;
    }
    return 0;
}

/** Finds a suitable profile from FFmpeg context */
static GstVaapiProfile
get_profile(AVCodecContext *avctx, GstVaapiEntrypoint entrypoint)
{
    GstVaapiContextFfmpeg * const vactx = avctx->hwaccel_context;
    GstVaapiDisplay *display;
    GstVaapiProfile profiles[4];
    guint i, n_profiles = 0;

#define ADD_PROFILE(profile) do {                               \
        profiles[n_profiles++] = GST_VAAPI_PROFILE_##profile;   \
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
        if (gst_vaapi_display_has_decoder(display, profiles[i], entrypoint))
            return profiles[i];
    return 0;
}

/** Ensures VA context is correctly set up for the current FFmpeg context */
static GstVaapiContext *
get_context(AVCodecContext *avctx)
{
    GstVaapiContextFfmpeg * const vactx = avctx->hwaccel_context;
    GstVaapiDecoder * const decoder = GST_VAAPI_DECODER(vactx->decoder);
    GstVaapiDisplay *display;
    GstVaapiContext *context;
    gboolean success;

    if (!avctx->coded_width || !avctx->coded_height)
        return NULL;

    gst_vaapi_decoder_set_framerate(
        decoder,
        avctx->time_base.den / avctx->ticks_per_frame,
        avctx->time_base.num
    );

    gst_vaapi_decoder_set_pixel_aspect_ratio(
        decoder,
        avctx->sample_aspect_ratio.num,
        avctx->sample_aspect_ratio.den
    );

    success = gst_vaapi_decoder_ensure_context(
        decoder,
        vactx->profile,
        vactx->entrypoint,
        avctx->coded_width,
        avctx->coded_height
    );
    if (!success) {
        GST_DEBUG("failed to reset VA context:");
        GST_DEBUG("  profile 0x%08x", vactx->profile);
        GST_DEBUG("  entrypoint %d", vactx->entrypoint);
        GST_DEBUG("  surface size %dx%d", avctx->width, avctx->height);
        return NULL;
    }
    display                = GST_VAAPI_DECODER_DISPLAY(decoder);
    context                = GST_VAAPI_DECODER_CONTEXT(decoder);
    vactx->base.display    = GST_VAAPI_DISPLAY_VADISPLAY(display);
    vactx->base.context_id = GST_VAAPI_OBJECT_ID(context);
    return context;
}

/** Sets AVCodecContext.extradata with additional codec data */
static gboolean
set_codec_data(AVCodecContext *avctx, const guchar *buf, guint buf_size)
{
    av_freep(&avctx->extradata);
    avctx->extradata_size = 0;
    if (!buf || buf_size < 1)
        return TRUE;

    avctx->extradata = av_malloc(buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return FALSE;
    avctx->extradata_size = buf_size;
    memcpy(avctx->extradata, buf, buf_size);
    memset(avctx->extradata + buf_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    return TRUE;
}

/** AVCodecContext.get_format() implementation */
static enum PixelFormat
gst_vaapi_decoder_ffmpeg_get_format(AVCodecContext *avctx, const enum PixelFormat *fmt)
{
    GstVaapiContextFfmpeg * const vactx = avctx->hwaccel_context;
    GstVaapiProfile profile;
    GstVaapiEntrypoint entrypoint;
    guint i;

    /* XXX: only VLD entrypoint is supported at this time */
    for (i = 0; fmt[i] != PIX_FMT_NONE; i++) {
        entrypoint = get_entrypoint(fmt[i]);
        if (entrypoint != GST_VAAPI_ENTRYPOINT_VLD)
            continue;

        profile = get_profile(avctx, entrypoint);
        if (profile) {
            vactx->profile    = profile;
            vactx->entrypoint = entrypoint;
            return fmt[i];
        }
    }
    return PIX_FMT_NONE;
}

/** AVCodecContext.get_buffer() implementation */
static int
gst_vaapi_decoder_ffmpeg_get_buffer(AVCodecContext *avctx, AVFrame *pic)
{
    GstVaapiContextFfmpeg * const vactx = avctx->hwaccel_context;
    GstVaapiContext *context;
    GstVaapiSurface *surface;
    GstVaapiID surface_id;

    context = get_context(avctx);
    if (!context)
        return -1;

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
    pic->pts         = vactx->decoder->priv->in_timestamp;
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
gst_vaapi_decoder_ffmpeg_close(GstVaapiDecoderFfmpeg *ffdecoder)
{
    GstVaapiDecoderFfmpegPrivate * const priv = ffdecoder->priv;

    if (priv->avctx) {
        if (priv->is_opened) {
            avcodec_close(priv->avctx);
            priv->is_opened = FALSE;
        }
        av_freep(&priv->avctx->extradata);
        priv->avctx->extradata_size = 0;
    }

    if (priv->pctx) {
        av_parser_close(priv->pctx);
        priv->pctx = NULL;
    }
}

static gboolean
gst_vaapi_decoder_ffmpeg_open(GstVaapiDecoderFfmpeg *ffdecoder, GstBuffer *buffer)
{
    GstVaapiDecoderFfmpegPrivate * const priv = ffdecoder->priv;
    GstVaapiDisplay * const display = GST_VAAPI_DECODER_DISPLAY(ffdecoder);
    GstBuffer * const codec_data = GST_VAAPI_DECODER_CODEC_DATA(ffdecoder);
    GstVaapiCodec codec = GST_VAAPI_DECODER_CODEC(ffdecoder);
    enum CodecID codec_id;
    AVCodec *ffcodec;
    gboolean try_parser, need_parser;
    int ret;

    gst_vaapi_decoder_ffmpeg_close(ffdecoder);

    if (codec_data) {
        const guchar *data = GST_BUFFER_DATA(codec_data);
        const guint   size = GST_BUFFER_SIZE(codec_data);
        if (!set_codec_data(priv->avctx, data, size))
            return FALSE;
    }

    codec_id = get_codec_id_from_codec(codec);
    if (codec_id == CODEC_ID_NONE)
        return FALSE;

    ffcodec = avcodec_find_decoder(codec_id);
    if (!ffcodec)
        return FALSE;

    switch (codec_id) {
    case CODEC_ID_H264:
        /* For AVC1 formats, sequence headers are in extradata and
           input encoded buffers represent the whole NAL unit */
        try_parser  = priv->avctx->extradata_size == 0;
        need_parser = try_parser;
        break;
    case CODEC_ID_WMV3:
        /* There is no WMV3 parser in FFmpeg */
        try_parser  = FALSE;
        need_parser = FALSE;
        break;
    case CODEC_ID_VC1:
        /* For VC-1, sequence headers ae in extradata and input encoded
           buffers represent the whole slice */
        try_parser  = priv->avctx->extradata_size == 0;
        need_parser = FALSE;
        break;
    default:
        try_parser  = TRUE;
        need_parser = TRUE;
        break;
    }

    if (try_parser) {
        priv->pctx = av_parser_init(codec_id);
        if (!priv->pctx && need_parser)
            return FALSE;
    }

    /* XXX: av_find_stream_info() does this and some codecs really
       want hard an extradata buffer for initialization (e.g. VC-1) */
    if (!priv->avctx->extradata && priv->pctx && priv->pctx->parser->split) {
        const guchar *buf = GST_BUFFER_DATA(buffer);
        guint buf_size = GST_BUFFER_SIZE(buffer);
        buf_size = priv->pctx->parser->split(priv->avctx, buf, buf_size);
        if (buf_size > 0 && !set_codec_data(priv->avctx, buf, buf_size))
            return FALSE;
    }

    if (priv->pctx && !need_parser) {
        av_parser_close(priv->pctx);
        priv->pctx = NULL;
    }

    /* Use size information from the demuxer, whenever available */
    priv->avctx->coded_width  = GST_VAAPI_DECODER_WIDTH(ffdecoder);
    priv->avctx->coded_height = GST_VAAPI_DECODER_HEIGHT(ffdecoder);

    GST_VAAPI_DISPLAY_LOCK(display);
    ret = avcodec_open(priv->avctx, ffcodec);
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (ret < 0)
        return FALSE;
    return TRUE;
}

static void
gst_vaapi_decoder_ffmpeg_destroy(GstVaapiDecoderFfmpeg *ffdecoder)
{
    GstVaapiDecoderFfmpegPrivate * const priv = ffdecoder->priv;

    gst_vaapi_decoder_ffmpeg_close(ffdecoder);

    if (priv->vactx) {
        g_free(priv->vactx);
        priv->vactx = NULL;
    }

    av_freep(&priv->avctx);
    av_freep(&priv->frame);
}

static gboolean
gst_vaapi_decoder_ffmpeg_create(GstVaapiDecoderFfmpeg *ffdecoder)
{
    GstVaapiDecoderFfmpegPrivate * const priv = ffdecoder->priv;

    if (!GST_VAAPI_DECODER_CODEC(ffdecoder))
        return FALSE;

    if (!priv->frame) {
        priv->frame = avcodec_alloc_frame();
        if (!priv->frame)
            return FALSE;
    }

    if (!priv->avctx) {
        priv->avctx = avcodec_alloc_context();
        if (!priv->avctx)
            return FALSE;
    }

    if (!priv->vactx) {
        priv->vactx = g_new(GstVaapiContextFfmpeg, 1);
        if (!priv->vactx)
            return FALSE;
    }
    memset(&priv->vactx->base, 0, sizeof(priv->vactx->base));
    priv->vactx->decoder = ffdecoder;

    priv->avctx->hwaccel_context = priv->vactx;
    priv->avctx->get_format      = gst_vaapi_decoder_ffmpeg_get_format;
    priv->avctx->get_buffer      = gst_vaapi_decoder_ffmpeg_get_buffer;
    priv->avctx->reget_buffer    = gst_vaapi_decoder_ffmpeg_reget_buffer;
    priv->avctx->release_buffer  = gst_vaapi_decoder_ffmpeg_release_buffer;
    priv->avctx->thread_count    = 1;
    priv->avctx->draw_horiz_band = NULL;
    priv->avctx->slice_flags     = SLICE_FLAG_CODED_ORDER|SLICE_FLAG_ALLOW_FIELD;
    return TRUE;
}

static GstVaapiDecoderStatus
decode_frame(GstVaapiDecoderFfmpeg *ffdecoder, guchar *buf, guint buf_size)
{
    GstVaapiDecoderFfmpegPrivate * const priv = ffdecoder->priv;
    GstVaapiDisplay * const display = GST_VAAPI_DECODER_DISPLAY(ffdecoder);
    GstVaapiSurface *surface;
    int bytes_read, got_picture = 0;

    GST_VAAPI_DISPLAY_LOCK(display);
    bytes_read = avcodec_decode_video(
        priv->avctx,
        priv->frame,
        &got_picture,
        buf, buf_size
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!got_picture)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    if (bytes_read < 0)
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    surface = gst_vaapi_context_find_surface_by_id(
        GST_VAAPI_DECODER_CONTEXT(ffdecoder),
        GPOINTER_TO_UINT(priv->frame->data[3])
    );
    if (!surface)
        return GST_VAAPI_DECODER_STATUS_ERROR_INVALID_SURFACE;

    if (!gst_vaapi_decoder_push_surface(GST_VAAPI_DECODER_CAST(ffdecoder),
                                        surface, priv->frame->pts))
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

GstVaapiDecoderStatus
gst_vaapi_decoder_ffmpeg_decode(GstVaapiDecoder *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderFfmpeg * const ffdecoder = GST_VAAPI_DECODER_FFMPEG(decoder);
    GstVaapiDecoderFfmpegPrivate * const priv = ffdecoder->priv;
    GstClockTime inbuf_ts;
    guchar *outbuf;
    gint inbuf_ofs, inbuf_size, outbuf_size;
    gboolean got_frame;

    g_return_val_if_fail(priv->is_constructed,
                         GST_VAAPI_DECODER_STATUS_ERROR_INIT_FAILED);

    if (!priv->is_opened) {
        priv->is_opened = gst_vaapi_decoder_ffmpeg_open(ffdecoder, buffer);
        if (!priv->is_opened)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;
    }

    inbuf_ofs  = 0;
    inbuf_size = GST_BUFFER_SIZE(buffer);
    inbuf_ts   = GST_BUFFER_TIMESTAMP(buffer);

    if (priv->pctx) {
        do {
            int parsed_size = av_parser_parse(
                priv->pctx,
                priv->avctx,
                &outbuf, &outbuf_size,
                GST_BUFFER_DATA(buffer) + inbuf_ofs, inbuf_size,
                inbuf_ts, inbuf_ts
            );
            got_frame = outbuf && outbuf_size > 0;

            if (parsed_size > 0) {
                inbuf_ofs  += parsed_size;
                inbuf_size -= parsed_size;
            }
        } while (!got_frame && inbuf_size > 0);
        inbuf_ts    = priv->pctx->pts;
    }
    else {
        outbuf      = GST_BUFFER_DATA(buffer);
        outbuf_size = inbuf_size;
        got_frame   = outbuf && outbuf_size > 0;
        inbuf_ofs   = inbuf_size;
        inbuf_size  = 0;
    }

    if (inbuf_size > 0 &&
        !gst_vaapi_decoder_push_buffer_sub(decoder, buffer,
                                           inbuf_ofs, inbuf_size))
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;

    if (!got_frame && !GST_BUFFER_IS_EOS(buffer))
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

    priv->in_timestamp = inbuf_ts;
    return decode_frame(ffdecoder, outbuf, outbuf_size);
}

static void
gst_vaapi_decoder_ffmpeg_finalize(GObject *object)
{
    GstVaapiDecoderFfmpeg * const ffdecoder = GST_VAAPI_DECODER_FFMPEG(object);

    gst_vaapi_decoder_ffmpeg_destroy(ffdecoder);

    G_OBJECT_CLASS(gst_vaapi_decoder_ffmpeg_parent_class)->finalize(object);
}

static void
gst_vaapi_decoder_ffmpeg_constructed(GObject *object)
{
    GstVaapiDecoderFfmpeg * const ffdecoder = GST_VAAPI_DECODER_FFMPEG(object);
    GstVaapiDecoderFfmpegPrivate * const priv = ffdecoder->priv;
    GObjectClass *parent_class;

    parent_class = G_OBJECT_CLASS(gst_vaapi_decoder_ffmpeg_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);

    priv->is_constructed = gst_vaapi_decoder_ffmpeg_create(ffdecoder);
}

static void
gst_vaapi_decoder_ffmpeg_class_init(GstVaapiDecoderFfmpegClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDecoderClass * const decoder_class = GST_VAAPI_DECODER_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDecoderFfmpegPrivate));

    object_class->finalize      = gst_vaapi_decoder_ffmpeg_finalize;
    object_class->constructed   = gst_vaapi_decoder_ffmpeg_constructed;

    decoder_class->decode       = gst_vaapi_decoder_ffmpeg_decode;
}

static gpointer
gst_vaapi_decoder_ffmpeg_init_once_cb(gpointer user_data)
{
    avcodec_register_all();
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
    priv->pctx                  = NULL;
    priv->avctx                 = NULL;
    priv->vactx                 = NULL;
    priv->is_constructed        = FALSE;
    priv->is_opened             = FALSE;
}

/**
 * gst_vaapi_decoder_ffmpeg_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder based on FFmpeg where the codec is
 * determined from @caps. The @caps can hold extra information like
 * codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_ffmpeg_new(GstVaapiDisplay *display, GstCaps *caps)
{
    GstVaapiDecoderFfmpeg *ffdecoder;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    ffdecoder = g_object_new(
        GST_VAAPI_TYPE_DECODER_FFMPEG,
        "display", display,
        "caps",    caps,
        NULL
    );
    if (!ffdecoder->priv->is_constructed) {
        g_object_unref(ffdecoder);
        return NULL;
    }
    return GST_VAAPI_DECODER_CAST(ffdecoder);
}
