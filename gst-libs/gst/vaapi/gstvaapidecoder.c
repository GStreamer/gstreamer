/*
 *  gstvaapidecoder.c - VA decoder abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2013 Intel Corporation
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

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapidecoder.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapiparser_frame.h"
#include "gstvaapisurfaceproxy_priv.h"
#include "gstvaapiutils.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDecoder, gst_vaapi_decoder, G_TYPE_OBJECT)

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_CAPS,

    N_PROPERTIES
};

static GParamSpec *g_properties[N_PROPERTIES] = { NULL, };

static void
drop_frame(GstVaapiDecoder *decoder, GstVideoCodecFrame *frame);

static void
parser_state_finalize(GstVaapiParserState *ps)
{
    if (ps->input_adapter) {
        gst_adapter_clear(ps->input_adapter);
        g_object_unref(ps->input_adapter);
        ps->input_adapter = NULL;
    }

    if (ps->output_adapter) {
        gst_adapter_clear(ps->output_adapter);
        g_object_unref(ps->output_adapter);
        ps->output_adapter = NULL;
    }

    if (ps->next_unit_pending) {
        gst_vaapi_decoder_unit_clear(&ps->next_unit);
        ps->next_unit_pending = FALSE;
    }
}

static gboolean
parser_state_init(GstVaapiParserState *ps)
{
    ps->input_adapter = gst_adapter_new();
    if (!ps->input_adapter)
        return FALSE;

    ps->output_adapter = gst_adapter_new();
    if (!ps->output_adapter)
        return FALSE;
    return TRUE;
}

static void
parser_state_prepare(GstVaapiParserState *ps, GstAdapter *adapter)
{
    /* XXX: check we really have a continuity from the previous call */
    if (ps->current_adapter != adapter)
        goto reset;
    return;

reset:
    ps->current_adapter = adapter;
    ps->input_offset2 = -1;
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
              buffer, gst_buffer_get_size(buffer));

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
              buffer, gst_buffer_get_size(buffer));

    return buffer;
}

static GstVaapiDecoderStatus
do_parse(GstVaapiDecoder *decoder,
    GstVideoCodecFrame *base_frame, GstAdapter *adapter, gboolean at_eos,
    guint *got_unit_size_ptr, gboolean *got_frame_ptr)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiParserState * const ps = &priv->parser_state;
    GstVaapiParserFrame *frame;
    GstVaapiDecoderUnit *unit;
    GstVaapiDecoderStatus status;

    *got_unit_size_ptr = 0;
    *got_frame_ptr = FALSE;

    frame = gst_video_codec_frame_get_user_data(base_frame);
    if (!frame) {
        GstVideoCodecState * const codec_state = priv->codec_state;
        frame = gst_vaapi_parser_frame_new(codec_state->info.width,
            codec_state->info.height);
        if (!frame)
            return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
        gst_video_codec_frame_set_user_data(base_frame,
            frame, (GDestroyNotify)gst_vaapi_mini_object_unref);
    }

    parser_state_prepare(ps, adapter);

    unit = &ps->next_unit;
    if (ps->next_unit_pending) {
        ps->next_unit_pending = FALSE;
        goto got_unit;
    }
    gst_vaapi_decoder_unit_init(unit);

    ps->current_frame = base_frame;
    status = GST_VAAPI_DECODER_GET_CLASS(decoder)->parse(decoder,
        adapter, at_eos, unit);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        if (at_eos && frame->units->len > 0 &&
            status == GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA) {
            /* XXX: assume the frame is complete at <EOS> */
            *got_frame_ptr = TRUE;
            return GST_VAAPI_DECODER_STATUS_SUCCESS;
        }
        return status;
    }

    if (GST_VAAPI_DECODER_UNIT_IS_FRAME_START(unit) && frame->units->len > 0) {
        ps->next_unit_pending = TRUE;
        *got_frame_ptr = TRUE;
        return GST_VAAPI_DECODER_STATUS_SUCCESS;
    }

