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

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_CODEC,
};

static gpointer
decoder_thread_cb(gpointer data)
{
    GstVaapiDecoder * const decoder = data;
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiDecoderClass * const klass = GST_VAAPI_DECODER_GET_CLASS(decoder);
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;

    if (!klass->decode) {
        g_error("unimplemented GstVaapiDecoder::decode() function");
        return NULL;
    }

    while (!priv->decoder_thread_cancel) {
        g_mutex_lock(priv->adapter_mutex);
        while (!gst_adapter_available(priv->adapter)) {
            g_cond_wait(priv->adapter_cond, priv->adapter_mutex);
            if (priv->decoder_thread_cancel)
                break;
        }
        g_mutex_unlock(priv->adapter_mutex);

        if (!priv->decoder_thread_cancel) {
            if (status == GST_VAAPI_DECODER_STATUS_SUCCESS) {
                g_object_ref(decoder);
                status = klass->decode(decoder);
                g_object_unref(decoder);
                GST_DEBUG("decode frame (status = %d)", status);
            }
            else {
                /* XXX: something went wrong, simply destroy any
                   buffer until this decoder is destroyed */
                g_mutex_lock(priv->adapter_mutex);
                gst_adapter_clear(priv->adapter);
                g_mutex_unlock(priv->adapter_mutex);

                /* Signal the main thread we got an error */
                gst_vaapi_decoder_push_surface(decoder, NULL);
            }
        }
    }
    return NULL;
}

static GstBuffer *
create_buffer(const guchar *buf, guint buf_size, gboolean copy)
{
    GstBuffer *buffer;

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
        GST_BUFFER_DATA(buffer) = buffer->malloc_data;
        GST_BUFFER_SIZE(buffer) = buf_size;
    }
    else {
        GST_BUFFER_DATA(buffer) = (guint8 *)buf;
        GST_BUFFER_SIZE(buffer) = buf_size;
    }
    return buffer;
}

static gboolean
push_buffer(GstVaapiDecoder *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (!buffer)
        return FALSE;

    g_return_val_if_fail(priv->adapter_mutex && priv->adapter_cond, FALSE);

    GST_DEBUG("queue encoded data buffer %p (%d bytes)",
              buffer, GST_BUFFER_SIZE(buffer));

    /* XXX: add a mechanism to wait for enough buffer bytes to be consumed */
    g_mutex_lock(priv->adapter_mutex);
    gst_adapter_push(priv->adapter, buffer);
    g_cond_signal(priv->adapter_cond);
    g_mutex_unlock(priv->adapter_mutex);

    if (!priv->decoder_thread) {
        priv->decoder_thread = g_thread_create(
            decoder_thread_cb, decoder,
            TRUE,
            NULL
        );
        if (!priv->decoder_thread)
            return FALSE;
    }
    return TRUE;
}

static void
unref_surface_cb(gpointer surface, gpointer user_data)
{
    if (surface)
        g_object_unref(GST_VAAPI_SURFACE(surface));
}

