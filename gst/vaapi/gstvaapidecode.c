/*
 *  gstvaapidecode.c - VA-API video decoder
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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
 * SECTION:gstvaapidecode
 * @short_description: A VA-API based video decoder
 *
 * vaapidecode decodes from raw bitstreams to surfaces suitable for
 * the vaapisink element.
 */

#include "config.h"

#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapivideosink.h>
#include <gst/vaapi/gstvaapivideobuffer.h>
#include <gst/video/videocontext.h>

#if USE_VAAPI_GLX
#include <gst/vaapi/gstvaapivideobuffer_glx.h>
#define gst_vaapi_video_buffer_new(display) \
    gst_vaapi_video_buffer_glx_new(GST_VAAPI_DISPLAY_GLX(display))
#endif

#include "gstvaapidecode.h"
#include "gstvaapipluginutil.h"

#if USE_FFMPEG
# include <gst/vaapi/gstvaapidecoder_ffmpeg.h>
#endif
#if USE_CODEC_PARSERS
# include <gst/vaapi/gstvaapidecoder_h264.h>
# include <gst/vaapi/gstvaapidecoder_mpeg2.h>
# include <gst/vaapi/gstvaapidecoder_mpeg4.h>
# include <gst/vaapi/gstvaapidecoder_vc1.h>
#endif

/* Favor codecparsers-based decoders for 0.3.x series */
#define USE_FFMPEG_DEFAULT \
    (USE_FFMPEG && !USE_CODEC_PARSERS)

#define GST_PLUGIN_NAME "vaapidecode"
#define GST_PLUGIN_DESC "A VA-API based video decoder"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapidecode);
#define GST_CAT_DEFAULT gst_debug_vaapidecode

/* ElementFactory information */
static const GstElementDetails gst_vaapidecode_details =
    GST_ELEMENT_DETAILS(
        "VA-API decoder",
        "Codec/Decoder/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

/* Default templates */
#define GST_CAPS_CODEC(CODEC) CODEC "; "

static const char gst_vaapidecode_sink_caps_str[] =
    GST_CAPS_CODEC("video/mpeg, mpegversion=2, systemstream=(boolean)false")
    GST_CAPS_CODEC("video/mpeg, mpegversion=4")
    GST_CAPS_CODEC("video/x-divx")
    GST_CAPS_CODEC("video/x-xvid")
    GST_CAPS_CODEC("video/x-h263")
    GST_CAPS_CODEC("video/x-h264")
    GST_CAPS_CODEC("video/x-wmv")
    ;

static const char gst_vaapidecode_src_caps_str[] =
    GST_VAAPI_SURFACE_CAPS;

static GstStaticPadTemplate gst_vaapidecode_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapidecode_sink_caps_str));

static GstStaticPadTemplate gst_vaapidecode_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapidecode_src_caps_str));

#define GstVideoContextClass GstVideoContextInterface
GST_BOILERPLATE_WITH_INTERFACE(
    GstVaapiDecode,
    gst_vaapidecode,
    GstElement,
    GST_TYPE_ELEMENT,
    GstVideoContext,
    GST_TYPE_VIDEO_CONTEXT,
    gst_video_context);

enum {
    PROP_0,

    PROP_USE_FFMPEG,
};

static gboolean
gst_vaapidecode_update_src_caps(GstVaapiDecode *decode, GstCaps *caps);

static void
gst_vaapi_decoder_notify_caps(GObject *obj, GParamSpec *pspec, void *user_data)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(user_data);
    GstCaps *caps;

    g_assert(decode->decoder == GST_VAAPI_DECODER(obj));

    caps = gst_vaapi_decoder_get_caps(decode->decoder);
    gst_vaapidecode_update_src_caps(decode, caps);
}