got_unit:
    gst_vaapi_parser_frame_append_unit(frame, unit);
    *got_unit_size_ptr = unit->size;
    *got_frame_ptr = GST_VAAPI_DECODER_UNIT_IS_FRAME_END(unit);
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
do_decode_units(GstVaapiDecoder *decoder, GArray *units)
{
    GstVaapiDecoderClass * const klass = GST_VAAPI_DECODER_GET_CLASS(decoder);
    GstVaapiDecoderStatus status;
    guint i;

    for (i = 0; i < units->len; i++) {
        GstVaapiDecoderUnit * const unit =
            &g_array_index(units, GstVaapiDecoderUnit, i);
        if (GST_VAAPI_DECODER_UNIT_IS_SKIPPED(unit))
            continue;
        status = klass->decode(decoder, unit);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
do_decode_1(GstVaapiDecoder *decoder, GstVaapiParserFrame *frame)
{
    GstVaapiDecoderClass * const klass = GST_VAAPI_DECODER_GET_CLASS(decoder);
    GstVaapiDecoderStatus status;

    if (frame->pre_units->len > 0) {
        status = do_decode_units(decoder, frame->pre_units);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
    }

    if (frame->units->len > 0) {
        if (klass->start_frame) {
            GstVaapiDecoderUnit * const unit =
                &g_array_index(frame->units, GstVaapiDecoderUnit, 0);
            status = klass->start_frame(decoder, unit);
            if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
                return status;
        }

        status = do_decode_units(decoder, frame->units);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;

        if (klass->end_frame) {
            status = klass->end_frame(decoder);
            if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
                return status;
        }
    }

    if (frame->post_units->len > 0) {
        status = do_decode_units(decoder, frame->post_units);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
    }

    /* Drop frame if there is no slice data unit in there */
    if (G_UNLIKELY(frame->units->len == 0))
        return GST_VAAPI_DECODER_STATUS_DROP_FRAME;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline GstVaapiDecoderStatus
do_decode(GstVaapiDecoder *decoder, GstVideoCodecFrame *base_frame)
{
    GstVaapiParserState * const ps = &decoder->priv->parser_state;
    GstVaapiParserFrame * const frame = base_frame->user_data;
    GstVaapiDecoderStatus status;

    ps->current_frame = base_frame;

    gst_vaapi_parser_frame_ref(frame);
    status = do_decode_1(decoder, frame);
    gst_vaapi_parser_frame_unref(frame);

    switch ((guint)status) {
    case GST_VAAPI_DECODER_STATUS_DROP_FRAME:
        drop_frame(decoder, base_frame);
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;
        break;
    }
    return status;
}

static inline GstVaapiDecoderStatus
do_flush(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderClass * const klass = GST_VAAPI_DECODER_GET_CLASS(decoder);

    if (klass->flush)
        return klass->flush(decoder);
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_step(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiParserState * const ps = &priv->parser_state;
    GstVaapiDecoderStatus status;
    GstBuffer *buffer;
    gboolean got_frame;
    guint got_unit_size, input_size;

    status = gst_vaapi_decoder_check_status(decoder);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;

    /* Fill adapter with all buffers we have in the queue */
    for (;;) {
        buffer = pop_buffer(decoder);
        if (!buffer)
            break;

        ps->at_eos = GST_BUFFER_IS_EOS(buffer);
        if (!ps->at_eos)
            gst_adapter_push(ps->input_adapter, buffer);
    }

    /* Parse and decode all decode units */
    input_size = gst_adapter_available(ps->input_adapter);
    if (input_size == 0) {
        if (ps->at_eos)
            return GST_VAAPI_DECODER_STATUS_END_OF_STREAM;
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    }

    do {
        if (!ps->current_frame) {
            ps->current_frame = g_slice_new0(GstVideoCodecFrame);
            if (!ps->current_frame)
                return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
            ps->current_frame->ref_count = 1;
        }

        status = do_parse(decoder, ps->current_frame, ps->input_adapter,
            ps->at_eos, &got_unit_size, &got_frame);
        GST_DEBUG("parse frame (status = %d)", status);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
            if (status == GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA && ps->at_eos)
                status = GST_VAAPI_DECODER_STATUS_END_OF_STREAM;
            break;
        }

        if (got_unit_size > 0) {
            buffer = gst_adapter_take_buffer(ps->input_adapter, got_unit_size);
            input_size -= got_unit_size;

            if (gst_adapter_available(ps->output_adapter) == 0) {
                ps->current_frame->pts =
                    gst_adapter_prev_timestamp(ps->input_adapter, NULL);
            }
            gst_adapter_push(ps->output_adapter, buffer);
        }

        if (got_frame) {
            ps->current_frame->input_buffer = gst_adapter_take_buffer(
                ps->output_adapter,
                gst_adapter_available(ps->output_adapter));

            status = do_decode(decoder, ps->current_frame);
            GST_DEBUG("decode frame (status = %d)", status);

            gst_video_codec_frame_unref(ps->current_frame);
            ps->current_frame = NULL;
            break;
        }
    } while (input_size > 0);
    return status;
}

static void
drop_frame(GstVaapiDecoder *decoder, GstVideoCodecFrame *frame)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    GST_DEBUG("drop frame %d", frame->system_frame_number);

    /* no surface proxy */
    gst_video_codec_frame_set_user_data(frame, NULL, NULL);

    frame->pts = GST_CLOCK_TIME_NONE;
    GST_VIDEO_CODEC_FRAME_FLAG_SET(frame,
        GST_VIDEO_CODEC_FRAME_FLAG_DECODE_ONLY);

    g_queue_push_tail(priv->frames, gst_video_codec_frame_ref(frame));
}

static inline void
push_frame(GstVaapiDecoder *decoder, GstVideoCodecFrame *frame)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVaapiSurfaceProxy * const proxy = frame->user_data;

    GST_DEBUG("queue decoded surface %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(GST_VAAPI_SURFACE_PROXY_SURFACE_ID(proxy)));

    g_queue_push_tail(priv->frames, gst_video_codec_frame_ref(frame));
}

static inline GstVideoCodecFrame *
pop_frame(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVideoCodecFrame *frame;
    GstVaapiSurfaceProxy *proxy;

    frame = g_queue_pop_head(priv->frames);
    if (!frame)
        return NULL;

    proxy = frame->user_data;
    GST_DEBUG("dequeue decoded surface %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(GST_VAAPI_SURFACE_PROXY_SURFACE_ID(proxy)));

    return frame;
}

static void
set_caps(GstVaapiDecoder *decoder, const GstCaps *caps)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;
    GstVideoCodecState * const codec_state = priv->codec_state;
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    GstVaapiProfile profile;
    const GValue *v_codec_data;

    profile = gst_vaapi_profile_from_caps(caps);
    if (!profile)
        return;

    priv->codec = gst_vaapi_profile_get_codec(profile);
    if (!priv->codec)
        return;

    if (!gst_video_info_from_caps(&codec_state->info, caps))
        return;

    codec_state->caps = gst_caps_copy(caps);

    v_codec_data = gst_structure_get_value(structure, "codec_data");
    if (v_codec_data)
        gst_buffer_replace(&codec_state->codec_data,
            gst_value_get_buffer(v_codec_data));
}

static inline GstCaps *
get_caps(GstVaapiDecoder *decoder)
{
    return GST_VAAPI_DECODER_CODEC_STATE(decoder)->caps;
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

    gst_video_codec_state_unref(priv->codec_state);
    priv->codec_state = NULL;

    parser_state_finalize(&priv->parser_state);
 
    if (priv->buffers) {
        clear_queue(priv->buffers, (GDestroyNotify)gst_buffer_unref);
        g_queue_free(priv->buffers);
        priv->buffers = NULL;
    }

    if (priv->frames) {
        clear_queue(priv->frames, (GDestroyNotify)
            gst_video_codec_frame_unref);
        g_queue_free(priv->frames);
        priv->frames = NULL;
    }

    g_clear_object(&priv->context);
    priv->va_context = VA_INVALID_ID;

    g_clear_object(&priv->display);
    priv->va_display = NULL;

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
    GstVaapiDecoder * const decoder = GST_VAAPI_DECODER_CAST(object);
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_object(value, priv->display);
        break;
    case PROP_CAPS:
        gst_value_set_caps(value, get_caps(decoder));
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
    g_properties[PROP_DISPLAY] =
         g_param_spec_object("display",
                             "Display",
                             "The GstVaapiDisplay this decoder is bound to",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);

    g_properties[PROP_CAPS] =
         g_param_spec_pointer("caps",
                              "Decoder caps",
                              "The decoder caps",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY);

    g_object_class_install_properties(object_class, N_PROPERTIES, g_properties);
}

static void
gst_vaapi_decoder_init(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate *priv = GST_VAAPI_DECODER_GET_PRIVATE(decoder);
    GstVideoCodecState *codec_state;

    parser_state_init(&priv->parser_state);

    codec_state = g_slice_new0(GstVideoCodecState);
    codec_state->ref_count = 1;
    gst_video_info_init(&codec_state->info);

    decoder->priv               = priv;
    priv->display               = NULL;
    priv->va_display            = NULL;
    priv->context               = NULL;
    priv->va_context            = VA_INVALID_ID;
    priv->codec                 = 0;
    priv->codec_state           = codec_state;
    priv->buffers               = g_queue_new();
    priv->frames                = g_queue_new();
}

/**
 * gst_vaapi_decoder_get_codec:
 * @decoder: a #GstVaapiDecoder
 *
 * Retrieves the @decoder codec type.
 *
 * Return value: the #GstVaapiCodec type for @decoder
 */
GstVaapiCodec
gst_vaapi_decoder_get_codec(GstVaapiDecoder *decoder)
{
    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), (GstVaapiCodec)0);

    return decoder->priv->codec;
}

