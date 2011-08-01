/*
 *  gstvaapidecoder.c - VA decoder abstraction
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011 Intel Corporation
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

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_CAPS,
};

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
        buffer = gst_buffer_new();
        if (!buffer)
            return FALSE;
        GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_EOS);
    }

    GST_DEBUG("queue encoded data buffer %p (%d bytes)",
              buffer, GST_BUFFER_SIZE(buffer));

    g_queue_push_tail(priv->buffers, buffer);
    return TRUE;
}

static void
push_back_buffer(GstVaapiDecoder *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    GST_DEBUG("requeue encoded data buffer %p (%d bytes)",
              buffer, GST_BUFFER_SIZE(buffer));

    g_queue_push_head(priv->buffers, buffer);
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
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiDecoderStatus status;
    GstBuffer *buffer;

    /* Decoding will fail if there is no surface left */
    if (priv->context &&
        gst_vaapi_context_get_surface_count(priv->context) == 0)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE;

    do {
        buffer = pop_buffer(decoder);
        if (!buffer)
            return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

        status = GST_VAAPI_DECODER_GET_CLASS(decoder)->decode(decoder, buffer);
        GST_DEBUG("decode frame (status = %d)", status);
        gst_buffer_unref(buffer);
        if (status == GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;

        if (GST_BUFFER_IS_EOS(buffer))
            return GST_VAAPI_DECODER_STATUS_END_OF_STREAM;
    } while (status == GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA);
    return status;
}

