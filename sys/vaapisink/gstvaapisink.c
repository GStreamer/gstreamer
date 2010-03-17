/*
 *  gstvaapisink.c - VA-API video sink
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

#include "config.h"
#include <gst/gst.h>
#include <gst/vaapi/gstvaapivideobuffer.h>
#include <gst/vaapi/gstvaapivideosink.h>
#include <gst/vaapi/gstvaapidisplay_x11.h>
#include <gst/vaapi/gstvaapiwindow_x11.h>
#include "gstvaapisink.h"

#define GST_PLUGIN_NAME "vaapisink"
#define GST_PLUGIN_DESC "A VA-API based videosink"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapisink);
#define GST_CAT_DEFAULT gst_debug_vaapisink

/* ElementFactory information */
static const GstElementDetails gst_vaapisink_details =
    GST_ELEMENT_DETAILS(
        "Video sink",
        "Sink/Video",
        "A VA-API based videosink",
        "Gwenole Beauchesne <gbeauchesne@splitted-desktop.com>");

/* Default template */
static GstStaticPadTemplate gst_vaapisink_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(
            "video/x-vaapi-surface, "
            "width = (int) [ 1, MAX ], "
            "height = (int) [ 1, MAX ]; "));

static void gst_vaapisink_iface_init(GType type);

GST_BOILERPLATE_FULL(
    GstVaapiSink,
    gst_vaapisink,
    GstVideoSink,
    GST_TYPE_VIDEO_SINK,
    gst_vaapisink_iface_init);

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_DISPLAY_NAME,
};

static GstVaapiDisplay *
gst_vaapi_video_sink_do_get_display(GstVaapiVideoSink *sink)
{
    return gst_vaapisink_get_display(GST_VAAPISINK(sink));
}

static void gst_vaapi_video_sink_iface_init(GstVaapiVideoSinkInterface *iface)
{
    iface->get_display = gst_vaapi_video_sink_do_get_display;
}

static void gst_vaapisink_iface_init(GType type)
{
    const GType g_define_type_id = type;

    G_IMPLEMENT_INTERFACE(GST_VAAPI_TYPE_VIDEO_SINK,
                          gst_vaapi_video_sink_iface_init);
}

static void
gst_vaapisink_destroy(GstVaapiSink *sink)
{
    if (sink->display) {
        g_object_unref(sink->display);
        sink->display = NULL;
    }

    if (sink->display_name) {
        g_free(sink->display_name);
        sink->display_name = NULL;
    }
}

static inline gboolean
gst_vaapisink_ensure_display(GstVaapiSink *sink)
{
    if (!sink->display)
        sink->display = gst_vaapi_display_x11_new(sink->display_name);
    return sink->display != NULL;
}

static gboolean
gst_vaapisink_start(GstBaseSink *base_sink)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);

    if (!gst_vaapisink_ensure_display(sink))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapisink_stop(GstBaseSink *base_sink)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);

    if (sink->window) {
        g_object_unref(sink->window);
        sink->window = NULL;
    }

    if (sink->display) {
        g_object_unref(sink->display);
        sink->display = NULL;
    }
    return TRUE;
}

static gboolean
gst_vaapisink_set_caps(GstBaseSink *base_sink, GstCaps *caps)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    gint width, height;

    if (!structure)
        return FALSE;
    if (!gst_structure_get_int(structure, "width",  &width))
        return FALSE;
    if (!gst_structure_get_int(structure, "height", &height))
        return FALSE;

    if (sink->window)
        gst_vaapi_window_set_size(sink->window, width, height);
    else {
        sink->window = gst_vaapi_window_x11_new(sink->display, width, height);
        if (!sink->window)
            return FALSE;
        gst_vaapi_window_show(sink->window);
    }
    return TRUE;
}

static GstFlowReturn
gst_vaapisink_show_frame(GstBaseSink *base_sink, GstBuffer *buffer)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstVaapiVideoBuffer * const vbuffer = GST_VAAPI_VIDEO_BUFFER(buffer);
    GstVaapiSurface *surface;
    guint flags;

    if (!sink->window)
        return GST_FLOW_UNEXPECTED;

    surface = gst_vaapi_video_buffer_get_surface(vbuffer);
    if (!surface)
        return GST_FLOW_UNEXPECTED;

    flags = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
    if (!gst_vaapi_window_put_surface(sink->window, surface, flags))
        return GST_FLOW_UNEXPECTED;

    return GST_FLOW_OK;
}

static void
gst_vaapisink_finalize(GObject *object)
{
    gst_vaapisink_destroy(GST_VAAPISINK(object));

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_vaapisink_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiSink * const sink = GST_VAAPISINK(object);

    switch (prop_id) {
    case PROP_DISPLAY_NAME:
        g_free(sink->display_name);
        sink->display_name = g_strdup(g_value_get_string(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapisink_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiSink * const sink = GST_VAAPISINK(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_object(value, sink->display);
        break;
    case PROP_DISPLAY_NAME:
        g_value_set_string(value, sink->display_name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_vaapisink_base_init(gpointer klass)
{
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_set_details(element_class, &gst_vaapisink_details);

    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&gst_vaapisink_sink_factory)
    );
}

static void gst_vaapisink_class_init(GstVaapiSinkClass *klass)
{
    GObjectClass * const      object_class    = G_OBJECT_CLASS(klass);
    GstBaseSinkClass * const  basesink_class  = GST_BASE_SINK_CLASS(klass);

    object_class->finalize      = gst_vaapisink_finalize;
    object_class->set_property  = gst_vaapisink_set_property;
    object_class->get_property  = gst_vaapisink_get_property;

    basesink_class->start       = gst_vaapisink_start;
    basesink_class->stop        = gst_vaapisink_stop;
    basesink_class->set_caps    = gst_vaapisink_set_caps;
    basesink_class->preroll     = gst_vaapisink_show_frame;
    basesink_class->render      = gst_vaapisink_show_frame;

    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_object("display",
                             "display",
                             "display",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READABLE));

    g_object_class_install_property
        (object_class,
         PROP_DISPLAY_NAME,
         g_param_spec_string("display-name",
                             "X11 display name",
                             "X11 display name",
                             "",
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void gst_vaapisink_init(GstVaapiSink *sink, GstVaapiSinkClass *klass)
{
    sink->display_name  = NULL;
    sink->display       = NULL;
}

GstVaapiDisplay *
gst_vaapisink_get_display(GstVaapiSink *sink)
{
    if (!gst_vaapisink_ensure_display(sink))
        return NULL;
    return sink->display;
}

static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapisink,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    return gst_element_register(plugin,
                                GST_PLUGIN_NAME,
                                GST_RANK_PRIMARY,
                                GST_TYPE_VAAPISINK);
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
