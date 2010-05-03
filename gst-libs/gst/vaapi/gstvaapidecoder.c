/*
 *  gstvaapidecoder.c - VA decoder abstraction
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
 * SECTION:gstvaapidecoder
 * @short_description: VA decoder abstraction
 */

#include "config.h"
#include <assert.h>
#include <string.h>
#include "gstvaapicompat.h"
#include "gstvaapidecoder.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapiutils.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDecoder, gst_vaapi_decoder, G_TYPE_OBJECT);

/* XXX: Make it a GstVaapiDecodedSurface + propagate PTS */
typedef struct _DecodedSurface DecodedSurface;
struct _DecodedSurface {
    GstVaapiSurfaceProxy *proxy;
    GstVaapiDecoderStatus status;
};

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_CODEC,
    PROP_CODEC_DATA,
    PROP_WIDTH,
    PROP_HEIGHT,
};

static inline void
init_buffer(GstBuffer *buffer, const guchar *buf, guint buf_size)
{
    GST_BUFFER_DATA(buffer)      = (guint8 *)buf;
    GST_BUFFER_SIZE(buffer)      = buf_size;
    GST_BUFFER_TIMESTAMP(buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buffer)  = GST_CLOCK_TIME_NONE;
}

static inline GstBuffer *
create_eos_buffer(void)
{
    GstBuffer *buffer;

    buffer = gst_buffer_new();
    if (!buffer)
        return NULL;

    init_buffer(buffer, NULL, 0);
    GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_EOS);
    return buffer;
}

static GstBuffer *
create_buffer(const guchar *buf, guint buf_size, gboolean copy)
{
    GstBuffer *buffer;

    if (!buf || !buf_size)
        return NULL;

    buffer = gst_buffer_new();
    if (!buffer)
        return NULL;

    if (copy) {
        buffer->malloc_data = g_malloc(buf_size);
        if (!buffer->malloc_data) {
            gst_buffer_unref(buffer);
            return NULL;
        }
        memcpy(buffer->malloc_data, buf, buf_size);
        buf = buffer->malloc_data;
    }
    init_buffer(buffer, buf, buf_size);
    return buffer;
}

static void
destroy_buffer(GstBuffer *buffer)
{
    gst_buffer_unref(buffer);
}

static gboolean
push_buffer(GstVaapiDecoder *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (!buffer) {
        buffer = create_eos_buffer();
        if (!buffer)
            return FALSE;
    }

    GST_DEBUG("queue encoded data buffer %p (%d bytes)",
              buffer, GST_BUFFER_SIZE(buffer));

    g_queue_push_tail(priv->buffers, buffer);
    return TRUE;
}

static GstBuffer *
pop_buffer(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstBuffer *buffer;

    buffer = g_queue_pop_head(priv->buffers);
    if (!buffer)
        return NULL;

    GST_DEBUG("dequeue buffer %p for decoding (%d bytes)",
              buffer, GST_BUFFER_SIZE(buffer));

    return buffer;
}