/**
 * gst_vaapi_decoder_get_codec_state:
 * @decoder: a #GstVaapiDecoder
 *
 * Retrieves the @decoder codec state. The decoder owns the returned
 * #GstVideoCodecState structure, so use gst_video_codec_state_ref()
 * whenever necessary.
 *
 * Return value: the #GstVideoCodecState object for @decoder
 */
GstVideoCodecState *
gst_vaapi_decoder_get_codec_state(GstVaapiDecoder *decoder)
{
    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    return GST_VAAPI_DECODER_CODEC_STATE(decoder);
}

/**
 * gst_vaapi_decoder_get_caps:
 * @decoder: a #GstVaapiDecoder
 *
 * Retrieves the @decoder caps. The decoder owns the returned caps, so
 * use gst_caps_ref() whenever necessary.
 *
 * Return value: the @decoder caps
 */
GstCaps *
gst_vaapi_decoder_get_caps(GstVaapiDecoder *decoder)
{
    return get_caps(decoder);
}

/**
 * gst_vaapi_decoder_put_buffer:
 * @decoder: a #GstVaapiDecoder
 * @buf: a #GstBuffer
 *
 * Queues a #GstBuffer to the HW decoder. The decoder holds a
 * reference to @buf.
 *
 * Caller can notify an End-Of-Stream with @buf set to %NULL. However,
 * if an empty buffer is passed, i.e. a buffer with %NULL data pointer
 * or size equals to zero, then the function ignores this buffer and
 * returns %TRUE.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_decoder_put_buffer(GstVaapiDecoder *decoder, GstBuffer *buf)
{
    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), FALSE);

    if (buf) {
        if (gst_buffer_get_size(buf) == 0)
            return TRUE;
        buf = gst_buffer_ref(buf);
    }
    return push_buffer(decoder, buf);
}

/**
 * gst_vaapi_decoder_get_surface:
 * @decoder: a #GstVaapiDecoder
 * @out_proxy_ptr: the next decoded surface as a #GstVaapiSurfaceProxy
 *
 * Flushes encoded buffers to the decoder and returns a decoded
 * surface, if any.
 *
 * On successful return, *@out_proxy_ptr contains the decoded surface
 * as a #GstVaapiSurfaceProxy. The caller owns this object, so
 * gst_vaapi_surface_proxy_unref() shall be called after usage.
 *
 * Return value: a #GstVaapiDecoderStatus
 */
