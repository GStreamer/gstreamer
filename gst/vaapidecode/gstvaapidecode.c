/*
 *  gstvaapidecode.c - VA-API video decoder
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
 * SECTION:gstvaapidecode
 * @short_description: A VA-API based video decoder
 *
 * vaapidecode decodes from raw bitstreams to surfaces suitable for
 * the vaapisink element.
 */

#include "config.h"
#include "gstvaapidecode.h"
#include <gst/vaapi/gstvaapivideosink.h>
#include <gst/vaapi/gstvaapivideobuffer.h>
#include <gst/vaapi/gstvaapidecoder_ffmpeg.h>

#define GST_PLUGIN_NAME "vaapidecode"
#define GST_PLUGIN_DESC "A VA-API based video decoder"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapidecode);
#define GST_CAT_DEFAULT gst_debug_vaapidecode

/* ElementFactory information */
static const GstElementDetails gst_vaapidecode_details =
    GST_ELEMENT_DETAILS(
        "Video decode",
        "Codec/Decoder/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gbeauchesne@splitted-desktop.com>");

/* Default templates */
#define GST_CAPS_CODEC(CODEC)                   \
    CODEC ", "                                  \
    "width = (int) [ 1, MAX ], "                \
    "height = (int) [ 1, MAX ]; "

static const char gst_vaapidecode_sink_caps_str[] =
    GST_CAPS_CODEC("video/mpeg, mpegversion=2")
    GST_CAPS_CODEC("video/mpeg, mpegversion=4")
    GST_CAPS_CODEC("video/x-divx")
    GST_CAPS_CODEC("video/x-xvid")
    GST_CAPS_CODEC("video/x-h263")
    GST_CAPS_CODEC("video/x-h264")
    GST_CAPS_CODEC("video/x-wmv")
    ;

static const char gst_vaapidecode_src_caps_str[] =
    "video/x-vaapi-surface, "
    "width = (int) [ 1, MAX ], "
    "height = (int) [ 1, MAX ]; ";

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

GST_BOILERPLATE(
    GstVaapiDecode,
    gst_vaapidecode,
    GstElement,
    GST_TYPE_ELEMENT);

enum {
    PROP_0,

    PROP_USE_FFMPEG,
};

static GstFlowReturn
gst_vaapidecode_step(GstVaapiDecode *decode)
{
    GstVaapiSurfaceProxy *proxy;
    GstVaapiDecoderStatus status;
    GstBuffer *buffer;
    GstFlowReturn ret;

    for (;;) {
        proxy = gst_vaapi_decoder_get_surface(decode->decoder, &status);
        if (!proxy) {
            if (status != GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA)
                goto error_decode;
            /* More data is needed */
            break;
        }

        buffer = NULL;
        ret = gst_pad_alloc_buffer(
            decode->srcpad,
            0, 0,
            GST_PAD_CAPS(decode->srcpad),
            &buffer
        );
        if (ret != GST_FLOW_OK || !buffer)
            goto error_create_buffer;

        GST_BUFFER_TIMESTAMP(buffer) = GST_VAAPI_SURFACE_PROXY_TIMESTAMP(proxy);
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
error_decode:
    {
        GST_DEBUG("decode error %d", status);
        return GST_FLOW_UNEXPECTED;
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
gst_vaapidecode_ensure_display(GstVaapiDecode *decode)
{
    GstVaapiVideoSink *sink;
    GstVaapiDisplay *display;

    if (decode->display)
        return TRUE;

    /* Look for a downstream vaapisink */
    sink = gst_vaapi_video_sink_lookup(GST_ELEMENT(decode));
    if (!sink)
        return FALSE;

    display = gst_vaapi_video_sink_get_display(sink);
    if (!display)
        return FALSE;

    decode->display = g_object_ref(display);
    return TRUE;
}

static gboolean
gst_vaapidecode_create(GstVaapiDecode *decode)
{
    if (!gst_vaapidecode_ensure_display(decode))
        return FALSE;

    if (decode->use_ffmpeg)
        decode->decoder =
            gst_vaapi_decoder_ffmpeg_new(decode->display, decode->decoder_caps);
    return decode->decoder != NULL;
}

static void
gst_vaapidecode_destroy(GstVaapiDecode *decode)
{
    if (decode->decoder_caps) {
        gst_caps_unref(decode->decoder_caps);
        decode->decoder_caps = NULL;
    }

    if (decode->decoder) {
        gst_vaapi_decoder_put_buffer(decode->decoder, NULL);
        g_object_unref(decode->decoder);
        decode->decoder = NULL;
    }

    if (decode->display) {
        g_object_unref(decode->display);
        decode->display = NULL;
    }
}

static void gst_vaapidecode_base_init(gpointer klass)
{
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_set_details(element_class, &gst_vaapidecode_details);

    /* sink pad */
    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&gst_vaapidecode_sink_factory)
    );

    /* src pad */
    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&gst_vaapidecode_src_factory)
    );
}

