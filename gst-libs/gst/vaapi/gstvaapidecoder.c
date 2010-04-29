/*
 *  gstvaapidecoder.c - VA decoder abstraction
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
    PROP_CODEC_DATA
};

/* Wait _at most_ 10 ms for encoded buffers between each decoding step */
#define GST_VAAPI_DECODER_TIMEOUT (10000)

static GstBuffer *
pop_buffer(GstVaapiDecoder *decoder);

static gboolean
push_surface(GstVaapiDecoder *decoder, GstVaapiSurface *surface);

static DecodedSurface *
pop_surface(GstVaapiDecoder *decoder, GTimeVal *end_time);

static void
decoder_task(gpointer data)
{
    GstVaapiDecoder * const decoder = GST_VAAPI_DECODER_CAST(data);
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiDecoderClass * const klass = GST_VAAPI_DECODER_GET_CLASS(decoder);
    GstBuffer *buffer;

    buffer = pop_buffer(decoder);
    if (!buffer)
        return;

    priv->decoder_status = klass->decode(decoder, buffer);
    GST_DEBUG("decode frame (status = %d)", priv->decoder_status);

    switch (priv->decoder_status) {
    case GST_VAAPI_DECODER_STATUS_SUCCESS:
    case GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA:
        break;
    default:
        /*  Send an empty surface to signal an error */
        push_surface(decoder, NULL);
        gst_task_pause(priv->decoder_task);
        break;
    }
}

static void
update_clock(GstVaapiDecoder *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstClockTime timestamp, duration;

    timestamp = GST_BUFFER_TIMESTAMP(buffer);
    duration  = GST_BUFFER_DURATION(buffer);

    if (GST_CLOCK_TIME_IS_VALID(duration)) {
        if (GST_CLOCK_TIME_IS_VALID(timestamp))
            priv->surface_timestamp = timestamp;
        priv->surface_duration = duration;
    }
    else {
        /* Assumes those are user-generated buffers with no timestamp
           or duration information. Try to rely on "framerate". */
        if (!GST_CLOCK_TIME_IS_VALID(priv->surface_timestamp))
            priv->surface_timestamp = 0;
        priv->surface_duration =
            gst_util_uint64_scale_int(GST_SECOND, priv->fps_d, priv->fps_n);
    }
}

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

    g_async_queue_push(priv->buffers, buffer);
    return TRUE;
}