static void
gst_vaapi_decoder_finalize(GObject *object)
{
    GstVaapiDecoderPrivate * const priv = GST_VAAPI_DECODER(object)->priv;

    if (priv->decoder_thread) {
        priv->decoder_thread_cancel = TRUE;
        if (priv->adapter_mutex && priv->adapter_cond) {
            g_mutex_lock(priv->adapter_mutex);
            g_cond_signal(priv->adapter_cond);
            g_mutex_unlock(priv->adapter_mutex);
        }
        g_thread_join(priv->decoder_thread);
        priv->decoder_thread = NULL;
    }

    if (priv->adapter) {
        gst_adapter_clear(priv->adapter);
        g_object_unref(priv->adapter);
        priv->adapter = NULL;
    }

    if (priv->adapter_cond) {
        g_cond_free(priv->adapter_cond);
        priv->adapter_cond = NULL;
    }

    if (priv->adapter_mutex) {
        g_mutex_free(priv->adapter_mutex);
        priv->adapter_mutex = NULL;
    }

    if (priv->context) {
        g_object_unref(priv->context);
        priv->context = NULL;
    }

    g_queue_foreach(&priv->surfaces, unref_surface_cb, NULL);

    if (priv->surfaces_cond) {
        g_cond_free(priv->surfaces_cond);
        priv->surfaces_cond = NULL;
    }

    if (priv->surfaces_mutex) {
        g_mutex_free(priv->surfaces_mutex);
        priv->surfaces_mutex = NULL;
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
}

static void
gst_vaapi_decoder_init(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate *priv = GST_VAAPI_DECODER_GET_PRIVATE(decoder);

    decoder->priv               = priv;
    priv->context               = NULL;
    priv->codec                 = 0;
    priv->adapter               = gst_adapter_new();
    priv->adapter_mutex         = g_mutex_new();
    priv->adapter_cond          = g_cond_new();
    priv->surfaces_mutex        = g_mutex_new();
    priv->surfaces_cond         = g_cond_new();
    priv->decoder_thread        = NULL;
    priv->decoder_thread_cancel = FALSE;

    g_queue_init(&priv->surfaces);
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
    g_return_val_if_fail(buf, FALSE);
    g_return_val_if_fail(buf_size > 0, FALSE);

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
    g_return_val_if_fail(buf, FALSE);
    g_return_val_if_fail(buf_size > 0, FALSE);

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
    g_return_val_if_fail(GST_IS_BUFFER(buf), FALSE);

    return push_buffer(decoder, gst_buffer_ref(buf));
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
static GstVaapiSurface *
_gst_vaapi_decoder_get_surface(
    GstVaapiDecoder       *decoder,
    GTimeVal              *timeout,
    GstVaapiDecoderStatus *pstatus
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiSurface *surface;

    g_mutex_lock(priv->surfaces_mutex);
    while (g_queue_is_empty(&priv->surfaces))
        if (!g_cond_timed_wait(priv->surfaces_cond, priv->surfaces_mutex, timeout))
            break;
    surface = g_queue_pop_head(&priv->surfaces);
    g_mutex_unlock(priv->surfaces_mutex);

    if (surface)
        *pstatus = GST_VAAPI_DECODER_STATUS_SUCCESS;
    else {
        g_mutex_lock(priv->adapter_mutex);
        if (gst_adapter_available(priv->adapter))
            *pstatus = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        else if (timeout)
            *pstatus = GST_VAAPI_DECODER_STATUS_TIMEOUT;
        else
            *pstatus = GST_VAAPI_DECODER_STATUS_END_OF_STREAM;
        g_mutex_unlock(priv->adapter_mutex);
    }
    return surface;
}

GstVaapiSurfaceProxy *
gst_vaapi_decoder_get_surface(
    GstVaapiDecoder       *decoder,
    GstVaapiDecoderStatus *pstatus
)
{
    GstVaapiSurfaceProxy *proxy = NULL;
    GstVaapiSurface *surface;
    GstVaapiDecoderStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    surface = _gst_vaapi_decoder_get_surface(decoder, NULL, &status);
    if (surface) {
        proxy = gst_vaapi_surface_proxy_new(decoder->priv->context, surface);
        if (!proxy)
            status = GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
        g_object_unref(surface);
    }

    if (pstatus)
        *pstatus = status;
    return proxy;
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
    GstVaapiSurfaceProxy *proxy = NULL;
    GstVaapiSurface *surface;
    GstVaapiDecoderStatus status;
    GTimeVal end_time;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    g_get_current_time(&end_time);
    g_time_val_add(&end_time, timeout);

    surface = _gst_vaapi_decoder_get_surface(decoder, &end_time, &status);
    if (surface) {
        proxy = gst_vaapi_surface_proxy_new(decoder->priv->context, surface);
        if (!proxy)
            status = GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
        g_object_unref(surface);
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

guint
gst_vaapi_decoder_copy(
    GstVaapiDecoder *decoder,
    guint            offset,
    guchar          *buf,
    guint            buf_size
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    guint avail;

    if (!buf || !buf_size)
        return 0;

    avail = gst_vaapi_decoder_read_avail(decoder);
    if (offset >= avail)
        return 0;
    if (buf_size > avail - offset)
        buf_size = avail - offset;

    if (buf_size > 0) {
        g_mutex_lock(priv->adapter_mutex);
        gst_adapter_copy(priv->adapter, buf, offset, buf_size);
        g_mutex_unlock(priv->adapter_mutex);
    }
    return buf_size;
}

guint
gst_vaapi_decoder_read_avail(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    guint avail;

    g_mutex_lock(priv->adapter_mutex);
    avail = gst_adapter_available(priv->adapter);
    g_mutex_unlock(priv->adapter_mutex);
    return avail;
}

guint
gst_vaapi_decoder_read(GstVaapiDecoder *decoder, guchar *buf, guint buf_size)
{
    buf_size = gst_vaapi_decoder_copy(decoder, 0, buf, buf_size);
    if (buf_size > 0)
        gst_vaapi_decoder_flush(decoder, buf_size);
    return buf_size;
}

void
gst_vaapi_decoder_flush(GstVaapiDecoder *decoder, guint buf_size)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    guint avail;

    if (!buf_size)
        return;

    avail = gst_vaapi_decoder_read_avail(decoder);
    if (buf_size > avail)
        buf_size = avail;

    g_mutex_lock(priv->adapter_mutex);
    gst_adapter_flush(priv->adapter, buf_size);
    g_mutex_unlock(priv->adapter_mutex);
}

gboolean
gst_vaapi_decoder_push_surface(
    GstVaapiDecoder *decoder,
    GstVaapiSurface *surface
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (surface)
        GST_DEBUG("queue decoded surface %" GST_VAAPI_ID_FORMAT,
                  GST_VAAPI_ID_ARGS(GST_VAAPI_OBJECT_ID(surface)));
    else
        GST_DEBUG("queue null surface to signal an error");

    g_mutex_lock(priv->surfaces_mutex);
    g_queue_push_tail(&priv->surfaces, surface ? g_object_ref(surface) : NULL);
    g_cond_signal(priv->surfaces_cond);
    g_mutex_unlock(priv->surfaces_mutex);
    return TRUE;
}