static inline gboolean
gst_vaapidecode_update_sink_caps(GstVaapiDecode *decode, GstCaps *caps)
{
    if (decode->sinkpad_caps)
        gst_caps_unref(decode->sinkpad_caps);
    decode->sinkpad_caps = gst_caps_ref(caps);
    return TRUE;
}

static gboolean
gst_vaapidecode_update_src_caps(GstVaapiDecode *decode, GstCaps *caps)
{
    GstCaps *other_caps;
    GstStructure *structure;
    const GValue *v_width, *v_height, *v_framerate, *v_par, *v_interlaced;
    gboolean success;

    if (!decode->srcpad_caps) {
        decode->srcpad_caps = gst_caps_from_string(GST_VAAPI_SURFACE_CAPS_NAME);
        if (!decode->srcpad_caps)
            return FALSE;
    }

    structure    = gst_caps_get_structure(caps, 0);
    v_width      = gst_structure_get_value(structure, "width");
    v_height     = gst_structure_get_value(structure, "height");
    v_framerate  = gst_structure_get_value(structure, "framerate");
    v_par        = gst_structure_get_value(structure, "pixel-aspect-ratio");
    v_interlaced = gst_structure_get_value(structure, "interlaced");

    structure = gst_caps_get_structure(decode->srcpad_caps, 0);
    if (v_width && v_height) {
        gst_structure_set_value(structure, "width", v_width);
        gst_structure_set_value(structure, "height", v_height);
    }
    if (v_framerate)
        gst_structure_set_value(structure, "framerate", v_framerate);
    if (v_par)
        gst_structure_set_value(structure, "pixel-aspect-ratio", v_par);
    if (v_interlaced)
        gst_structure_set_value(structure, "interlaced", v_interlaced);

    gst_structure_set(structure, "type", G_TYPE_STRING, "vaapi", NULL);
    gst_structure_set(structure, "opengl", G_TYPE_BOOLEAN, USE_VAAPI_GLX, NULL);

    other_caps = gst_caps_copy(decode->srcpad_caps);
    success = gst_pad_set_caps(decode->srcpad, other_caps);
    gst_caps_unref(other_caps);
    return success;
}

static void
gst_vaapidecode_release(GstVaapiDecode *decode, GObject *dead_object)
{
    g_mutex_lock(decode->decoder_mutex);
    g_cond_signal(decode->decoder_ready);
    g_mutex_unlock(decode->decoder_mutex);
}

