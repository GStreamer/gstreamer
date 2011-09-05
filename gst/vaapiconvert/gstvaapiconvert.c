/*
 *  gstvaapiconvert.c - VA-API video converter
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
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
 * SECTION:gstvaapiconvert
 * @short_description: A VA-API based video pixels format converter
 *
 * vaapiconvert converts from raw YUV pixels to surfaces suitable for
 * the vaapisink element.
 */

#include "config.h"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/vaapi/gstvaapivideosink.h>
#include <gst/vaapi/gstvaapivideobuffer.h>
#include <gst/vaapi/gstvaapiutils_gst.h>
#include "gstvaapiconvert.h"

#define GST_PLUGIN_NAME "vaapiconvert"
#define GST_PLUGIN_DESC "A VA-API based video pixels format converter"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapiconvert);
#define GST_CAT_DEFAULT gst_debug_vaapiconvert

/* ElementFactory information */
static const GstElementDetails gst_vaapiconvert_details =
    GST_ELEMENT_DETAILS(
        "VA-API colorspace converter",
        "Filter/Converter/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gbeauchesne@splitted-desktop.com>");

/* Default templates */
static const char gst_vaapiconvert_yuv_caps_str[] =
    "video/x-raw-yuv, "
    "width  = (int) [ 1, MAX ], "
    "height = (int) [ 1, MAX ]; ";

static const char gst_vaapiconvert_vaapi_caps_str[] =
    GST_VAAPI_SURFACE_CAPS;

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

/*
 * Direct rendering levels (direct-rendering)
 * 0: upstream allocated YUV pixels
 * 1: vaapiconvert allocated YUV pixels (mapped from VA image)
 * 2: vaapiconvert allocated YUV pixels (mapped from VA surface)
 */
#define DIRECT_RENDERING_DEFAULT 2

enum {
    PROP_0,

    PROP_DIRECT_RENDERING,
};

static gboolean
gst_vaapiconvert_start(GstBaseTransform *trans);

static gboolean
gst_vaapiconvert_stop(GstBaseTransform *trans);

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

