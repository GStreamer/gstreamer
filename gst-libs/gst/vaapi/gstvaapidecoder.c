/*
 *  gstvaapidecoder.c - VA decoder abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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
    PROP_CODEC_INFO,
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
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS && GST_BUFFER_IS_EOS(buffer))
            status = GST_VAAPI_DECODER_STATUS_END_OF_STREAM;
        gst_buffer_unref(buffer);
    } while (status == GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA);
    return status;
}

static gboolean
push_surface(
    GstVaapiDecoder      *decoder,
    GstVaapiSurfaceProxy *proxy,
    GstClockTime          timestamp
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    GST_DEBUG("queue decoded surface %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(gst_vaapi_surface_proxy_get_surface_id(proxy)));

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

static inline void
set_codec_info(GstVaapiDecoder *decoder, GstVaapiCodecInfo *codec_info)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (codec_info) {
        priv->codec_info = *codec_info;
        if (!priv->codec_info.pic_size)
            priv->codec_info.pic_size = sizeof(GstVaapiPicture);
        if (!priv->codec_info.slice_size)
            priv->codec_info.slice_size = sizeof(GstVaapiSlice);
    }
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
        priv->va_context = VA_INVALID_ID;
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
        priv->va_display = NULL;
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
        if (priv->display)
            priv->va_display = gst_vaapi_display_get_display(priv->display);
        else
            priv->va_display = NULL;
        break;
    case PROP_CAPS:
        set_caps(decoder, g_value_get_pointer(value));
        break;
    case PROP_CODEC_INFO:
        set_codec_info(decoder, g_value_get_pointer(value));
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

    g_object_class_install_property
        (object_class,
         PROP_CODEC_INFO,
         g_param_spec_pointer("codec-info",
                              "Codec info",
                              "The codec info",
                              G_PARAM_WRITABLE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_decoder_init(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate *priv = GST_VAAPI_DECODER_GET_PRIVATE(decoder);

    decoder->priv               = priv;
    priv->display               = NULL;
    priv->va_display            = NULL;
    priv->context               = NULL;
    priv->va_context            = VA_INVALID_ID;
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
    if (!priv->context)
        return FALSE;

    priv->va_context = gst_vaapi_context_get_id(priv->context);
    return TRUE;
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
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiSurfaceProxy *proxy;

    proxy = gst_vaapi_surface_proxy_new(priv->context, surface);
    if (!proxy)
        return FALSE;
    return push_surface(decoder, proxy, timestamp);
}

gboolean
gst_vaapi_decoder_push_surface_proxy(
    GstVaapiDecoder      *decoder,
    GstVaapiSurfaceProxy *proxy,
    GstClockTime          timestamp
)
{
    return push_surface(decoder, g_object_ref(proxy), timestamp);
}

static void
destroy_iq_matrix(GstVaapiDecoder *decoder, GstVaapiIqMatrix *iq_matrix);

static void
destroy_bitplane(GstVaapiDecoder *decoder, GstVaapiBitPlane *bitplane);

static void
destroy_slice(GstVaapiDecoder *decoder, GstVaapiSlice *slice);

static void
destroy_slice_cb(gpointer data, gpointer user_data)
{
    GstVaapiDecoder * const decoder = GST_VAAPI_DECODER(user_data);
    GstVaapiSlice * const   slice   = data;

    destroy_slice(decoder, slice);
}

static void
destroy_picture(GstVaapiDecoder *decoder, GstVaapiPicture *picture)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (picture->slices) {
        g_ptr_array_foreach(picture->slices, destroy_slice_cb, decoder);
        g_ptr_array_free(picture->slices, TRUE);
        picture->slices = NULL;
    }

    if (picture->iq_matrix) {
        destroy_iq_matrix(decoder, picture->iq_matrix);
        picture->iq_matrix = NULL;
    }

    if (picture->bitplane) {
        destroy_bitplane(decoder, picture->bitplane);
        picture->bitplane = NULL;
    }

    picture->surface = NULL;
    picture->surface_id = VA_INVALID_ID;

    vaapi_destroy_buffer(priv->va_display, &picture->param_id);
    picture->param = NULL;
    g_slice_free1(priv->codec_info.pic_size, picture);
}

static GstVaapiPicture *
create_picture(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiPicture *picture;

    picture = g_slice_alloc(priv->codec_info.pic_size);
    if (!picture)
        return NULL;

    picture->type       = GST_VAAPI_PICTURE_TYPE_NONE;
    picture->flags      = 0;
    picture->surface_id = VA_INVALID_ID;
    picture->surface    = NULL;
    picture->param_id   = VA_INVALID_ID;
    picture->param      = NULL;
    picture->slices     = NULL;
    picture->iq_matrix  = NULL;
    picture->bitplane   = NULL;
    picture->pts        = GST_CLOCK_TIME_NONE;

    picture->surface = gst_vaapi_context_get_surface(priv->context);
    if (!picture->surface)
        goto error;
    picture->surface_id = gst_vaapi_surface_get_id(picture->surface);

    picture->param = vaapi_create_buffer(
        priv->va_display,
        priv->va_context,
        VAPictureParameterBufferType,
        priv->codec_info.pic_param_size,
        &picture->param_id
    );
    if (!picture->param)
        goto error;

    picture->slices = g_ptr_array_new();
    if (!picture->slices)
        goto error;
    return picture;

error:
    destroy_picture(priv->va_display, picture);
    return NULL;
}

GstVaapiPicture *
gst_vaapi_decoder_new_picture(GstVaapiDecoder *decoder)
{
    return create_picture(decoder);
}

void
gst_vaapi_decoder_free_picture(GstVaapiDecoder *decoder, GstVaapiPicture *picture)
{
    destroy_picture(decoder, picture);
}

static void
destroy_iq_matrix(GstVaapiDecoder *decoder, GstVaapiIqMatrix *iq_matrix)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    vaapi_destroy_buffer(priv->va_display, &iq_matrix->param_id);
    iq_matrix->param = NULL;
    g_slice_free(GstVaapiIqMatrix, iq_matrix);
}

static GstVaapiIqMatrix *
create_iq_matrix(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiIqMatrix *iq_matrix;

    iq_matrix = g_slice_new(GstVaapiIqMatrix);
    if (!iq_matrix)
        return NULL;

    iq_matrix->param_id = VA_INVALID_ID;

    iq_matrix->param = vaapi_create_buffer(
        priv->va_display,
        priv->va_context,
        VAIQMatrixBufferType,
        priv->codec_info.iq_matrix_size,
        &iq_matrix->param_id
    );
    if (!iq_matrix->param)
        goto error;
    return iq_matrix;

error:
    destroy_iq_matrix(decoder, iq_matrix);
    return NULL;
}

GstVaapiIqMatrix *
gst_vaapi_decoder_new_iq_matrix(GstVaapiDecoder *decoder)
{
    return create_iq_matrix(decoder);
}

static void
destroy_bitplane(GstVaapiDecoder *decoder, GstVaapiBitPlane *bitplane)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    vaapi_destroy_buffer(priv->va_display, &bitplane->data_id);
    bitplane->data = NULL;
    g_slice_free(GstVaapiBitPlane, bitplane);
}

static GstVaapiBitPlane *
create_bitplane(GstVaapiDecoder *decoder, guint size)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiBitPlane *bitplane;

    bitplane = g_slice_new(GstVaapiBitPlane);
    if (!bitplane)
        return NULL;

    bitplane->data_id = VA_INVALID_ID;

    bitplane->data = vaapi_create_buffer(
        priv->va_display,
        priv->va_context,
        VABitPlaneBufferType,
        size,
        &bitplane->data_id
    );
    if (!bitplane->data)
        goto error;
    return bitplane;

error:
    destroy_bitplane(decoder, bitplane);
    return NULL;
}

GstVaapiBitPlane *
gst_vaapi_decoder_new_bitplane(GstVaapiDecoder *decoder, guint size)
{
    return create_bitplane(decoder, size);
}

static void
destroy_slice(GstVaapiDecoder *decoder, GstVaapiSlice *slice)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    vaapi_destroy_buffer(priv->va_display, &slice->data_id);
    vaapi_destroy_buffer(priv->va_display, &slice->param_id);
    slice->param = NULL;
    g_slice_free1(priv->codec_info.slice_size, slice);
}

static GstVaapiSlice *
create_slice(GstVaapiDecoder *decoder, guchar *buf, guint buf_size)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiSlice *slice;
    VASliceParameterBufferBase *slice_param;
    guchar *data;

    slice = g_slice_alloc(priv->codec_info.slice_size);
    if (!slice)
        return NULL;

    slice->data_id  = VA_INVALID_ID;
    slice->param_id = VA_INVALID_ID;

    data = vaapi_create_buffer(
        priv->va_display,
        priv->va_context,
        VASliceDataBufferType,
        buf_size,
        &slice->data_id
    );
    if (!data)
        goto error;
    memcpy(data, buf, buf_size);
    vaapi_unmap_buffer(priv->va_display, slice->data_id, NULL);

    slice->param = vaapi_create_buffer(
        priv->va_display,
        priv->va_context,
        VASliceParameterBufferType,
        priv->codec_info.slice_param_size,
        &slice->param_id
    );
    if (!slice->param)
        goto error;

    slice_param                    = slice->param;
    slice_param->slice_data_size   = buf_size;
    slice_param->slice_data_offset = 0;
    slice_param->slice_data_flag   = VA_SLICE_DATA_FLAG_ALL;
    return slice;

error:
    destroy_slice(decoder, slice);
    return NULL;
}

GstVaapiSlice *
gst_vaapi_decoder_new_slice(
    GstVaapiDecoder *decoder,
    GstVaapiPicture *picture,
    guchar          *buf,
    guint            buf_size
)
{
    GstVaapiSlice *slice;

    slice = create_slice(decoder, buf, buf_size);
    if (!slice)
        return NULL;
    g_ptr_array_add(picture->slices, slice);
    return slice;
}

gboolean
gst_vaapi_decoder_decode_picture(
    GstVaapiDecoder *decoder,
    GstVaapiPicture *picture
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiIqMatrix * const iq_matrix = picture->iq_matrix;
    GstVaapiBitPlane * const bitplane = picture->bitplane;
    GstVaapiSlice *slice;
    VABufferID va_buffers[3];
    guint i, n_va_buffers = 0;
    VAStatus status;

    GST_DEBUG("decode picture 0x%08x", gst_vaapi_surface_get_id(picture->surface));

    vaapi_unmap_buffer(priv->va_display, picture->param_id, &picture->param);
    va_buffers[n_va_buffers++] = picture->param_id;

    if (iq_matrix) {
        vaapi_unmap_buffer(priv->va_display, iq_matrix->param_id, &iq_matrix->param);
        va_buffers[n_va_buffers++] = iq_matrix->param_id;
    }

    if (bitplane) {
        vaapi_unmap_buffer(priv->va_display, bitplane->data_id, (void **)&bitplane->data);
        va_buffers[n_va_buffers++] = bitplane->data_id;
    }

    status = vaBeginPicture(
        priv->va_display,
        priv->va_context,
        picture->surface_id
    );
    if (!vaapi_check_status(status, "vaBeginPicture()"))
        return FALSE;

    status = vaRenderPicture(
        priv->va_display,
        priv->va_context,
        va_buffers, n_va_buffers
    );
    if (!vaapi_check_status(status, "vaRenderPicture()"))
        return FALSE;

    for (i = 0; i < picture->slices->len; i++) {
        slice = g_ptr_array_index(picture->slices, i);

        vaapi_unmap_buffer(priv->va_display, slice->param_id, NULL);
        va_buffers[0] = slice->param_id;
        va_buffers[1] = slice->data_id;
        n_va_buffers  = 2;

        status = vaRenderPicture(
            priv->va_display,
            priv->va_context,
            va_buffers, n_va_buffers
        );
        if (!vaapi_check_status(status, "vaRenderPicture()"))
            return FALSE;
    }

    status = vaEndPicture(priv->va_display, priv->va_context);
    if (!vaapi_check_status(status, "vaEndPicture()"))
        return FALSE;
    return TRUE;
}