static GstFlowReturn
gst_vaapidecode_step(GstVaapiDecode *decode)
{
    GstVaapiSurfaceProxy *proxy;
    GstVaapiDecoderStatus status;
    GstBuffer *buffer;
    GstFlowReturn ret;
    guint tries;

    for (;;) {
        tries = 0;
    again:
        proxy = gst_vaapi_decoder_get_surface(decode->decoder, &status);
        if (!proxy) {
            if (status == GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE) {
                /* Wait for a VA surface to be displayed and free'd */
                if (++tries > 100)
                    goto error_decode_timeout;
                GTimeVal timeout;
                g_get_current_time(&timeout);
                g_time_val_add(&timeout, 10000); /* 10 ms each step */
                g_mutex_lock(decode->decoder_mutex);
                g_cond_timed_wait(
                    decode->decoder_ready,
                    decode->decoder_mutex,
                    &timeout
                );
                g_mutex_unlock(decode->decoder_mutex);
                goto again;
            }
            if (status != GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA)
                goto error_decode;
            /* More data is needed */
            break;
        }

        g_object_weak_ref(
            G_OBJECT(proxy),
            (GWeakNotify)gst_vaapidecode_release,
            decode
        );

        buffer = gst_vaapi_video_buffer_new(decode->display);
        if (!buffer)
            goto error_create_buffer;

        GST_BUFFER_TIMESTAMP(buffer) = GST_VAAPI_SURFACE_PROXY_TIMESTAMP(proxy);
        gst_buffer_set_caps(buffer, GST_PAD_CAPS(decode->srcpad));

        if (GST_VAAPI_SURFACE_PROXY_TFF(proxy))
            GST_BUFFER_FLAG_SET(buffer, GST_VIDEO_BUFFER_TFF);

        gst_vaapi_video_buffer_set_surface_proxy(
            GST_VAAPI_VIDEO_BUFFER(buffer),
            proxy
        );

        ret = gst_pad_push(decode->srcpad, buffer);
        if (ret != GST_FLOW_OK)
            goto error_commit_buffer;

        g_object_unref(proxy);
    }
    return GST_FLOW_OK;

    /* ERRORS */
error_decode_timeout:
    {
        GST_DEBUG("decode timeout. Decoder required a VA surface but none "
                  "got available within one second");
        return GST_FLOW_UNEXPECTED;
    }
error_decode:
    {
        GST_DEBUG("decode error %d", status);
        switch (status) {
        case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC:
        case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE:
        case GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT:
            ret = GST_FLOW_NOT_SUPPORTED;
            break;
        default:
            ret = GST_FLOW_UNEXPECTED;
            break;
        }
        return ret;
    }
error_create_buffer:
    {
        const GstVaapiID surface_id =
            gst_vaapi_surface_get_id(GST_VAAPI_SURFACE_PROXY_SURFACE(proxy));

        GST_DEBUG("video sink failed to create video buffer for proxy'ed "
                  "surface %" GST_VAAPI_ID_FORMAT " (error %d)",
                  GST_VAAPI_ID_ARGS(surface_id), ret);
        g_object_unref(proxy);
        return GST_FLOW_UNEXPECTED;
    }
error_commit_buffer:
    {
        GST_DEBUG("video sink rejected the video buffer (error %d)", ret);
        g_object_unref(proxy);
        return GST_FLOW_UNEXPECTED;
    }
}

static gboolean
gst_vaapidecode_create(GstVaapiDecode *decode, GstCaps *caps)
{
    VADisplay dpy;
    GstStructure *structure;
    int version;

    if (!gst_vaapi_ensure_display(decode, &decode->display))
        return FALSE;

    decode->decoder_mutex = g_mutex_new();
    if (!decode->decoder_mutex)
        return FALSE;

    decode->decoder_ready = g_cond_new();
    if (!decode->decoder_ready)
        return FALSE;

    dpy = decode->display;
    if (decode->use_ffmpeg) {
#if USE_FFMPEG
        decode->decoder = gst_vaapi_decoder_ffmpeg_new(dpy, caps);
#endif
    }
    else {
#if USE_CODEC_PARSERS
        structure = gst_caps_get_structure(caps, 0);
        if (!structure)
            return FALSE;
        if (gst_structure_has_name(structure, "video/x-h264"))
            decode->decoder = gst_vaapi_decoder_h264_new(dpy, caps);
        else if (gst_structure_has_name(structure, "video/mpeg")) {
            if (!gst_structure_get_int(structure, "mpegversion", &version))
                return FALSE;
            if (version == 2)
                decode->decoder = gst_vaapi_decoder_mpeg2_new(dpy, caps);
            else if (version == 4)
                decode->decoder = gst_vaapi_decoder_mpeg4_new(dpy, caps);
        }
        else if (gst_structure_has_name(structure, "video/x-wmv"))
            decode->decoder = gst_vaapi_decoder_vc1_new(dpy, caps);
        else if (gst_structure_has_name(structure, "video/x-h263") ||
                 gst_structure_has_name(structure, "video/x-divx") ||
                 gst_structure_has_name(structure, "video/x-xvid"))
            decode->decoder = gst_vaapi_decoder_mpeg4_new(dpy, caps);
#endif
    }
    if (!decode->decoder)
        return FALSE;

    g_signal_connect(
        G_OBJECT(decode->decoder),
        "notify::caps",
        G_CALLBACK(gst_vaapi_decoder_notify_caps),
        decode
    );

    decode->decoder_caps = gst_caps_ref(caps);
    return TRUE;
}