static void
gst_vaapiconvert_base_init(gpointer klass)
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
gst_vaapiconvert_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiConvert * const convert = GST_VAAPICONVERT(object);

    switch (prop_id) {
    case PROP_DIRECT_RENDERING:
        GST_OBJECT_LOCK(convert);
        convert->direct_rendering = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(convert);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapiconvert_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiConvert * const convert = GST_VAAPICONVERT(object);

    switch (prop_id) {
    case PROP_DIRECT_RENDERING:
        g_value_set_uint(value, convert->direct_rendering);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapiconvert_class_init(GstVaapiConvertClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass * const trans_class = GST_BASE_TRANSFORM_CLASS(klass);

    object_class->finalize      = gst_vaapiconvert_finalize;
    object_class->set_property  = gst_vaapiconvert_set_property;
    object_class->get_property  = gst_vaapiconvert_get_property;

    trans_class->start          = gst_vaapiconvert_start;
    trans_class->stop           = gst_vaapiconvert_stop;
    trans_class->transform      = gst_vaapiconvert_transform;
    trans_class->transform_caps = gst_vaapiconvert_transform_caps;
    trans_class->set_caps       = gst_vaapiconvert_set_caps;
    trans_class->get_unit_size  = gst_vaapiconvert_get_unit_size;
    trans_class->prepare_output_buffer = gst_vaapiconvert_prepare_output_buffer;

    /**
     * GstVaapiConvert:direct-rendering:
     *
     * Selects the direct rendering level.
     * <orderedlist>
     * <listitem override="0">
     *   Disables direct rendering.
     * </listitem>
     * <listitem>
     *   Enables direct rendering to the output buffer. i.e. this
     *   tries to use a single buffer for both sink and src pads.
     * </listitem>
     * <listitem>
     *   Enables direct rendering to the underlying surface. i.e. with
     *   drivers supporting vaDeriveImage(), the output surface pixels
     *   will be modified directly.
     * </listitem>
     * </orderedlist>
     */
    g_object_class_install_property
        (object_class,
         PROP_DIRECT_RENDERING,
         g_param_spec_uint("direct-rendering",
                           "Direct rendering",
                           "Direct rendering level",
                           0, 2,
                           DIRECT_RENDERING_DEFAULT,
                           G_PARAM_READWRITE));
}

static void
gst_vaapiconvert_init(GstVaapiConvert *convert, GstVaapiConvertClass *klass)
{
    GstPad *sinkpad;

    convert->display                    = NULL;
    convert->images                     = NULL;
    convert->image_width                = 0;
    convert->image_height               = 0;
    convert->surfaces                   = NULL;
    convert->surface_width              = 0;
    convert->surface_height             = 0;
    convert->direct_rendering_caps      = 0;
    convert->direct_rendering           = G_MAXUINT32;

    /* Override buffer allocator on sink pad */
    sinkpad = gst_element_get_static_pad(GST_ELEMENT(convert), "sink");
    gst_pad_set_bufferalloc_function(
        sinkpad,
        gst_vaapiconvert_sinkpad_buffer_alloc
    );
    g_object_unref(sinkpad);
}

static gboolean
gst_vaapiconvert_start(GstBaseTransform *trans)
{
    GstVaapiConvert * const convert = GST_VAAPICONVERT(trans);
    GstVaapiDisplay *display;

    /* Look for a downstream display */
    display = gst_vaapi_display_lookup_downstream(GST_ELEMENT(trans));
    if (!display)
        return FALSE;

    convert->display = g_object_ref(display);
    return TRUE;
}

static gboolean
gst_vaapiconvert_stop(GstBaseTransform *trans)
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
    GstVaapiVideoBuffer *vbuffer;
    GstVaapiSurface *surface;
    GstVaapiImage *image;

    vbuffer = GST_VAAPI_VIDEO_BUFFER(outbuf);
    surface = gst_vaapi_video_buffer_get_surface(vbuffer);
    if (!surface)
        return GST_FLOW_UNEXPECTED;

    if (convert->direct_rendering) {
        if (!GST_VAAPI_IS_VIDEO_BUFFER(inbuf)) {
            GST_DEBUG("GstVaapiVideoBuffer was expected");
            return GST_FLOW_UNEXPECTED;
        }

        vbuffer = GST_VAAPI_VIDEO_BUFFER(inbuf);
        image   = gst_vaapi_video_buffer_get_image(vbuffer);
        if (!image)
            return GST_FLOW_UNEXPECTED;
        if (!gst_vaapi_image_unmap(image))
            return GST_FLOW_UNEXPECTED;

        if (convert->direct_rendering < 2)
            gst_vaapi_surface_put_image(surface, image);
        return GST_FLOW_OK;
    }

    image = gst_vaapi_video_pool_get_object(convert->images);
    if (!image)
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
gst_vaapiconvert_ensure_image_pool(GstVaapiConvert *convert, GstCaps *caps)
{
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    GstVideoFormat vformat;
    GstVaapiImage *image;
    gint width, height;

    gst_structure_get_int(structure, "width",  &width);
    gst_structure_get_int(structure, "height", &height);

    if (width != convert->image_width || height != convert->image_height) {
        convert->image_width  = width;
        convert->image_height = height;
        if (convert->images)
            g_object_unref(convert->images);
        convert->images = gst_vaapi_image_pool_new(convert->display, caps);
        if (!convert->images)
            return FALSE;

        /* Check if we can alias sink & output buffers (same data_size) */
        if (gst_video_format_parse_caps(caps, &vformat, NULL, NULL)) {
            image = gst_vaapi_video_pool_get_object(convert->images);
            if (image) {
                if (convert->direct_rendering_caps == 0 &&
                    (gst_vaapi_image_is_linear(image) &&
                     (gst_vaapi_image_get_data_size(image) ==
                      gst_video_format_get_size(vformat, width, height))))
                    convert->direct_rendering_caps = 1;
                gst_vaapi_video_pool_put_object(convert->images, image);
            }
        }
    }
    return TRUE;
}

static gboolean
gst_vaapiconvert_ensure_surface_pool(GstVaapiConvert *convert, GstCaps *caps)
{
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    GstVaapiSurface *surface;
    GstVaapiImage *image;
    gint width, height;

    gst_structure_get_int(structure, "width",  &width);
    gst_structure_get_int(structure, "height", &height);

    if (width != convert->surface_width || height != convert->surface_height) {
        convert->surface_width  = width;
        convert->surface_height = height;
        if (convert->surfaces)
            g_object_unref(convert->surfaces);
        convert->surfaces = gst_vaapi_surface_pool_new(convert->display, caps);
        if (!convert->surfaces)
            return FALSE;

        /* Check if we can access to the surface pixels directly */
        surface = gst_vaapi_video_pool_get_object(convert->surfaces);
        if (surface) {
            image = gst_vaapi_surface_derive_image(surface);
            if (image) {
                if (gst_vaapi_image_map(image)) {
                    if (convert->direct_rendering_caps == 1)
                        convert->direct_rendering_caps = 2;
                    gst_vaapi_image_unmap(image);
                }
                g_object_unref(image);
            }
            gst_vaapi_video_pool_put_object(convert->surfaces, surface);
        }
    }
    return TRUE;
}

static gboolean
gst_vaapiconvert_negotiate_buffers(
    GstVaapiConvert  *convert,
    GstCaps          *incaps,
    GstCaps          *outcaps
)
{
    guint dr;

    if (!gst_vaapiconvert_ensure_image_pool(convert, incaps))
        return FALSE;

    if (!gst_vaapiconvert_ensure_surface_pool(convert, outcaps))
        return FALSE;

    dr = MIN(convert->direct_rendering, convert->direct_rendering_caps);
    if (convert->direct_rendering != dr) {
        convert->direct_rendering = dr;
        GST_DEBUG("direct-rendering level: %d", dr);
    }
    return TRUE;
}

static gboolean
gst_vaapiconvert_set_caps(
    GstBaseTransform *trans,
    GstCaps          *incaps,
    GstCaps          *outcaps
)
{
    GstVaapiConvert * const convert = GST_VAAPICONVERT(trans);

    if (!gst_vaapiconvert_negotiate_buffers(convert, incaps, outcaps))
        return FALSE;

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
    GstVaapiConvert * const convert = GST_VAAPICONVERT(trans);
    GstBuffer *buffer = NULL;
    GstVaapiImage *image = NULL;
    GstVaapiSurface *surface = NULL;
    GstVaapiVideoBuffer *vbuffer;

    /* Check if we can use direct-rendering */
    if (!gst_vaapiconvert_negotiate_buffers(convert, caps, caps))
        goto error;
    if (!convert->direct_rendering)
        return GST_FLOW_OK;

    switch (convert->direct_rendering) {
    case 2:
        buffer  = gst_vaapi_video_buffer_new_from_pool(convert->surfaces);
        if (!buffer)
            goto error;
        vbuffer = GST_VAAPI_VIDEO_BUFFER(buffer);

        surface = gst_vaapi_video_buffer_get_surface(vbuffer);
        image   = gst_vaapi_surface_derive_image(surface);
        if (image) {
            gst_vaapi_video_buffer_set_image(vbuffer, image);
            break;
        }

        /* We can't use the derive-image optimization. Disable it. */
        convert->direct_rendering = 1;
        gst_buffer_unref(buffer);
        buffer = NULL;

    case 1:
        buffer  = gst_vaapi_video_buffer_new_from_pool(convert->images);
        if (!buffer)
            goto error;
        vbuffer = GST_VAAPI_VIDEO_BUFFER(buffer);

        image   = gst_vaapi_video_buffer_get_image(vbuffer);
        break;
    }
    g_assert(image);

    if (!gst_vaapi_image_map(image))
        goto error;

    GST_BUFFER_DATA(buffer) = gst_vaapi_image_get_plane(image, 0);
    GST_BUFFER_SIZE(buffer) = gst_vaapi_image_get_data_size(image);

    gst_buffer_set_caps(buffer, caps);
    *pbuf = buffer;
    return GST_FLOW_OK;

error:
    /* We can't use the inout-buffers optimization. Disable it. */
    GST_DEBUG("disable in/out buffer optimization");
    if (buffer)
        gst_buffer_unref(buffer);
    convert->direct_rendering = 0;
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
    GstBuffer *buffer = NULL;
    GstFlowReturn ret;

    if (convert->direct_rendering == 2) {
        if (GST_VAAPI_IS_VIDEO_BUFFER(inbuf)) {
            buffer = gst_vaapi_video_buffer_new_from_buffer(inbuf);
            GST_BUFFER_SIZE(buffer) = size;
        }
        else {
            GST_DEBUG("upstream element destroyed our in/out buffer");
            convert->direct_rendering = 1;
        }
    }

    if (!buffer) {
        buffer = gst_vaapi_video_buffer_new_from_pool(convert->surfaces);
        if (!buffer)
            return GST_FLOW_UNEXPECTED;
    }

    gst_buffer_set_caps(buffer, caps);
    *poutbuf = buffer;
    return GST_FLOW_OK;
}

static gboolean
plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapiconvert,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    return gst_element_register(plugin,
                                GST_PLUGIN_NAME,
                                GST_RANK_SECONDARY,
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