static gboolean
push_surface(
    GstVaapiDecoder *decoder,
    GstVaapiSurface *surface,
    GstClockTime     timestamp
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiSurfaceProxy *proxy;

    GST_DEBUG("queue decoded surface %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(GST_VAAPI_OBJECT_ID(surface)));

    proxy = gst_vaapi_surface_proxy_new(priv->context, surface);
    if (!proxy)
        return FALSE;

    gst_vaapi_surface_proxy_set_timestamp(proxy, timestamp);
    g_queue_push_tail(priv->surfaces, proxy);
    return TRUE;
}

static inline GstVaapiSurfaceProxy *
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
set_caps(GstVaapiDecoder *decoder, GstCaps *caps)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    GstVaapiProfile profile;
    const GValue *v_codec_data;
    gint v1, v2;

    profile = gst_vaapi_profile_from_caps(caps);
    if (!profile)
        return;

    priv->caps = gst_caps_copy(caps);

    priv->codec = gst_vaapi_profile_get_codec(profile);
    if (!priv->codec)
        return;

    if (gst_structure_get_int(structure, "width", &v1))
        priv->width = v1;
    if (gst_structure_get_int(structure, "height", &v2))
        priv->height = v2;

    if (gst_structure_get_fraction(structure, "framerate", &v1, &v2)) {
        priv->fps_n = v1;
        priv->fps_d = v2;
    }

    if (gst_structure_get_fraction(structure, "pixel-aspect-ratio", &v1, &v2)) {
        priv->par_n = v1;
        priv->par_d = v2;
    }

    v_codec_data = gst_structure_get_value(structure, "codec_data");
    if (v_codec_data)
        set_codec_data(decoder, gst_value_get_buffer(v_codec_data));
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

    if (priv->caps) {
        gst_caps_unref(priv->caps);
        priv->caps = NULL;
    }

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
        clear_queue(priv->surfaces, (GDestroyNotify)g_object_unref);
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
    GstVaapiDecoder * const        decoder = GST_VAAPI_DECODER(object);
    GstVaapiDecoderPrivate * const priv    = decoder->priv;

    switch (prop_id) {
    case PROP_DISPLAY:
        priv->display = g_object_ref(g_value_get_object(value));
        break;
    case PROP_CAPS:
        set_caps(decoder, g_value_get_pointer(value));
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
    case PROP_CAPS:
        gst_value_set_caps(value, priv->caps);
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
         PROP_CAPS,
         g_param_spec_pointer("caps",
                              "Decoder caps",
                              "The decoder caps",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_decoder_init(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate *priv = GST_VAAPI_DECODER_GET_PRIVATE(decoder);

    decoder->priv               = priv;
    priv->context               = NULL;
    priv->caps                  = NULL;
    priv->codec                 = 0;
    priv->codec_data            = NULL;
    priv->width                 = 0;
    priv->height                = 0;
    priv->fps_n                 = 0;
    priv->fps_d                 = 0;
    priv->par_n                 = 0;
    priv->par_d                 = 0;
    priv->buffers               = g_queue_new();
    priv->surfaces              = g_queue_new();
}

/**
 * gst_vaapi_decoder_get_caps:
 * @decoder: a #GstVaapiDecoder
 *
 * Retrieves the @decoder caps. The deocder owns the returned caps, so
 * use gst_caps_ref() whenever necessary.
 *
 * Return value: the @decoder caps
 */
GstCaps *
gst_vaapi_decoder_get_caps(GstVaapiDecoder *decoder)
{
    return decoder->priv->caps;
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
    GstVaapiSurfaceProxy *proxy;
    GstVaapiDecoderStatus status;

    if (pstatus)
        *pstatus = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    proxy = pop_surface(decoder);
    if (!proxy) {
        do {
            status = decode_step(decoder);
        } while (status == GST_VAAPI_DECODER_STATUS_SUCCESS);
        proxy = pop_surface(decoder);
    }

    if (proxy)
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;

    if (pstatus)
        *pstatus = status;
    return proxy;
}

void
gst_vaapi_decoder_set_picture_size(
    GstVaapiDecoder    *decoder,
    guint               width,
    guint               height
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    gboolean size_changed = FALSE;

    if (priv->width != width) {
        GST_DEBUG("picture width changed to %d", width);
        priv->width = width;
        gst_caps_set_simple(priv->caps, "width", G_TYPE_INT, width, NULL);
        size_changed = TRUE;
    }

    if (priv->height != height) {
        GST_DEBUG("picture height changed to %d", height);
        priv->height = height;
        gst_caps_set_simple(priv->caps, "height", G_TYPE_INT, height, NULL);
        size_changed = TRUE;
    }

    if (size_changed)
        g_object_notify(G_OBJECT(decoder), "caps");
}

void
gst_vaapi_decoder_set_framerate(
    GstVaapiDecoder    *decoder,
    guint               fps_n,
    guint               fps_d
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (!fps_n || !fps_d)
        return;

    if (priv->fps_n != fps_n || priv->fps_d != fps_d) {
        GST_DEBUG("framerate changed to %u/%u", fps_n, fps_d);
        priv->fps_n = fps_n;
        priv->fps_d = fps_d;
        gst_caps_set_simple(
            priv->caps,
            "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
            NULL
        );
        g_object_notify(G_OBJECT(decoder), "caps");
    }
}

void
gst_vaapi_decoder_set_pixel_aspect_ratio(
    GstVaapiDecoder    *decoder,
    guint               par_n,
    guint               par_d
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (!par_n || !par_d)
        return;

    if (priv->par_n != par_n || priv->par_d != par_d) {
        GST_DEBUG("pixel-aspect-ratio changed to %u/%u", par_n, par_d);
        priv->par_n = par_n;
        priv->par_d = par_d;
        gst_caps_set_simple(
            priv->caps,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d,
            NULL
        );
        g_object_notify(G_OBJECT(decoder), "caps");
    }
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

    gst_vaapi_decoder_set_picture_size(decoder, width, height);

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
gst_vaapi_decoder_push_buffer_sub(
    GstVaapiDecoder *decoder,
    GstBuffer       *buffer,
    guint            offset,
    guint            size
)
{
    GstBuffer *subbuffer;

    subbuffer = gst_buffer_create_sub(buffer, offset, size);
    if (!subbuffer)
        return FALSE;

    push_back_buffer(decoder, subbuffer);
    return TRUE;
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