static void
gst_vaapidecode_destroy(GstVaapiDecode *decode)
{
    if (decode->decoder) {
        gst_vaapi_decoder_put_buffer(decode->decoder, NULL);
        g_object_unref(decode->decoder);
        decode->decoder = NULL;
    }

    if (decode->decoder_caps) {
        gst_caps_unref(decode->decoder_caps);
        decode->decoder_caps = NULL;
    }

    if (decode->decoder_ready) {
        gst_vaapidecode_release(decode, NULL);
        g_cond_free(decode->decoder_ready);
        decode->decoder_ready = NULL;
    }

    if (decode->decoder_mutex) {
        g_mutex_free(decode->decoder_mutex);
        decode->decoder_mutex = NULL;
    }
}

static gboolean
gst_vaapidecode_reset(GstVaapiDecode *decode, GstCaps *caps)
{
    if (decode->decoder &&
        decode->decoder_caps &&
        gst_caps_is_always_compatible(caps, decode->decoder_caps))
        return TRUE;

    gst_vaapidecode_destroy(decode);
    return gst_vaapidecode_create(decode, caps);
}

/* GstVideoContext interface */

static void
gst_vaapidecode_set_video_context(GstVideoContext *context, const gchar *type,
    const GValue *value)
{
    GstVaapiDecode *decode = GST_VAAPIDECODE (context);
    gst_vaapi_set_display (type, value, &decode->display);
}

static gboolean
gst_video_context_supported (GstVaapiDecode *decode, GType iface_type)
{
  return (iface_type == GST_TYPE_VIDEO_CONTEXT);
}

static void
gst_video_context_interface_init(GstVideoContextInterface *iface)
{
    iface->set_context = gst_vaapidecode_set_video_context;
}

static void
gst_vaapidecode_base_init(gpointer klass)
{
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);
    GstPadTemplate *pad_template;

    gst_element_class_set_details(element_class, &gst_vaapidecode_details);

    /* sink pad */
    pad_template = gst_static_pad_template_get(&gst_vaapidecode_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
    gst_object_unref(pad_template);

    /* src pad */
    pad_template = gst_static_pad_template_get(&gst_vaapidecode_src_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
    gst_object_unref(pad_template);
}

static void
gst_vaapidecode_finalize(GObject *object)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(object);

    gst_vaapidecode_destroy(decode);

    if (decode->sinkpad_caps) {
        gst_caps_unref(decode->sinkpad_caps);
        decode->sinkpad_caps = NULL;
    }

    if (decode->srcpad_caps) {
        gst_caps_unref(decode->srcpad_caps);
        decode->srcpad_caps = NULL;
    }

    if (decode->display) {
        g_object_unref(decode->display);
        decode->display = NULL;
    }

    if (decode->allowed_caps) {
        gst_caps_unref(decode->allowed_caps);
        decode->allowed_caps = NULL;
    }

    if (decode->delayed_new_seg) {
        gst_event_unref(decode->delayed_new_seg);
        decode->delayed_new_seg = NULL;
    }

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_vaapidecode_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(object);

    switch (prop_id) {
    case PROP_USE_FFMPEG:
        decode->use_ffmpeg = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapidecode_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(object);

    switch (prop_id) {
    case PROP_USE_FFMPEG:
        g_value_set_boolean(value, decode->use_ffmpeg);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static GstStateChangeReturn
gst_vaapidecode_change_state(GstElement *element, GstStateChange transition)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(element);
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        decode->is_ready = TRUE;
        break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        break;
    default:
        break;
    }

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
    if (ret != GST_STATE_CHANGE_SUCCESS)
        return ret;

    switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
        gst_vaapidecode_destroy(decode);
        if (decode->display) {
            g_object_unref(decode->display);
            decode->display = NULL;
        }
        decode->is_ready = FALSE;
        break;
    default:
        break;
    }
    return GST_STATE_CHANGE_SUCCESS;
}

