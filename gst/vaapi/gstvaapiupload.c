/*
 *  gstvaapiupload.c - VA-API video upload element
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

#include "gstvaapiupload.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideobuffer.h"

#define GST_PLUGIN_NAME "vaapiupload"
#define GST_PLUGIN_DESC "A video to VA flow filter"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapiupload);
#define GST_CAT_DEFAULT gst_debug_vaapiupload

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
    GstVaapiUpload * const upload = GST_VAAPIUPLOAD(context);

    gst_vaapi_set_display(type, value, &upload->display);

    if (upload->uploader)
        gst_vaapi_uploader_ensure_display(upload->uploader, upload->display);
}

static void
gst_video_context_interface_init(GstVideoContextInterface *iface)
{
    iface->set_context = gst_vaapiupload_set_video_context;
}

#define GstVideoContextClass GstVideoContextInterface
G_DEFINE_TYPE_WITH_CODE(
    GstVaapiUpload,
    gst_vaapiupload,
    GST_TYPE_BASE_TRANSFORM,
    G_IMPLEMENT_INTERFACE(GST_TYPE_IMPLEMENTS_INTERFACE,
                          gst_vaapiupload_implements_iface_init);
    G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_CONTEXT,
                          gst_video_context_interface_init))

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

static void
gst_vaapiupload_destroy(GstVaapiUpload *upload)
{
    g_clear_object(&upload->uploader);
    g_clear_object(&upload->display);
}

static void
gst_vaapiupload_finalize(GObject *object)
{
    gst_vaapiupload_destroy(GST_VAAPIUPLOAD(object));

    G_OBJECT_CLASS(gst_vaapiupload_parent_class)->finalize(object);
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

    trans_class->start          = gst_vaapiupload_start;
    trans_class->stop           = gst_vaapiupload_stop;
    trans_class->transform      = gst_vaapiupload_transform;
    trans_class->transform_caps = gst_vaapiupload_transform_caps;
    trans_class->set_caps       = gst_vaapiupload_set_caps;
    trans_class->get_unit_size  = gst_vaapiupload_get_unit_size;
    trans_class->prepare_output_buffer = gst_vaapiupload_prepare_output_buffer;

    gst_element_class_set_static_metadata(element_class,
        "VA-API colorspace converter",
        "Filter/Converter/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

    /* sink pad */
    pad_template = gst_static_pad_template_get(&gst_vaapiupload_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);

    /* src pad */
    pad_template = gst_static_pad_template_get(&gst_vaapiupload_src_factory);
    gst_element_class_add_pad_template(element_class, pad_template);
}

static void
gst_vaapiupload_init(GstVaapiUpload *upload)
{
    GstPad *sinkpad, *srcpad;

    /* Override buffer allocator on sink pad */
    sinkpad = gst_element_get_static_pad(GST_ELEMENT(upload), "sink");
    gst_pad_set_bufferalloc_function(
        sinkpad,
        gst_vaapiupload_sinkpad_buffer_alloc
    );
    gst_pad_set_query_function(sinkpad, gst_vaapiupload_query);
    gst_object_unref(sinkpad);

    /* Override query on src pad */
    srcpad = gst_element_get_static_pad(GST_ELEMENT(upload), "src");
    gst_pad_set_query_function(srcpad, gst_vaapiupload_query);
    gst_object_unref(srcpad);
}

static inline gboolean
gst_vaapiupload_ensure_display(GstVaapiUpload *upload)
{
    return gst_vaapi_ensure_display(upload, GST_VAAPI_DISPLAY_TYPE_ANY,
        &upload->display);
}

static gboolean
gst_vaapiupload_ensure_uploader(GstVaapiUpload *upload)
{
    if (!gst_vaapiupload_ensure_display(upload))
        return FALSE;

    if (!upload->uploader) {
        upload->uploader = gst_vaapi_uploader_new(upload->display);
        if (!upload->uploader)
            return FALSE;
    }
    if (!gst_vaapi_uploader_ensure_display(upload->uploader, upload->display))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapiupload_start(GstBaseTransform *trans)
{
    GstVaapiUpload * const upload = GST_VAAPIUPLOAD(trans);

    if (!gst_vaapiupload_ensure_uploader(upload))
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

    if (!gst_vaapi_uploader_process(upload->uploader, inbuf, outbuf))
        return GST_FLOW_UNEXPECTED;
    return GST_FLOW_OK;
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
        if (gst_vaapiupload_ensure_uploader(upload)) {
            GstCaps *allowed_caps, *inter_caps;
            allowed_caps = gst_vaapi_uploader_get_caps(upload->uploader);
            if (!allowed_caps)
                return NULL;
            inter_caps = gst_caps_intersect(out_caps, allowed_caps);
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
gst_vaapiupload_set_caps(
    GstBaseTransform *trans,
    GstCaps          *incaps,
    GstCaps          *outcaps
)
{
    GstVaapiUpload * const upload = GST_VAAPIUPLOAD(trans);

    if (!gst_vaapi_uploader_ensure_caps(upload->uploader, incaps, outcaps))
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

    *pbuf = NULL;

    if (!gst_vaapi_uploader_ensure_display(upload->uploader, upload->display))
        return GST_FLOW_NOT_SUPPORTED;
    if (!gst_vaapi_uploader_ensure_caps(upload->uploader, caps, NULL))
        return GST_FLOW_NOT_SUPPORTED;

    /* Allocate a regular GstBuffer if direct rendering is not supported */
    if (!gst_vaapi_uploader_has_direct_rendering(upload->uploader))
        return GST_FLOW_OK;

    *pbuf = gst_vaapi_uploader_get_buffer(upload->uploader);
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
    gst_object_unref(trans);
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
    GstBuffer *buffer;

    *poutbuf = NULL;

    if (!gst_vaapi_uploader_has_direct_rendering(upload->uploader))
        buffer = gst_vaapi_uploader_get_buffer(upload->uploader);
    else
        buffer = gst_vaapi_video_buffer_new_from_buffer(inbuf);
    if (!buffer)
        return GST_FLOW_UNEXPECTED;

    gst_buffer_set_caps(buffer, caps);
    GST_BUFFER_DATA(buffer) = NULL;
    GST_BUFFER_SIZE(buffer) = 0;

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

  gst_object_unref (upload);
  return res;
}