static GstVaapiDecoderStatus
decode_step(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderStatus status;
    GstBuffer *buffer;

    do {
        buffer = pop_buffer(decoder);
        if (!buffer)
            return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

        status = GST_VAAPI_DECODER_GET_CLASS(decoder)->decode(decoder, buffer);
        GST_DEBUG("decode frame (status = %d)", status);
        if (status == GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;

        if (GST_BUFFER_IS_EOS(buffer))
            return GST_VAAPI_DECODER_STATUS_END_OF_STREAM;
    } while (status == GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA);
    return status;
}

static inline DecodedSurface *
create_surface(void)
{
    return g_slice_new0(DecodedSurface);
}

static inline void
destroy_surface(DecodedSurface *ds)
{
    g_slice_free(DecodedSurface, ds);
}

static gboolean
push_surface(
    GstVaapiDecoder *decoder,
    GstVaapiSurface *surface,
    GstClockTime     timestamp
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    DecodedSurface *ds;

    ds = create_surface();
    if (!ds)
        return FALSE;

    GST_DEBUG("queue decoded surface %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(GST_VAAPI_OBJECT_ID(surface)));
    ds->proxy = gst_vaapi_surface_proxy_new(priv->context, surface);
    if (ds->proxy) {
        ds->status = GST_VAAPI_DECODER_STATUS_SUCCESS;
        gst_vaapi_surface_proxy_set_timestamp(ds->proxy, timestamp);
    }
    else
        ds->status = GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;

    g_queue_push_tail(priv->surfaces, ds);
    return TRUE;
}

static inline DecodedSurface *
pop_surface(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    return g_queue_pop_head(priv->surfaces);
}

static inline void
set_codec_data(GstVaapiDecoder *decoder, GstBuffer *codec_data)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (priv->codec_data) {
        gst_buffer_unref(priv->codec_data);
        priv->codec_data = NULL;
    }

    if (codec_data)
        priv->codec_data = gst_buffer_ref(codec_data);
}

static void
clear_queue(GQueue *q, GDestroyNotify destroy)
{
    while (!g_queue_is_empty(q))
        destroy(g_queue_pop_head(q));
}

static void
gst_vaapi_decoder_finalize(GObject *object)
{
    GstVaapiDecoder * const        decoder = GST_VAAPI_DECODER(object);
    GstVaapiDecoderPrivate * const priv    = decoder->priv;

    set_codec_data(decoder, NULL);

    if (priv->context) {
        g_object_unref(priv->context);
        priv->context = NULL;
    }

    if (priv->buffers) {
        clear_queue(priv->buffers, (GDestroyNotify)destroy_buffer);
        g_queue_free(priv->buffers);
        priv->buffers = NULL;
    }

    if (priv->surfaces) {
        clear_queue(priv->surfaces, (GDestroyNotify)destroy_surface);
        g_queue_free(priv->surfaces);
        priv->surfaces = NULL;
    }

    if (priv->display) {
        g_object_unref(priv->display);
        priv->display = NULL;
    }

    G_OBJECT_CLASS(gst_vaapi_decoder_parent_class)->finalize(object);
}

static void
gst_vaapi_decoder_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiDecoderPrivate * const priv = GST_VAAPI_DECODER(object)->priv;

    switch (prop_id) {
    case PROP_DISPLAY:
        priv->display = g_object_ref(g_value_get_object(value));
        break;
    case PROP_CODEC:
        priv->codec = g_value_get_uint(value);
        break;
    case PROP_CODEC_DATA:
        set_codec_data(GST_VAAPI_DECODER(object), gst_value_get_buffer(value));
        break;
    case PROP_WIDTH:
        priv->width = g_value_get_uint(value);
        break;
    case PROP_HEIGHT:
        priv->height = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_decoder_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiDecoderPrivate * const priv = GST_VAAPI_DECODER(object)->priv;

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_object(value, priv->display);
        break;
    case PROP_CODEC:
        g_value_set_uint(value, priv->codec);
        break;
    case PROP_CODEC_DATA:
        gst_value_set_buffer(value, priv->codec_data);
        break;
    case PROP_WIDTH:
        g_value_set_uint(value, priv->width);
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, priv->height);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_decoder_class_init(GstVaapiDecoderClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDecoderPrivate));

    object_class->finalize     = gst_vaapi_decoder_finalize;
    object_class->set_property = gst_vaapi_decoder_set_property;
    object_class->get_property = gst_vaapi_decoder_get_property;

    /**
     * GstVaapiDecoder:display:
     *
     * The #GstVaapiDisplay this decoder is bound to.
     */
    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_object("display",
                             "Display",
                             "The GstVaapiDisplay this decoder is bound to",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_CODEC,
         g_param_spec_uint("codec",
                           "Codec",
                           "The codec handled by the decoder",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_CODEC_DATA,
         gst_param_spec_mini_object("codec-data",
                                    "Codec data",
                                    "Extra codec data",
                                    GST_TYPE_BUFFER,
                                    G_PARAM_WRITABLE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_WIDTH,
         g_param_spec_uint("width",
                           "Width",
                           "The coded width of the picture",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "Height",
                           "The coded height of the picture",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_decoder_init(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate *priv = GST_VAAPI_DECODER_GET_PRIVATE(decoder);

    decoder->priv               = priv;
    priv->context               = NULL;
    priv->codec                 = 0;
    priv->codec_data            = NULL;
    priv->width                 = 0;
    priv->height                = 0;
    priv->fps_n                 = 1000;
    priv->fps_d                 = 30;
    priv->buffers               = g_queue_new();
    priv->surfaces              = g_queue_new();
}

/**
 * gst_vaapi_decoder_get_frame_rate:
 * @decoder: a #GstVaapiDecoder
 * @num: return location for the numerator of the frame rate
 * @den: return location for the denominator of the frame rate
 *
 * Retrieves the current frame rate as the fraction @num / @den. The
 * default frame rate is 30 fps.
 */
void
gst_vaapi_decoder_get_frame_rate(
    GstVaapiDecoder *decoder,
    guint           *num,
    guint           *den
)
{
    g_return_if_fail(GST_VAAPI_IS_DECODER(decoder));

    if (num)
        *num = decoder->priv->fps_n;

    if (den)
        *den = decoder->priv->fps_d;
}

/**
 * gst_vaapi_decoder_set_frame_rate:
 * @decoder: a #GstVaapiDecoder
 * @num: the numerator of the frame rate
 * @den: the denominator of the frame rate
 *
 * Sets the frame rate for the stream to @num / @den. By default, the
 * decoder will use the frame rate encoded in the elementary stream.
 * If none is available, the decoder will default to 30 fps.
 */
void
gst_vaapi_decoder_set_frame_rate(
    GstVaapiDecoder *decoder,
    guint            num,
    guint            den
)
{
    g_return_if_fail(GST_VAAPI_IS_DECODER(decoder));

    decoder->priv->fps_n = num;
    decoder->priv->fps_d = den;
}

/**
 * gst_vaapi_decoder_put_buffer_data:
 * @decoder: a #GstVaapiDecoder
 * @buf: pointer to buffer data
 * @buf_size: size of buffer data in bytes
 *
 * Queues @buf_size bytes from the data @buf to the HW decoder. The
 * caller is responsible for making sure @buf is live beyond this
 * function. So, this function is mostly useful with static data
 * buffers. gst_vaapi_decoder_put_buffer_data_copy() does the same but
 * copies the data.
 *
 * Caller can notify an End-Of-Stream with @buf set to %NULL and
 * @buf_size set to zero.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_decoder_put_buffer_data(
    GstVaapiDecoder *decoder,
    const guchar    *buf,
    guint            buf_size
)
{
    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), FALSE);

    return push_buffer(decoder, create_buffer(buf, buf_size, FALSE));
}

/**
 * gst_vaapi_decoder_put_buffer_data_copy:
 * @decoder: a #GstVaapiDecoder
 * @buf: pointer to buffer data
 * @buf_size: size of buffer data in bytes
 *
 * Queues a copy of @buf to the HW decoder.
 *
 * Caller can notify an End-Of-Stream with @buf set to %NULL and
 * @buf_size set to zero.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_decoder_put_buffer_data_copy(
    GstVaapiDecoder *decoder,
    const guchar    *buf,
    guint            buf_size
)
{
    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), FALSE);

    return push_buffer(decoder, create_buffer(buf, buf_size, TRUE));
}

/**
 * gst_vaapi_decoder_put_buffer:
 * @decoder: a #GstVaapiDecoder
 * @buf: a #GstBuffer
 *
 * Queues a #GstBuffer to the HW decoder. The decoder holds a
 * reference to @buf.
 *
 * Caller can notify an End-Of-Stream with @buf set to %NULL.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_decoder_put_buffer(GstVaapiDecoder *decoder, GstBuffer *buf)
{
    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), FALSE);

    return push_buffer(decoder, buf ? gst_buffer_ref(buf) : NULL);
}

/**
 * gst_vaapi_decoder_get_surface:
 * @decoder: a #GstVaapiDecoder
 * @pstatus: return location for the decoder status, or %NULL
 *
 * Flushes encoded buffers to the decoder and returns a decoded
 * surface, if any.
 *
 * Return value: a #GstVaapiSurfaceProxy holding the decoded surface,
 *   or %NULL if none is available (e.g. an error). Caller owns the
 *   returned object. g_object_unref() after usage.
 */
GstVaapiSurfaceProxy *
gst_vaapi_decoder_get_surface(
    GstVaapiDecoder       *decoder,
    GstVaapiDecoderStatus *pstatus
)
{
    GstVaapiSurfaceProxy *proxy  = NULL;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    DecodedSurface *ds;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    ds = pop_surface(decoder);
    if (!ds) {
        do {
            status = decode_step(decoder);
        } while (status == GST_VAAPI_DECODER_STATUS_SUCCESS);
        ds = pop_surface(decoder);
    }

    if (ds) {
        proxy  = ds->proxy;
        status = ds->status;
        destroy_surface(ds);
    }

    if (pstatus)
        *pstatus = status;
    return proxy;
}

gboolean
gst_vaapi_decoder_ensure_context(
    GstVaapiDecoder    *decoder,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint,
    guint               width,
    guint               height
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (priv->context)
        return gst_vaapi_context_reset(priv->context,
                                       profile, entrypoint, width, height);

    priv->context = gst_vaapi_context_new(
        priv->display,
        profile,
        entrypoint,
        width,
        height
    );
    return priv->context != NULL;
}

gboolean
gst_vaapi_decoder_push_surface(
    GstVaapiDecoder *decoder,
    GstVaapiSurface *surface,
    GstClockTime     timestamp
)
{
    return push_surface(decoder, surface, timestamp);
}
