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
#include "gstvaapisink.h"

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

GST_BOILERPLATE(GstVaapiSink, gst_vaapisink, GstVideoSink, GST_TYPE_VIDEO_SINK);

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
    GstElementClass * const   element_class   = GST_ELEMENT_CLASS(klass);
    GstBaseSinkClass * const  basesink_class  = GST_BASE_SINK_CLASS(klass);
    GstVideoSinkClass * const videosink_class = GST_VIDEO_SINK_CLASS(klass);
}

static void gst_vaapisink_init(GstVaapiSink *sink, GstVaapiSinkClass *klass)
{
}

static gboolean plugin_init(GstPlugin *plugin)
{
    return gst_element_register(plugin,
                                "vaapisink",
                                GST_RANK_PRIMARY,
                                GST_TYPE_VAAPISINK);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR, GST_VERSION_MINOR,
    "vaapisink",
    "A VA-API based videosink",
    plugin_init,
    PACKAGE_VERSION,
    "GPL",
    PACKAGE,
    PACKAGE_BUGREPORT);
