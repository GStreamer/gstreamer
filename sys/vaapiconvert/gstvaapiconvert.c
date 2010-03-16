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
#include <gst/video/video.h>
#include <gst/vaapi/gstvaapivideosink.h>
#include "gstvaapiconvert.h"

#define GST_PLUGIN_NAME "vaapiconvert"
#define GST_PLUGIN_DESC "A VA-API based video pixels format converter"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapiconvert);
#define GST_CAT_DEFAULT gst_debug_vaapiconvert

/* ElementFactory information */
static const GstElementDetails gst_vaapiconvert_details =
    GST_ELEMENT_DETAILS(
        "Video convert",
        "Convert/Video",
        "A VA-API based videoconvert",
        "Gwenole Beauchesne <gbeauchesne@splitted-desktop.com>");

/* Default templates */
static const char gst_vaapiconvert_yuv_caps_str[] =
    "video/x-raw-yuv, "
    "width = (int) [ 1, MAX ], "
    "height = (int) [ 1, MAX ]; ";

static const char gst_vaapiconvert_vaapi_caps_str[] =
    "video/x-vaapi-surface, "
    "width = (int) [ 1, MAX ], "
    "height = (int) [ 1, MAX ]; ";

static GstStaticPadTemplate gst_vaapiconvert_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapiconvert_yuv_caps_str));

static GstStaticPadTemplate gst_vaapiconvert_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapiconvert_vaapi_caps_str));

GST_BOILERPLATE(
    GstVaapiConvert,
    gst_vaapiconvert,
    GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static gboolean gst_vaapiconvert_start(GstBaseTransform *trans);
static gboolean gst_vaapiconvert_stop(GstBaseTransform *trans);

static GstFlowReturn
gst_vaapiconvert_transform(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    GstBuffer        *outbuf
);

static GstCaps *
gst_vaapiconvert_transform_caps(
    GstBaseTransform *trans,
    GstPadDirection   direction,
    GstCaps          *caps
);

static gboolean
gst_vaapiconvert_set_caps(
    GstBaseTransform *trans,
    GstCaps          *incaps,
    GstCaps          *outcaps
);

static gboolean
gst_vaapiconvert_get_unit_size(
    GstBaseTransform *trans,
    GstCaps          *caps,
    guint            *size
);

static GstFlowReturn
gst_vaapiconvert_sinkpad_buffer_alloc(
    GstPad           *pad,
    guint64           offset,
    guint             size,
    GstCaps          *caps,
    GstBuffer       **pbuf
);

static GstFlowReturn
gst_vaapiconvert_prepare_output_buffer(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    gint              size,
    GstCaps          *caps,
    GstBuffer       **poutbuf
);

static void
gst_vaapiconvert_destroy(GstVaapiConvert *convert)
{
    if (convert->images) {
        g_object_unref(convert->images);
        convert->images = NULL;
    }

    if (convert->surfaces) {
        g_object_unref(convert->surfaces);
        convert->surfaces = NULL;
    }

    if (convert->display) {
        g_object_unref(convert->display);
        convert->display = NULL;
    }
}

static void gst_vaapiconvert_base_init(gpointer klass)
{
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_set_details(element_class, &gst_vaapiconvert_details);

    /* sink pad */
    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&gst_vaapiconvert_sink_factory)
    );

    /* src pad */
    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&gst_vaapiconvert_src_factory)
    );
}

