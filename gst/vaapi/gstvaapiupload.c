/*
 *  gstvaapiupload.c - VA-API video uploader
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
 * SECTION:gstvaapiupload
 * @short_description: A video to VA flow filter
 *
 * vaapiupload converts from raw YUV pixels to VA surfaces suitable
 * for the vaapisink element, for example.
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/videocontext.h>
#include <gst/vaapi/gstvaapivideobuffer.h>

#include "gstvaapiupload.h"
#include "gstvaapipluginutil.h"
#include "gstvaapipluginbuffer.h"

#define GST_PLUGIN_NAME "vaapiupload"
#define GST_PLUGIN_DESC "A video to VA flow filter"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapiupload);
#define GST_CAT_DEFAULT gst_debug_vaapiupload

/* ElementFactory information */
static const GstElementDetails gst_vaapiupload_details =
    GST_ELEMENT_DETAILS(
        "VA-API colorspace converter",
        "Filter/Converter/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

/* Default templates */
static const char gst_vaapiupload_yuv_caps_str[] =
    "video/x-raw-yuv, "
    "width  = (int) [ 1, MAX ], "
    "height = (int) [ 1, MAX ]; ";

static const char gst_vaapiupload_vaapi_caps_str[] =
    GST_VAAPI_SURFACE_CAPS;

static GstStaticPadTemplate gst_vaapiupload_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapiupload_yuv_caps_str));

static GstStaticPadTemplate gst_vaapiupload_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapiupload_vaapi_caps_str));

static void
gst_vaapiupload_implements_iface_init(GstImplementsInterfaceClass *iface);

static void
gst_video_context_interface_init(GstVideoContextInterface *iface);

#define GstVideoContextClass GstVideoContextInterface
G_DEFINE_TYPE_WITH_CODE(
    GstVaapiUpload,
    gst_vaapiupload,
    GST_TYPE_BASE_TRANSFORM,
    G_IMPLEMENT_INTERFACE(GST_TYPE_IMPLEMENTS_INTERFACE,
                          gst_vaapiupload_implements_iface_init);
    G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_CONTEXT,
                          gst_video_context_interface_init))

/*
 * Direct rendering levels (direct-rendering)
 * 0: upstream allocated YUV pixels
 * 1: vaapiupload allocated YUV pixels (mapped from VA image)
 * 2: vaapiupload allocated YUV pixels (mapped from VA surface)
 */
#define DIRECT_RENDERING_DEFAULT 2

enum {
    PROP_0,

    PROP_DIRECT_RENDERING,
};

static gboolean
gst_vaapiupload_start(GstBaseTransform *trans);

static gboolean
gst_vaapiupload_stop(GstBaseTransform *trans);

static GstFlowReturn
gst_vaapiupload_transform(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    GstBuffer        *outbuf
);

static GstCaps *
gst_vaapiupload_transform_caps(
    GstBaseTransform *trans,
    GstPadDirection   direction,
    GstCaps          *caps
);

static gboolean
gst_vaapiupload_set_caps(
    GstBaseTransform *trans,
    GstCaps          *incaps,
    GstCaps          *outcaps
);

static gboolean
gst_vaapiupload_get_unit_size(
    GstBaseTransform *trans,
    GstCaps          *caps,
    guint            *size
);

static GstFlowReturn
gst_vaapiupload_sinkpad_buffer_alloc(
    GstPad           *pad,
    guint64           offset,
    guint             size,
    GstCaps          *caps,
    GstBuffer       **pbuf
);

static GstFlowReturn
gst_vaapiupload_prepare_output_buffer(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    gint              size,
    GstCaps          *caps,
    GstBuffer       **poutbuf
);

static gboolean
gst_vaapiupload_query(
    GstPad   *pad,
    GstQuery *query
);

/* GstImplementsInterface interface */

static gboolean
gst_vaapiupload_implements_interface_supported(
    GstImplementsInterface *iface,
    GType                   type
)
{
    return (type == GST_TYPE_VIDEO_CONTEXT);
}

static void
gst_vaapiupload_implements_iface_init(GstImplementsInterfaceClass *iface)
{
    iface->supported = gst_vaapiupload_implements_interface_supported;
}