static void
gst_vaapidecode_finalize(GObject *object)
{
    gst_vaapidecode_destroy(GST_VAAPIDECODE(object));

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

    object_class->finalize      = gst_vaapidecode_finalize;
    object_class->set_property  = gst_vaapidecode_set_property;
    object_class->get_property  = gst_vaapidecode_get_property;

    element_class->change_state = gst_vaapidecode_change_state;

    g_object_class_install_property
        (object_class,
         PROP_USE_FFMPEG,
         g_param_spec_boolean("use-ffmpeg",
                              "Use FFmpeg/VAAPI for decoding",
                              "Uses FFmpeg/VAAPI for decoding",
                              TRUE,
                              G_PARAM_READWRITE));
}

static gboolean
gst_vaapidecode_set_caps(GstPad *pad, GstCaps *caps)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(GST_OBJECT_PARENT(pad));
    GstPad *other_pad;
    GstCaps *other_caps = NULL;
    GstStructure *structure;
    const GValue *v_width, *v_height, *v_framerate, *v_par;

    if (pad == decode->sinkpad) {
        other_pad  = decode->srcpad;
        other_caps = gst_caps_from_string(gst_vaapidecode_src_caps_str);
    }
    else {
        other_pad  = decode->sinkpad;
        other_caps = gst_caps_from_string(gst_vaapidecode_sink_caps_str);
    }

    /* Negotiation succeeded, so now configure ourselves */
    structure    = gst_caps_get_structure(caps, 0);
    v_width      = gst_structure_get_value(structure, "width");
    v_height     = gst_structure_get_value(structure, "height");
    v_framerate  = gst_structure_get_value(structure, "framerate");
    v_par        = gst_structure_get_value(structure, "pixel-aspect-ratio");

    if (pad == decode->sinkpad)
        decode->decoder_caps = gst_caps_ref(caps);

    structure = gst_caps_get_structure(other_caps, 0);
    gst_structure_set_value(structure, "width", v_width);
    gst_structure_set_value(structure, "height", v_height);
    if (v_framerate)
        gst_structure_set_value(structure, "framerate", v_framerate);
    if (v_par)
        gst_structure_set_value(structure, "pixel-aspect-ratio", v_par);

    return gst_pad_set_caps(other_pad, other_caps);
}

static GstFlowReturn
gst_vaapidecode_chain(GstPad *pad, GstBuffer *buf)
{
    GstVaapiDecode * const decode = GST_VAAPIDECODE(GST_OBJECT_PARENT(pad));

    if (!decode->decoder) {
        if (!gst_vaapidecode_create(decode))
            goto error_create_decoder;
    }

    if (!gst_vaapi_decoder_put_buffer(decode->decoder, buf))
        goto error_push_buffer;

    gst_buffer_unref(buf);
    return gst_vaapidecode_step(decode);

    /* ERRORS */
error_create_decoder:
    {
        GST_DEBUG("failed to create decoder");
        gst_buffer_unref(buf);
        return GST_FLOW_UNEXPECTED;
    }
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

static void
gst_vaapidecode_init(GstVaapiDecode *decode, GstVaapiDecodeClass *klass)
{
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

    decode->display      = NULL;
    decode->decoder      = NULL;
    decode->decoder_caps = NULL;
    decode->use_ffmpeg   = TRUE;

    /* Pad through which data comes in to the element */
    decode->sinkpad = gst_pad_new_from_template(
        gst_element_class_get_pad_template(element_class, "sink"),
        "sink"
    );

    gst_pad_set_setcaps_function(decode->sinkpad, gst_vaapidecode_set_caps);
    gst_pad_set_chain_function(decode->sinkpad, gst_vaapidecode_chain);
    gst_pad_set_event_function(decode->sinkpad, gst_vaapidecode_sink_event);
    gst_element_add_pad(GST_ELEMENT(decode), decode->sinkpad);

    /* Pad through which data goes out of the element */
    decode->srcpad = gst_pad_new_from_template(
        gst_element_class_get_pad_template(element_class, "src"),
        "src"
    );

    gst_pad_set_event_function(decode->srcpad, gst_vaapidecode_src_event);
    gst_element_add_pad(GST_ELEMENT(decode), decode->srcpad);
}

static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapidecode,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    return gst_element_register(plugin,
                                GST_PLUGIN_NAME,
                                GST_RANK_NONE,
                                GST_TYPE_VAAPIDECODE);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR, GST_VERSION_MINOR,
    GST_PLUGIN_NAME,
    GST_PLUGIN_DESC,
    plugin_init,
    PACKAGE_VERSION,
    "GPL",
    PACKAGE,
    PACKAGE_BUGREPORT);