static void
gst_vaapidecode_class_init(GstVaapiDecodeClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapidecode,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    object_class->finalize      = gst_vaapidecode_finalize;
    object_class->set_property  = gst_vaapidecode_set_property;
    object_class->get_property  = gst_vaapidecode_get_property;

    element_class->change_state = gst_vaapidecode_change_state;

#if USE_FFMPEG
    g_object_class_install_property
        (object_class,
         PROP_USE_FFMPEG,
         g_param_spec_boolean("use-ffmpeg",
                              "Use FFmpeg/VAAPI for decoding",
                              "Uses FFmpeg/VAAPI for decoding",
                              USE_FFMPEG_DEFAULT,
                              G_PARAM_READWRITE));
#endif
}

static gboolean
gst_vaapidecode_ensure_allowed_caps(GstVaapiDecode *decode)
{
    GstCaps *decode_caps;
    guint i, n_decode_caps;

    if (decode->allowed_caps)
        return TRUE;

    if (!gst_vaapi_ensure_display(decode, &decode->display))
        goto error_no_display;

    decode_caps = gst_vaapi_display_get_decode_caps(decode->display);
    if (!decode_caps)
        goto error_no_decode_caps;
    n_decode_caps = gst_caps_get_size(decode_caps);

    decode->allowed_caps = gst_caps_new_empty();
    if (!decode->allowed_caps)
        goto error_no_memory;

    for (i = 0; i < n_decode_caps; i++) {
        GstStructure *structure;
        structure = gst_caps_get_structure(decode_caps, i);
        if (!structure)
            continue;
        structure = gst_structure_copy(structure);
        if (!structure)
            continue;
        gst_structure_remove_field(structure, "profile");
        gst_structure_set(
            structure,
            "width",  GST_TYPE_INT_RANGE, 1, G_MAXINT,
            "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
            NULL
        );
        gst_caps_merge_structure(decode->allowed_caps, structure);
    }

    gst_caps_unref(decode_caps);
    return TRUE;

    /* ERRORS */
error_no_display:
    {
        GST_DEBUG("failed to retrieve VA display");
        return FALSE;
    }
error_no_decode_caps:
    {
        GST_DEBUG("failed to retrieve VA decode caps");
        return FALSE;
    }
error_no_memory:
    {
        GST_DEBUG("failed to allocate allowed-caps set");
        gst_caps_unref(decode_caps);
        return FALSE;
    }
}

static GstCaps *
gst_vaapidecode_get_caps(GstPad *pad)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(GST_OBJECT_PARENT(pad));

    if (!decode->is_ready)
        return gst_static_pad_template_get_caps(&gst_vaapidecode_sink_factory);

    if (!gst_vaapidecode_ensure_allowed_caps(decode))
        return gst_caps_new_empty();

    return gst_caps_ref(decode->allowed_caps);
}

static gboolean
gst_vaapidecode_set_caps(GstPad *pad, GstCaps *caps)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(GST_OBJECT_PARENT(pad));

    g_return_val_if_fail(pad == decode->sinkpad, FALSE);

    if (!gst_vaapidecode_update_sink_caps(decode, caps))
        return FALSE;
    if (!gst_vaapidecode_update_src_caps(decode, caps))
        return FALSE;
    if (!gst_vaapidecode_reset(decode, decode->sinkpad_caps))
        return FALSE;

    /* Propagate NEWSEGMENT event downstream, now that pads are linked */
    if (decode->delayed_new_seg) {
        if (gst_pad_push_event(decode->srcpad, decode->delayed_new_seg))
            gst_event_unref(decode->delayed_new_seg);
        decode->delayed_new_seg = NULL;
    }
    return TRUE;
}