GstVaapiDecoderStatus
gst_vaapi_decoder_get_surface(GstVaapiDecoder *decoder,
    GstVaapiSurfaceProxy **out_proxy_ptr)
{
    GstVideoCodecFrame *frame;
    GstVaapiDecoderStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder),
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
    g_return_val_if_fail(out_proxy_ptr != NULL,
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);

    do {
        frame = pop_frame(decoder);
        while (frame) {
            if (!GST_VIDEO_CODEC_FRAME_IS_DECODE_ONLY(frame)) {
                GstVaapiSurfaceProxy * const proxy = frame->user_data;
                proxy->timestamp = frame->pts;
                proxy->duration = frame->duration;
                *out_proxy_ptr = proxy;
                gst_video_codec_frame_unref(frame);
                return GST_VAAPI_DECODER_STATUS_SUCCESS;
            }
            gst_video_codec_frame_unref(frame);
            frame = pop_frame(decoder);
        }
        status = decode_step(decoder);
    } while (status == GST_VAAPI_DECODER_STATUS_SUCCESS);

    *out_proxy_ptr = NULL;
    return status;
}

/**
 * gst_vaapi_decoder_get_frame:
 * @decoder: a #GstVaapiDecoder
 * @out_frame_ptr: the next decoded frame as a #GstVideoCodecFrame
 *
 * On successful return, *@out_frame_ptr contains the next decoded
 * frame available as a #GstVideoCodecFrame. The caller owns this
 * object, so gst_video_codec_frame_unref() shall be called after
 * usage. Otherwise, @GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA is
 * returned if no decoded frame is available.
 *
 * The actual surface is available as a #GstVaapiSurfaceProxy attached
 * to the user-data anchor of the output frame. Ownership of the proxy
 * is transferred to the frame.
 *
 * Return value: a #GstVaapiDecoderStatus
 */