static void
gst_vaapiconvert_finalize(GObject *object)
{
    gst_vaapiconvert_destroy(GST_VAAPICONVERT(object));

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_vaapiconvert_class_init(GstVaapiConvertClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass * const trans_class = GST_BASE_TRANSFORM_CLASS(klass);

    object_class->finalize      = gst_vaapiconvert_finalize;

    trans_class->start          = gst_vaapiconvert_start;
    trans_class->stop           = gst_vaapiconvert_stop;
    trans_class->transform      = gst_vaapiconvert_transform;
    trans_class->transform_caps = gst_vaapiconvert_transform_caps;
    trans_class->set_caps       = gst_vaapiconvert_set_caps;
    trans_class->get_unit_size  = gst_vaapiconvert_get_unit_size;
    trans_class->prepare_output_buffer = gst_vaapiconvert_prepare_output_buffer;
}

static void
gst_vaapiconvert_init(GstVaapiConvert *convert, GstVaapiConvertClass *klass)
{
    GstPad *sinkpad;

    convert->display            = NULL;
    convert->images             = NULL;
    convert->image_width        = 0;
    convert->image_height       = 0;
    convert->surfaces           = NULL;
    convert->surface_width      = 0;
    convert->surface_height     = 0;

    /* Override buffer allocator on sink pad */
    sinkpad = gst_element_get_static_pad(GST_ELEMENT(convert), "sink");
    gst_pad_set_bufferalloc_function(
        sinkpad,
        gst_vaapiconvert_sinkpad_buffer_alloc
    );
    g_object_unref(sinkpad);
}

static gboolean gst_vaapiconvert_start(GstBaseTransform *trans)
{
    GstVaapiConvert * const convert = GST_VAAPICONVERT(trans);
    GstVaapiVideoSink *sink;
    GstVaapiDisplay *display;

    /* Look for a downstream vaapisink */
    sink = gst_vaapi_video_sink_lookup(GST_ELEMENT(trans));
    if (!sink)
        return FALSE;

    display = gst_vaapi_video_sink_get_display(sink);
    if (!display)
        return FALSE;

    convert->display = g_object_ref(display);
    return TRUE;
}

static gboolean gst_vaapiconvert_stop(GstBaseTransform *trans)
{
    GstVaapiConvert * const convert = GST_VAAPICONVERT(trans);

    if (convert->display) {
        g_object_unref(convert->display);
        convert->display = NULL;
    }
    return TRUE;
}

static GstFlowReturn
gst_vaapiconvert_transform(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    GstBuffer        *outbuf
)
{
    GstVaapiConvert * const convert = GST_VAAPICONVERT(trans);
    GstVaapiVideoBuffer * const vbuffer = GST_VAAPI_VIDEO_BUFFER(outbuf);
    GstVaapiSurface *surface;
    GstVaapiImage *image;

    image = gst_vaapi_video_pool_get_object(convert->images);
    if (!image)
        return GST_FLOW_UNEXPECTED;

    surface = gst_vaapi_video_buffer_get_surface(vbuffer);
    if (!surface)
        return GST_FLOW_UNEXPECTED;

    gst_vaapi_image_update_from_buffer(image, inbuf);
    gst_vaapi_surface_put_image(surface, image);
    gst_vaapi_video_pool_put_object(convert->images, image);
    return GST_FLOW_OK;
}

static GstCaps *
gst_vaapiconvert_transform_caps(
    GstBaseTransform *trans,
    GstPadDirection   direction,
    GstCaps          *caps
)
{
    GstVaapiConvert * const convert = GST_VAAPICONVERT(trans);
    GstCaps *out_caps = NULL;
    GstStructure *structure;
    const GValue *v_width, *v_height, *v_framerate, *v_par;

    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    structure   = gst_caps_get_structure(caps, 0);
    v_width     = gst_structure_get_value(structure, "width");
    v_height    = gst_structure_get_value(structure, "height");
    v_framerate = gst_structure_get_value(structure, "framerate");
    v_par       = gst_structure_get_value(structure, "pixel-aspect-ratio");

    if (!v_width || !v_height)
        return NULL;

    if (direction == GST_PAD_SINK) {
        if (!gst_structure_has_name(structure, "video/x-raw-yuv"))
            return NULL;
        out_caps = gst_caps_from_string(gst_vaapiconvert_vaapi_caps_str);
    }
    else {
        if (!gst_structure_has_name(structure, "video/x-vaapi-surface"))
            return NULL;
        out_caps = gst_caps_from_string(gst_vaapiconvert_yuv_caps_str);
        if (convert->display) {
            GstCaps *allowed_caps, *inter_caps;
            allowed_caps = gst_vaapi_display_get_image_caps(convert->display);
            if (!allowed_caps)
                return NULL;
            inter_caps = gst_caps_intersect(out_caps, allowed_caps);
            gst_caps_unref(allowed_caps);
            gst_caps_unref(out_caps);
            out_caps = inter_caps;
        }
    }

    structure = gst_caps_get_structure(out_caps, 0);
    gst_structure_set_value(structure, "width", v_width);
    gst_structure_set_value(structure, "height", v_height);
    if (v_framerate)
        gst_structure_set_value(structure, "framerate", v_framerate);
    if (v_par)
        gst_structure_set_value(structure, "pixel-aspect-ratio", v_par);
    return out_caps;
}

static gboolean
gst_vaapiconvert_set_caps(
    GstBaseTransform *trans,
    GstCaps          *incaps,
    GstCaps          *outcaps
)
{
    GstVaapiConvert * const convert = GST_VAAPICONVERT(trans);
    GstStructure *structure;
    gint width, height;

    structure = gst_caps_get_structure(incaps, 0);
    gst_structure_get_int(structure, "width",  &width);
    gst_structure_get_int(structure, "height", &height);

    if (width != convert->image_width || height != convert->image_height) {
        convert->image_width  = width;
        convert->image_height = height;
        if (convert->images)
            g_object_unref(convert->images);
        convert->images = gst_vaapi_image_pool_new(convert->display, incaps);
        if (!convert->images)
            return FALSE;
    }

    structure = gst_caps_get_structure(outcaps, 0);
    gst_structure_get_int(structure, "width",  &width);
    gst_structure_get_int(structure, "height", &height);

    if (width != convert->surface_width || height != convert->surface_height) {
        convert->surface_width  = width;
        convert->surface_height = height;
        if (convert->surfaces)
            g_object_unref(convert->surfaces);
        convert->surfaces = gst_vaapi_surface_pool_new(convert->display, outcaps);
        if (!convert->surfaces)
            return FALSE;
    }
    return TRUE;
}

static gboolean
gst_vaapiconvert_get_unit_size(
    GstBaseTransform *trans,
    GstCaps          *caps,
    guint            *size
)
{
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    GstVideoFormat format;
    gint width, height;

    if (gst_structure_has_name(structure, "video/x-vaapi-surface"))
        *size = 0;
    else {
        if (!gst_video_format_parse_caps(caps, &format, &width, &height))
            return FALSE;
        *size = gst_video_format_get_size(format, width, height);
    }
    return TRUE;
}

static GstFlowReturn
gst_vaapiconvert_buffer_alloc(
    GstBaseTransform *trans,
    guint             size,
    GstCaps          *caps,
    GstBuffer       **pbuf
)
{
    return GST_FLOW_OK;
}

static GstFlowReturn
gst_vaapiconvert_sinkpad_buffer_alloc(
    GstPad           *pad,
    guint64           offset,
    guint             size,
    GstCaps          *caps,
    GstBuffer       **pbuf
)
{
    GstBaseTransform *trans;
    GstFlowReturn ret;

    trans = GST_BASE_TRANSFORM(gst_pad_get_parent_element(pad));
    if (!trans)
        return GST_FLOW_UNEXPECTED;

    ret = gst_vaapiconvert_buffer_alloc(trans, size, caps, pbuf);
    g_object_unref(trans);
    return ret;
}

static GstFlowReturn
gst_vaapiconvert_prepare_output_buffer(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    gint              size,
    GstCaps          *caps,
    GstBuffer       **poutbuf
)
{
    GstVaapiConvert * const convert = GST_VAAPICONVERT(trans);
    GstBuffer *buffer;

    buffer = gst_vaapi_video_buffer_new_from_pool(convert->surfaces);
    if (!buffer)
        return GST_FLOW_UNEXPECTED;

    gst_buffer_set_caps(buffer, caps);
    *poutbuf = buffer;
    return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapiconvert,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    return gst_element_register(plugin,
                                GST_PLUGIN_NAME,
                                GST_RANK_PRIMARY,
                                GST_TYPE_VAAPICONVERT);
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