static GstFlowReturn
gst_vaapidecode_chain(GstPad *pad, GstBuffer *buf)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(GST_OBJECT_PARENT(pad));

    if (!gst_vaapi_decoder_put_buffer(decode->decoder, buf))
        goto error_push_buffer;

    gst_buffer_unref(buf);
    return gst_vaapidecode_step(decode);

    /* ERRORS */
error_push_buffer:
    {
        GST_DEBUG("failed to push input buffer to decoder");
        gst_buffer_unref(buf);
        return GST_FLOW_UNEXPECTED;
    }
}

static gboolean
gst_vaapidecode_sink_event(GstPad *pad, GstEvent *event)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(GST_OBJECT_PARENT(pad));

    GST_DEBUG("handle sink event '%s'", GST_EVENT_TYPE_NAME(event));

    /* Propagate event downstream */
    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_NEWSEGMENT:
        if (decode->delayed_new_seg) {
            gst_event_unref(decode->delayed_new_seg);
            decode->delayed_new_seg = NULL;
        }
        if (!GST_PAD_PEER(decode->srcpad)) {
            decode->delayed_new_seg = gst_event_ref(event);
            return TRUE;
        }
        break;
    default:
        break;
    }
    return gst_pad_push_event(decode->srcpad, event);
}

static gboolean
gst_vaapidecode_src_event(GstPad *pad, GstEvent *event)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(GST_OBJECT_PARENT(pad));

    GST_DEBUG("handle src event '%s'", GST_EVENT_TYPE_NAME(event));

    /* Propagate event upstream */
    return gst_pad_push_event(decode->sinkpad, event);
}

static gboolean
gst_vaapidecode_query (GstPad *pad, GstQuery *query) {
    GstVaapiDecode *decode = GST_VAAPIDECODE (gst_pad_get_parent_element (pad));
    gboolean res;

    GST_DEBUG ("sharing display %p", decode->display);

    if (gst_vaapi_reply_to_query (query, decode->display))
      res = TRUE;
    else
      res = gst_pad_query_default (pad, query);

    g_object_unref (decode);
    return res;
}

static void
gst_vaapidecode_init(GstVaapiDecode *decode, GstVaapiDecodeClass *klass)
{
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

    decode->display             = NULL;
    decode->decoder             = NULL;
    decode->decoder_mutex       = NULL;
    decode->decoder_ready       = NULL;
    decode->decoder_caps        = NULL;
    decode->allowed_caps        = NULL;
    decode->delayed_new_seg     = NULL;
    decode->use_ffmpeg          = USE_FFMPEG_DEFAULT;
    decode->is_ready            = FALSE;

    /* Pad through which data comes in to the element */
    decode->sinkpad = gst_pad_new_from_template(
        gst_element_class_get_pad_template(element_class, "sink"),
        "sink"
    );
    decode->sinkpad_caps = NULL;

    gst_pad_set_getcaps_function(decode->sinkpad, gst_vaapidecode_get_caps);
    gst_pad_set_setcaps_function(decode->sinkpad, gst_vaapidecode_set_caps);
    gst_pad_set_chain_function(decode->sinkpad, gst_vaapidecode_chain);
    gst_pad_set_event_function(decode->sinkpad, gst_vaapidecode_sink_event);
    gst_pad_set_query_function(decode->sinkpad, gst_vaapidecode_query);
    gst_element_add_pad(GST_ELEMENT(decode), decode->sinkpad);

    /* Pad through which data goes out of the element */
    decode->srcpad = gst_pad_new_from_template(
        gst_element_class_get_pad_template(element_class, "src"),
        "src"
    );
    decode->srcpad_caps = NULL;

    gst_pad_use_fixed_caps(decode->srcpad);
    gst_pad_set_event_function(decode->srcpad, gst_vaapidecode_src_event);
    gst_pad_set_query_function(decode->srcpad, gst_vaapidecode_query);
    gst_element_add_pad(GST_ELEMENT(decode), decode->srcpad);
}