GstVaapiDecoderStatus
gst_vaapi_decoder_get_frame(GstVaapiDecoder *decoder,
    GstVideoCodecFrame **out_frame_ptr)
{
    GstVideoCodecFrame *out_frame;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder),
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
    g_return_val_if_fail(out_frame_ptr != NULL,
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);

    out_frame = pop_frame(decoder);
    if (!out_frame)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

#if !GST_CHECK_VERSION(1,0,0)
    if (!GST_VIDEO_CODEC_FRAME_IS_DECODE_ONLY(out_frame)) {
        const guint flags = GST_VAAPI_SURFACE_PROXY_FLAGS(out_frame->user_data);
        guint out_flags = 0;

        if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_TFF)
            out_flags |= GST_VIDEO_CODEC_FRAME_FLAG_TFF;
        if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_RFF)
            out_flags |= GST_VIDEO_CODEC_FRAME_FLAG_RFF;
        if (flags & GST_VAAPI_SURFACE_PROXY_FLAG_ONEFIELD)
            out_flags |= GST_VIDEO_CODEC_FRAME_FLAG_ONEFIELD;
        GST_VIDEO_CODEC_FRAME_FLAG_SET(out_frame, out_flags);
    }
#endif

    *out_frame_ptr = out_frame;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

void
gst_vaapi_decoder_set_picture_size(
    GstVaapiDecoder    *decoder,
    guint               width,
    guint               height
)
{
    GstVideoCodecState * const codec_state = decoder->priv->codec_state;
    gboolean size_changed = FALSE;

    if (codec_state->info.width != width) {
        GST_DEBUG("picture width changed to %d", width);
        codec_state->info.width = width;
        gst_caps_set_simple(codec_state->caps,
            "width", G_TYPE_INT, width, NULL);
        size_changed = TRUE;
    }

    if (codec_state->info.height != height) {
        GST_DEBUG("picture height changed to %d", height);
        codec_state->info.height = height;
        gst_caps_set_simple(codec_state->caps,
            "height", G_TYPE_INT, height, NULL);
        size_changed = TRUE;
    }

    if (size_changed)
        g_object_notify_by_pspec(G_OBJECT(decoder), g_properties[PROP_CAPS]);
}

void
gst_vaapi_decoder_set_framerate(
    GstVaapiDecoder    *decoder,
    guint               fps_n,
    guint               fps_d
)
{
    GstVideoCodecState * const codec_state = decoder->priv->codec_state;

    if (!fps_n || !fps_d)
        return;

    if (codec_state->info.fps_n != fps_n || codec_state->info.fps_d != fps_d) {
        GST_DEBUG("framerate changed to %u/%u", fps_n, fps_d);
        codec_state->info.fps_n = fps_n;
        codec_state->info.fps_d = fps_d;
        gst_caps_set_simple(codec_state->caps,
            "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);
        g_object_notify_by_pspec(G_OBJECT(decoder), g_properties[PROP_CAPS]);
    }
}

void
gst_vaapi_decoder_set_pixel_aspect_ratio(
    GstVaapiDecoder    *decoder,
    guint               par_n,
    guint               par_d
)
{
    GstVideoCodecState * const codec_state = decoder->priv->codec_state;

    if (!par_n || !par_d)
        return;

    if (codec_state->info.par_n != par_n || codec_state->info.par_d != par_d) {
        GST_DEBUG("pixel-aspect-ratio changed to %u/%u", par_n, par_d);
        codec_state->info.par_n = par_n;
        codec_state->info.par_d = par_d;
        gst_caps_set_simple(codec_state->caps,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d, NULL);
        g_object_notify_by_pspec(G_OBJECT(decoder), g_properties[PROP_CAPS]);
    }
}

static const gchar *
gst_interlace_mode_to_string(GstVideoInterlaceMode mode)
{
    switch (mode) {
    case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:  return "progressive";
    case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:  return "interleaved";
    case GST_VIDEO_INTERLACE_MODE_MIXED:        return "mixed";
    }
    return "<unknown>";
}

void
gst_vaapi_decoder_set_interlace_mode(GstVaapiDecoder *decoder,
    GstVideoInterlaceMode mode)
{
    GstVideoCodecState * const codec_state = decoder->priv->codec_state;

    if (codec_state->info.interlace_mode != mode) {
        GST_DEBUG("interlace mode changed to %s",
                  gst_interlace_mode_to_string(mode));
        codec_state->info.interlace_mode = mode;
        gst_caps_set_simple(codec_state->caps, "interlaced",
            G_TYPE_BOOLEAN, mode != GST_VIDEO_INTERLACE_MODE_PROGRESSIVE, NULL);
        g_object_notify_by_pspec(G_OBJECT(decoder), g_properties[PROP_CAPS]);
    }
}

void
gst_vaapi_decoder_set_interlaced(GstVaapiDecoder *decoder, gboolean interlaced)
{
    gst_vaapi_decoder_set_interlace_mode(decoder,
        (interlaced ?
         GST_VIDEO_INTERLACE_MODE_INTERLEAVED :
         GST_VIDEO_INTERLACE_MODE_PROGRESSIVE));
}

gboolean
gst_vaapi_decoder_ensure_context(
    GstVaapiDecoder     *decoder,
    GstVaapiContextInfo *cip
)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (priv->context) {
        if (!gst_vaapi_context_reset_full(priv->context, cip))
            return FALSE;
    }
    else {
        priv->context = gst_vaapi_context_new_full(priv->display, cip);
        if (!priv->context)
            return FALSE;
    }
    priv->va_context = gst_vaapi_context_get_id(priv->context);
    return TRUE;
}