/* GstVideoContext interface */

static void
gst_vaapiupload_set_video_context(GstVideoContext *context, const gchar *type,
    const GValue *value)
{
  GstVaapiUpload *upload = GST_VAAPIUPLOAD (context);
  gst_vaapi_set_display (type, value, &upload->display);
}

static void
gst_video_context_interface_init(GstVideoContextInterface *iface)
{
    iface->set_context = gst_vaapiupload_set_video_context;
}

static void
gst_vaapiupload_destroy(GstVaapiUpload *upload)
{
    g_clear_object(&upload->images);
    g_clear_object(&upload->surfaces);
    g_clear_object(&upload->display);
}

static void
gst_vaapiupload_finalize(GObject *object)
{
    gst_vaapiupload_destroy(GST_VAAPIUPLOAD(object));

    G_OBJECT_CLASS(gst_vaapiupload_parent_class)->finalize(object);
}


static void
gst_vaapiupload_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiUpload * const upload = GST_VAAPIUPLOAD(object);

    switch (prop_id) {
    case PROP_DIRECT_RENDERING:
        GST_OBJECT_LOCK(upload);
        upload->direct_rendering = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(upload);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapiupload_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiUpload * const upload = GST_VAAPIUPLOAD(object);

    switch (prop_id) {
    case PROP_DIRECT_RENDERING:
        g_value_set_uint(value, upload->direct_rendering);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapiupload_class_init(GstVaapiUploadClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass * const trans_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstPadTemplate *pad_template;

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapiupload,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    object_class->finalize      = gst_vaapiupload_finalize;
    object_class->set_property  = gst_vaapiupload_set_property;
    object_class->get_property  = gst_vaapiupload_get_property;

    trans_class->start          = gst_vaapiupload_start;
    trans_class->stop           = gst_vaapiupload_stop;
    trans_class->transform      = gst_vaapiupload_transform;
    trans_class->transform_caps = gst_vaapiupload_transform_caps;
    trans_class->set_caps       = gst_vaapiupload_set_caps;
    trans_class->get_unit_size  = gst_vaapiupload_get_unit_size;
    trans_class->prepare_output_buffer = gst_vaapiupload_prepare_output_buffer;

    gst_element_class_set_details_simple(
        element_class,
        gst_vaapiupload_details.longname,
        gst_vaapiupload_details.klass,
        gst_vaapiupload_details.description,
        gst_vaapiupload_details.author
    );

    /* sink pad */
    pad_template = gst_static_pad_template_get(&gst_vaapiupload_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
    gst_object_unref(pad_template);

    /* src pad */
    pad_template = gst_static_pad_template_get(&gst_vaapiupload_src_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
    gst_object_unref(pad_template);

    /**
     * GstVaapiUpload:direct-rendering:
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
gst_vaapiupload_init(GstVaapiUpload *upload)
{
    GstPad *sinkpad, *srcpad;

    upload->display                     = NULL;
    upload->images                      = NULL;
    upload->images_reset                = FALSE;
    upload->image_width                 = 0;
    upload->image_height                = 0;
    upload->surfaces                    = NULL;
    upload->surfaces_reset              = FALSE;
    upload->surface_width               = 0;
    upload->surface_height              = 0;
    upload->direct_rendering_caps       = 0;
    upload->direct_rendering            = G_MAXUINT32;

    /* Override buffer allocator on sink pad */
    sinkpad = gst_element_get_static_pad(GST_ELEMENT(upload), "sink");
    gst_pad_set_bufferalloc_function(
        sinkpad,
        gst_vaapiupload_sinkpad_buffer_alloc
    );
    gst_pad_set_query_function(sinkpad, gst_vaapiupload_query);
    g_object_unref(sinkpad);

    /* Override query on src pad */
    srcpad = gst_element_get_static_pad(GST_ELEMENT(upload), "src");
    gst_pad_set_query_function(srcpad, gst_vaapiupload_query);
    g_object_unref(srcpad);
}

static inline gboolean
gst_vaapiupload_ensure_display(GstVaapiUpload *upload)
{
    return gst_vaapi_ensure_display(upload, GST_VAAPI_DISPLAY_TYPE_ANY,
        &upload->display);
}

static gboolean
gst_vaapiupload_start(GstBaseTransform *trans)
{
    GstVaapiUpload * const upload = GST_VAAPIUPLOAD(trans);

    if (!gst_vaapiupload_ensure_display(upload))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapiupload_stop(GstBaseTransform *trans)
{
    GstVaapiUpload * const upload = GST_VAAPIUPLOAD(trans);

    g_clear_object(&upload->display);

    return TRUE;
}

static GstFlowReturn
gst_vaapiupload_transform(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    GstBuffer        *outbuf
)
{
    GstVaapiUpload * const upload = GST_VAAPIUPLOAD(trans);
    GstVaapiVideoBuffer *vbuffer;
    GstVaapiSurface *surface;
    GstVaapiImage *image;
    gboolean success;

    vbuffer = GST_VAAPI_VIDEO_BUFFER(outbuf);
    surface = gst_vaapi_video_buffer_get_surface(vbuffer);
    if (!surface)
        return GST_FLOW_UNEXPECTED;

    if (upload->direct_rendering) {
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

        if (upload->direct_rendering < 2) {
            if (!gst_vaapi_surface_put_image(surface, image))
                goto error_put_image;
        }
        return GST_FLOW_OK;
    }

    image = gst_vaapi_video_pool_get_object(upload->images);
    if (!image)
        return GST_FLOW_UNEXPECTED;

    gst_vaapi_image_update_from_buffer(image, inbuf, NULL);
    success = gst_vaapi_surface_put_image(surface, image);
    gst_vaapi_video_pool_put_object(upload->images, image);
    if (!success)
        goto error_put_image;
    return GST_FLOW_OK;

error_put_image:
    {
        GST_WARNING("failed to upload %" GST_FOURCC_FORMAT " image "
                    "to surface 0x%08x",
                    GST_FOURCC_ARGS(gst_vaapi_image_get_format(image)),
                    gst_vaapi_surface_get_id(surface));
        return GST_FLOW_OK;
    }
}

static GstCaps *
gst_vaapiupload_transform_caps(
    GstBaseTransform *trans,
    GstPadDirection   direction,
    GstCaps          *caps
)
{
    GstVaapiUpload * const upload = GST_VAAPIUPLOAD(trans);
    GstCaps *out_caps = NULL;
    GstStructure *structure;

    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    structure = gst_caps_get_structure(caps, 0);

    if (direction == GST_PAD_SINK) {
        if (!gst_structure_has_name(structure, "video/x-raw-yuv"))
            return NULL;
        out_caps = gst_caps_from_string(gst_vaapiupload_vaapi_caps_str);

        structure = gst_caps_get_structure(out_caps, 0);
        gst_structure_set(
            structure,
            "type", G_TYPE_STRING, "vaapi",
            "opengl", G_TYPE_BOOLEAN, USE_GLX,
            NULL
        );
    }
    else {
        if (!gst_structure_has_name(structure, GST_VAAPI_SURFACE_CAPS_NAME))
            return NULL;
        out_caps = gst_caps_from_string(gst_vaapiupload_yuv_caps_str);
        if (upload->display) {
            GstCaps *allowed_caps, *inter_caps;
            allowed_caps = gst_vaapi_display_get_image_caps(upload->display);
            if (!allowed_caps)
                return NULL;
            inter_caps = gst_caps_intersect(out_caps, allowed_caps);
            gst_caps_unref(allowed_caps);
            gst_caps_unref(out_caps);
            out_caps = inter_caps;
        }
    }

    if (!gst_vaapi_append_surface_caps(out_caps, caps)) {
        gst_caps_unref(out_caps);
        return NULL;
    }
    return out_caps;
}

static gboolean
gst_vaapiupload_ensure_image_pool(GstVaapiUpload *upload, GstCaps *caps)
{
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    gint width, height;

    gst_structure_get_int(structure, "width",  &width);
    gst_structure_get_int(structure, "height", &height);

    if (width != upload->image_width || height != upload->image_height) {
        upload->image_width  = width;
        upload->image_height = height;
        g_clear_object(&upload->images);
        upload->images = gst_vaapi_image_pool_new(upload->display, caps);
        if (!upload->images)
            return FALSE;
        upload->images_reset = TRUE;
    }
    return TRUE;
}

static gboolean
gst_vaapiupload_ensure_surface_pool(GstVaapiUpload *upload, GstCaps *caps)
{
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    gint width, height;

    gst_structure_get_int(structure, "width",  &width);
    gst_structure_get_int(structure, "height", &height);

    if (width != upload->surface_width || height != upload->surface_height) {
        upload->surface_width  = width;
        upload->surface_height = height;
        g_clear_object(&upload->surfaces);
        upload->surfaces = gst_vaapi_surface_pool_new(upload->display, caps);
        if (!upload->surfaces)
            return FALSE;
        upload->surfaces_reset = TRUE;
    }
    return TRUE;
}

static void
gst_vaapiupload_ensure_direct_rendering_caps(
    GstVaapiUpload *upload,
    GstCaps         *caps
)
{
    GstVaapiSurface *surface;
    GstVaapiImage *image;
    GstVaapiImageFormat vaformat;
    GstVideoFormat vformat;
    GstStructure *structure;
    gint width, height;

    if (!upload->images_reset && !upload->surfaces_reset)
        return;

    upload->images_reset          = FALSE;
    upload->surfaces_reset        = FALSE;
    upload->direct_rendering_caps = 0;

    structure = gst_caps_get_structure(caps, 0);
    if (!structure)
        return;
    gst_structure_get_int(structure, "width",  &width);
    gst_structure_get_int(structure, "height", &height);

    /* Translate from Gst video format to VA image format */
    if (!gst_video_format_parse_caps(caps, &vformat, NULL, NULL))
        return;
    if (!gst_video_format_is_yuv(vformat))
        return;
    vaformat = gst_vaapi_image_format_from_video(vformat);
    if (!vaformat)
        return;

    /* Check if we can alias sink & output buffers (same data_size) */
    image = gst_vaapi_video_pool_get_object(upload->images);
    if (image) {
        if (upload->direct_rendering_caps == 0 &&
            (gst_vaapi_image_get_format(image) == vaformat &&
             gst_vaapi_image_is_linear(image) &&
             (gst_vaapi_image_get_data_size(image) ==
              gst_video_format_get_size(vformat, width, height))))
            upload->direct_rendering_caps = 1;
        gst_vaapi_video_pool_put_object(upload->images, image);
    }

    /* Check if we can access to the surface pixels directly */
    surface = gst_vaapi_video_pool_get_object(upload->surfaces);
    if (surface) {
        image = gst_vaapi_surface_derive_image(surface);
        if (image) {
            if (gst_vaapi_image_map(image)) {
                if (upload->direct_rendering_caps == 1 &&
                    (gst_vaapi_image_get_format(image) == vaformat &&
                     gst_vaapi_image_is_linear(image) &&
                     (gst_vaapi_image_get_data_size(image) ==
                      gst_video_format_get_size(vformat, width, height))))
                    upload->direct_rendering_caps = 2;
                gst_vaapi_image_unmap(image);
            }
            g_object_unref(image);
        }
        gst_vaapi_video_pool_put_object(upload->surfaces, surface);
    }
}

static gboolean
gst_vaapiupload_negotiate_buffers(
    GstVaapiUpload  *upload,
    GstCaps          *incaps,
    GstCaps          *outcaps
)
{
    guint dr;

    if (!gst_vaapiupload_ensure_image_pool(upload, incaps))
        return FALSE;

    if (!gst_vaapiupload_ensure_surface_pool(upload, outcaps))
        return FALSE;

    gst_vaapiupload_ensure_direct_rendering_caps(upload, incaps);
    dr = MIN(upload->direct_rendering, upload->direct_rendering_caps);
    if (upload->direct_rendering != dr) {
        upload->direct_rendering = dr;
        GST_DEBUG("direct-rendering level: %d", dr);
    }
    return TRUE;
}

static gboolean
gst_vaapiupload_set_caps(
    GstBaseTransform *trans,
    GstCaps          *incaps,
    GstCaps          *outcaps
)
{
    GstVaapiUpload * const upload = GST_VAAPIUPLOAD(trans);

    if (!gst_vaapiupload_negotiate_buffers(upload, incaps, outcaps))
        return FALSE;

    return TRUE;
}

static gboolean
gst_vaapiupload_get_unit_size(
    GstBaseTransform *trans,
    GstCaps          *caps,
    guint            *size
)
{
    GstStructure * const structure = gst_caps_get_structure(caps, 0);
    GstVideoFormat format;
    gint width, height;

    if (gst_structure_has_name(structure, GST_VAAPI_SURFACE_CAPS_NAME))
        *size = 0;
    else {
        if (!gst_video_format_parse_caps(caps, &format, &width, &height))
            return FALSE;
        *size = gst_video_format_get_size(format, width, height);
    }
    return TRUE;
}

static GstFlowReturn
gst_vaapiupload_buffer_alloc(
    GstBaseTransform *trans,
    guint             size,
    GstCaps          *caps,
    GstBuffer       **pbuf
)
{
    GstVaapiUpload * const upload = GST_VAAPIUPLOAD(trans);
    GstBuffer *buffer = NULL;
    GstVaapiImage *image = NULL;
    GstVaapiSurface *surface = NULL;
    GstVaapiVideoBuffer *vbuffer;

    /* Check if we can use direct-rendering */
    if (!gst_vaapiupload_negotiate_buffers(upload, caps, caps))
        goto error;
    if (!upload->direct_rendering)
        return GST_FLOW_OK;

    switch (upload->direct_rendering) {
    case 2:
        buffer  = gst_vaapi_video_buffer_new_from_pool(upload->surfaces);
        if (!buffer)
            goto error;
        vbuffer = GST_VAAPI_VIDEO_BUFFER(buffer);

        surface = gst_vaapi_video_buffer_get_surface(vbuffer);
        image   = gst_vaapi_surface_derive_image(surface);
        if (image && gst_vaapi_image_get_data_size(image) == size) {
            gst_vaapi_video_buffer_set_image(vbuffer, image);
            g_object_unref(image); /* video buffer owns an extra reference */
            break;
        }

        /* We can't use the derive-image optimization. Disable it. */
        upload->direct_rendering = 1;
        gst_buffer_unref(buffer);
        buffer = NULL;

    case 1:
        buffer  = gst_vaapi_video_buffer_new_from_pool(upload->images);
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
    upload->direct_rendering = 0;
    return GST_FLOW_OK;
}

static GstFlowReturn
gst_vaapiupload_sinkpad_buffer_alloc(
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

    ret = gst_vaapiupload_buffer_alloc(trans, size, caps, pbuf);
    g_object_unref(trans);
    return ret;
}

static GstFlowReturn
gst_vaapiupload_prepare_output_buffer(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    gint              size,
    GstCaps          *caps,
    GstBuffer       **poutbuf
)
{
    GstVaapiUpload * const upload = GST_VAAPIUPLOAD(trans);
    GstBuffer *buffer = NULL;

    if (upload->direct_rendering == 2) {
        if (GST_VAAPI_IS_VIDEO_BUFFER(inbuf)) {
            buffer = gst_vaapi_video_buffer_new_from_buffer(inbuf);
            GST_BUFFER_SIZE(buffer) = size;
        }
        else {
            GST_DEBUG("upstream element destroyed our in/out buffer");
            upload->direct_rendering = 1;
        }
    }

    if (!buffer) {
        buffer = gst_vaapi_video_buffer_new_from_pool(upload->surfaces);
        if (!buffer)
            return GST_FLOW_UNEXPECTED;
        gst_buffer_set_caps(buffer, caps);
    }

    *poutbuf = buffer;
    return GST_FLOW_OK;
}

static gboolean
gst_vaapiupload_query(GstPad *pad, GstQuery *query)
{
  GstVaapiUpload *upload = GST_VAAPIUPLOAD (gst_pad_get_parent_element (pad));
  gboolean res;

  GST_DEBUG ("sharing display %p", upload->display);

  if (gst_vaapi_reply_to_query (query, upload->display))
    res = TRUE;
  else
    res = gst_pad_query_default (pad, query);

  g_object_unref (upload);
  return res;
}