static GstBuffer *
pop_buffer(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GTimeVal end_time;
    GstBuffer *buffer;

    g_get_current_time(&end_time);
    g_time_val_add(&end_time, GST_VAAPI_DECODER_TIMEOUT);

    buffer = g_async_queue_timed_pop(priv->buffers, &end_time);
    if (!buffer)
        return NULL;

    GST_DEBUG("dequeue buffer %p for decoding (%d bytes)",
              buffer, GST_BUFFER_SIZE(buffer));

    update_clock(decoder, buffer);
    return buffer;
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
push_surface(GstVaapiDecoder *decoder, GstVaapiSurface *surface)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    DecodedSurface *ds;

    ds = create_surface();
    if (!ds)
        return FALSE;

    if (surface) {
        GST_DEBUG("queue decoded surface %" GST_VAAPI_ID_FORMAT,
                  GST_VAAPI_ID_ARGS(GST_VAAPI_OBJECT_ID(surface)));
        ds->proxy = gst_vaapi_surface_proxy_new(priv->context, surface);
        if (ds->proxy) {
            ds->status = GST_VAAPI_DECODER_STATUS_SUCCESS;
            gst_vaapi_surface_proxy_set_timestamp(
                ds->proxy, priv->surface_timestamp);
        }
        else
            ds->status = GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    else
        ds->status = priv->decoder_status;

    g_async_queue_push(priv->surfaces, ds);
    return TRUE;
}

static inline DecodedSurface *
pop_surface(GstVaapiDecoder *decoder, GTimeVal *end_time)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (!gst_vaapi_decoder_start(decoder))
        return NULL;

    return g_async_queue_timed_pop(priv->surfaces, end_time);
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
clear_async_queue(GAsyncQueue *q, GDestroyNotify destroy)
{
    guint i, qlen = g_async_queue_length(q);

    for (i = 0; i < qlen; i++)
        destroy(g_async_queue_pop(q));
}

static void
gst_vaapi_decoder_finalize(GObject *object)
{
    GstVaapiDecoder * const        decoder = GST_VAAPI_DECODER(object);
    GstVaapiDecoderPrivate * const priv    = decoder->priv;

    gst_vaapi_decoder_stop(decoder);

    set_codec_data(decoder, NULL);

    if (priv->context) {
        g_object_unref(priv->context);
        priv->context = NULL;
    }

    if (priv->buffers) {
        clear_async_queue(priv->buffers, (GDestroyNotify)gst_buffer_unref);
        g_async_queue_unref(priv->buffers);
        priv->buffers = NULL;
    }

    if (priv->surfaces) {
        clear_async_queue(priv->surfaces, (GDestroyNotify)destroy_surface);
        g_async_queue_unref(priv->surfaces);
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
}

static void
gst_vaapi_decoder_init(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate *priv = GST_VAAPI_DECODER_GET_PRIVATE(decoder);

    decoder->priv               = priv;
    priv->context               = NULL;
    priv->codec                 = 0;
    priv->codec_data            = NULL;
    priv->fps_n                 = 1000;
    priv->fps_d                 = 30;
    priv->surface_timestamp     = GST_CLOCK_TIME_NONE;
    priv->surface_duration      = GST_CLOCK_TIME_NONE;
    priv->buffers               = g_async_queue_new();
    priv->surfaces              = g_async_queue_new();
    priv->decoder_task          = NULL;

    g_static_rec_mutex_init(&priv->decoder_task_lock);
}

/**
 * gst_vaapi_decoder_start:
 * @decoder: a #GstVaapiDecoder
 *
 * Starts the decoder. This creates the internal decoder thread, if
 * necessary.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_decoder_start(GstVaapiDecoder *decoder)
{
    /* This is an internal function */
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (priv->decoder_task)
        return TRUE;

    priv->decoder_task = gst_task_create(decoder_task, decoder);
    if (!priv->decoder_task)
        return FALSE;

    gst_task_set_lock(priv->decoder_task, &priv->decoder_task_lock);
    return gst_task_start(priv->decoder_task);
}

/**
 * gst_vaapi_decoder_stop:
 * @decoder: a #GstVaapiDecoder
 *
 * Stops the decoder. This destroys any decoding thread that was
 * previously created by gst_vaapi_decoder_start(). Only
 * gst_vaapi_decoder_get_surface() on the queued surfaces will be
 * allowed at this point.
 *
 * Return value: %FALSE on success
 */
gboolean
gst_vaapi_decoder_stop(GstVaapiDecoder *decoder)
{
    /* This is an internal function */
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    gboolean success;

    if (!priv->decoder_task)
        return FALSE;

    success = gst_task_join(priv->decoder_task);
    priv->decoder_task = NULL;
    return success;
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
 * Waits for a decoded surface to arrive. This functions blocks until
 * the @decoder has a surface ready for the caller. @pstatus is
 * optional but it can help to know what went wrong during the
 * decoding process.
 *
 * Return value: a #GstVaapiSurfaceProxy holding the decoded surface,
 *   or %NULL if none is available (e.g. an error). Caller owns the
 *   returned object. g_object_unref() after usage.
 */
static GstVaapiSurfaceProxy *
_gst_vaapi_decoder_get_surface(
    GstVaapiDecoder       *decoder,
    GTimeVal              *end_time,
    GstVaapiDecoderStatus *pstatus
)
{
    GstVaapiDecoderStatus status;
    GstVaapiSurfaceProxy *proxy;
    DecodedSurface *ds;

    ds = pop_surface(decoder, end_time);
    if (ds) {
        proxy  = ds->proxy;
        status = ds->status;
        destroy_surface(ds);
    }
    else {
        proxy  = NULL;
        status = GST_VAAPI_DECODER_STATUS_TIMEOUT;
    }

    if (pstatus)
        *pstatus = status;
    return proxy;
}

GstVaapiSurfaceProxy *
gst_vaapi_decoder_get_surface(
    GstVaapiDecoder       *decoder,
    GstVaapiDecoderStatus *pstatus
)
{
    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    return _gst_vaapi_decoder_get_surface(decoder, NULL, pstatus);
}

/**
 * gst_vaapi_decoder_timed_get_surface:
 * @decoder: a #GstVaapiDecoder
 * @timeout: the number of microseconds to wait for the decoded surface
 * @pstatus: return location for the decoder status, or %NULL
 *
 * Waits for a decoded surface to arrive. This function blocks for at
 * least @timeout microseconds. @pstatus is optional but it can help
 * to know what went wrong during the decoding process.
 *
 * Return value: a #GstVaapiSurfaceProxy holding the decoded surface,
 *   or %NULL if none is available (e.g. an error). Caller owns the
 *   returned object. g_object_unref() after usage.
 */
GstVaapiSurfaceProxy *
gst_vaapi_decoder_timed_get_surface(
    GstVaapiDecoder       *decoder,
    guint32                timeout,
    GstVaapiDecoderStatus *pstatus
)
{
    GTimeVal end_time;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    g_get_current_time(&end_time);
    g_time_val_add(&end_time, timeout);

    return _gst_vaapi_decoder_get_surface(decoder, &end_time, pstatus);
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
    GstVaapiSurface *surface
)
{
    return push_surface(decoder, surface);
}