void
gst_vaapi_decoder_push_frame(GstVaapiDecoder *decoder,
    GstVideoCodecFrame *frame)
{
    push_frame(decoder, frame);
}

GstVaapiDecoderStatus
gst_vaapi_decoder_check_status(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderPrivate * const priv = decoder->priv;

    if (priv->context && gst_vaapi_context_get_surface_count(priv->context) < 1)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

GstVaapiDecoderStatus
gst_vaapi_decoder_parse(GstVaapiDecoder *decoder,
    GstVideoCodecFrame *base_frame, GstAdapter *adapter, gboolean at_eos,
    guint *got_unit_size_ptr, gboolean *got_frame_ptr)
{
    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder),
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
    g_return_val_if_fail(base_frame != NULL,
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
    g_return_val_if_fail(adapter != NULL,
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
    g_return_val_if_fail(got_unit_size_ptr != NULL,
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
    g_return_val_if_fail(got_frame_ptr != NULL,
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);

    return do_parse(decoder, base_frame, adapter, at_eos,
        got_unit_size_ptr, got_frame_ptr);
}

GstVaapiDecoderStatus
gst_vaapi_decoder_decode(GstVaapiDecoder *decoder, GstVideoCodecFrame *frame)
{
    GstVaapiDecoderStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder),
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
    g_return_val_if_fail(frame != NULL,
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);
    g_return_val_if_fail(frame->user_data != NULL,
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);

    status = gst_vaapi_decoder_check_status(decoder);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;
    return do_decode(decoder, frame);
}

GstVaapiDecoderStatus
gst_vaapi_decoder_flush(GstVaapiDecoder *decoder)
{
    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder),
        GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER);

    return do_flush(decoder);
}

GstVaapiDecoderStatus
gst_vaapi_decoder_decode_codec_data(GstVaapiDecoder *decoder)
{
    GstVaapiDecoderClass * const klass = GST_VAAPI_DECODER_GET_CLASS(decoder);
    GstBuffer * const codec_data = GST_VAAPI_DECODER_CODEC_DATA(decoder);
    GstVaapiDecoderStatus status;
    GstMapInfo map_info;
    const guchar *buf;
    guint buf_size;

    if (!codec_data)
        return GST_VAAPI_DECODER_STATUS_SUCCESS;

    /* FIXME: add a meaningful error code? */
    if (!klass->decode_codec_data)
        return GST_VAAPI_DECODER_STATUS_SUCCESS;

    if (!gst_buffer_map(codec_data, &map_info, GST_MAP_READ)) {
        GST_ERROR("failed to map buffer");
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    buf      = map_info.data;
    buf_size = map_info.size;
    if (G_LIKELY(buf && buf_size > 0))
        status = klass->decode_codec_data(decoder, buf, buf_size);
    else
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;
    gst_buffer_unmap(codec_data, &map_info);
    return status;
}
