/*
 *  gstvaapiconvert.c - VA-API video converter
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
#include "gstvaapiconvert.h"

/* ElementFactory information */
static const GstElementDetails gst_vaapiconvert_details =
    GST_ELEMENT_DETAILS(
        "Video convert",
        "Convert/Video",
        "A VA-API based videoconvert",
        "Gwenole Beauchesne <gbeauchesne@splitted-desktop.com>");

/* Default templates */
static GstStaticPadTemplate gst_vaapiconvert_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(
            "video/x-raw-yuv, "
            "width = (int) [ 1, MAX ], "
            "height = (int) [ 1, MAX ]; "));

static GstStaticPadTemplate gst_vaapiconvert_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(
            "video/x-vaapi-surface, "
            "width = (int) [ 1, MAX ], "
            "height = (int) [ 1, MAX ]; "));

GST_BOILERPLATE(
    GstVaapiConvert,
    gst_vaapiconvert,
    GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static GstFlowReturn
gst_vaapiconvert_transform(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    GstBuffer        *outbuf
);

static void gst_vaapiconvert_base_init(gpointer klass)
{
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_set_details(element_class, &gst_vaapiconvert_details);

    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&gst_vaapiconvert_sink_factory)
    );
    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&gst_vaapiconvert_src_factory)
    );
}

static void gst_vaapiconvert_class_init(GstVaapiConvertClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass * const trans_class = GST_BASE_TRANSFORM_CLASS(klass);

    trans_class->transform = GST_DEBUG_FUNCPTR(gst_vaapiconvert_transform);
}

static void
gst_vaapiconvert_init(GstVaapiConvert *convert, GstVaapiConvertClass *klass)
{
}

static GstFlowReturn
gst_vaapiconvert_transform(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    GstBuffer        *outbuf
)
{
    return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin)
{
    return gst_element_register(plugin,
                                "vaapiconvert",
                                GST_RANK_PRIMARY,
                                GST_TYPE_VAAPICONVERT);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR, GST_VERSION_MINOR,
    "vaapiconvert",
    "A VA-API based video pixels format converter",
    plugin_init,
    PACKAGE_VERSION,
    "GPL",
    PACKAGE,
    PACKAGE_BUGREPORT);
