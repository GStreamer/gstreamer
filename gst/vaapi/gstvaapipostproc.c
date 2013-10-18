/*
 *  gstvaapipostproc.c - VA-API video postprocessing
 *
 *  Copyright (C) 2012-2013 Intel Corporation
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
 * SECTION:gstvaapipostproc
 * @short_description: A video postprocessing filter
 *
 * vaapipostproc consists in various postprocessing algorithms to be
 * applied to VA surfaces. So far, only basic bob deinterlacing is
 * implemented.
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/video/video.h>
#include <gst/video/videocontext.h>

#include "gstvaapipostproc.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideobuffer.h"
#if GST_CHECK_VERSION(1,0,0)
#include "gstvaapivideobufferpool.h"
#include "gstvaapivideomemory.h"
#endif

#define GST_PLUGIN_NAME "vaapipostproc"
#define GST_PLUGIN_DESC "A video postprocessing filter"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapipostproc);
#define GST_CAT_DEFAULT gst_debug_vaapipostproc

/* Default templates */
static const char gst_vaapipostproc_sink_caps_str[] =
    GST_VAAPI_SURFACE_CAPS ", "
    GST_CAPS_INTERLACED_MODES "; "
#if GST_CHECK_VERSION(1,0,0)
    GST_VIDEO_CAPS_MAKE(GST_VIDEO_FORMATS_ALL) ", "
#else
    "video/x-raw-yuv, "
    "width  = " GST_VIDEO_SIZE_RANGE ", "
    "height = " GST_VIDEO_SIZE_RANGE ", "
    "framerate = " GST_VIDEO_FPS_RANGE ", "
#endif
    GST_CAPS_INTERLACED_MODES;

static const char gst_vaapipostproc_src_caps_str[] =
    GST_VAAPI_SURFACE_CAPS ", "
    GST_CAPS_INTERLACED_FALSE;

static GstStaticPadTemplate gst_vaapipostproc_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapipostproc_sink_caps_str));

static GstStaticPadTemplate gst_vaapipostproc_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapipostproc_src_caps_str));

/* GstImplementsInterface interface */
#if !GST_CHECK_VERSION(1,0,0)
static gboolean
gst_vaapipostproc_implements_interface_supported(
    GstImplementsInterface *iface,
    GType                   type
)
{
    return (type == GST_TYPE_VIDEO_CONTEXT);
}

static void
gst_vaapipostproc_implements_iface_init(GstImplementsInterfaceClass *iface)
{
    iface->supported = gst_vaapipostproc_implements_interface_supported;
}
#endif

/* GstVideoContext interface */
static void
gst_vaapipostproc_set_video_context(
    GstVideoContext *context,
    const gchar     *type,
    const GValue    *value
)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(context);

    gst_vaapi_set_display(type, value, &postproc->display);

    if (postproc->uploader)
        gst_vaapi_uploader_ensure_display(postproc->uploader, postproc->display);
}

static void
gst_video_context_interface_init(GstVideoContextInterface *iface)
{
    iface->set_context = gst_vaapipostproc_set_video_context;
}

#define GstVideoContextClass GstVideoContextInterface
G_DEFINE_TYPE_WITH_CODE(
    GstVaapiPostproc,
    gst_vaapipostproc,
    GST_TYPE_BASE_TRANSFORM,
#if !GST_CHECK_VERSION(1,0,0)
    G_IMPLEMENT_INTERFACE(GST_TYPE_IMPLEMENTS_INTERFACE,
                          gst_vaapipostproc_implements_iface_init);
#endif
    G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_CONTEXT,
                          gst_video_context_interface_init))

enum {
    PROP_0,

    PROP_FORMAT,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_FORCE_ASPECT_RATIO,
    PROP_DEINTERLACE_MODE,
    PROP_DEINTERLACE_METHOD,
};

#define DEFAULT_FORMAT                  GST_VIDEO_FORMAT_ENCODED
#define DEFAULT_DEINTERLACE_MODE        GST_VAAPI_DEINTERLACE_MODE_AUTO
#define DEFAULT_DEINTERLACE_METHOD      GST_VAAPI_DEINTERLACE_METHOD_BOB

#define GST_VAAPI_TYPE_DEINTERLACE_MODE \
    gst_vaapi_deinterlace_mode_get_type()

static GType
gst_vaapi_deinterlace_mode_get_type(void)
{
    static GType deinterlace_mode_type = 0;

    static const GEnumValue mode_types[] = {
        { GST_VAAPI_DEINTERLACE_MODE_AUTO,
          "Auto detection", "auto" },
        { GST_VAAPI_DEINTERLACE_MODE_INTERLACED,
          "Force deinterlacing", "interlaced" },
        { GST_VAAPI_DEINTERLACE_MODE_DISABLED,
          "Never deinterlace", "disabled" },
        { 0, NULL, NULL },
    };

    if (!deinterlace_mode_type) {
        deinterlace_mode_type =
            g_enum_register_static("GstVaapiDeinterlaceMode", mode_types);
    }
    return deinterlace_mode_type;
}

static GstVaapiFilterOpInfo *
find_filter_op(GPtrArray *filter_ops, GstVaapiFilterOp op)
{
    guint i;

    if (filter_ops) {
        for (i = 0; i < filter_ops->len; i++) {
            GstVaapiFilterOpInfo * const filter_op =
                g_ptr_array_index(filter_ops, i);
            if (filter_op->op == op)
                return filter_op;
        }
    }
    return NULL;
}

static inline gboolean
gst_vaapipostproc_ensure_display(GstVaapiPostproc *postproc)
{
    return gst_vaapi_ensure_display(postproc, GST_VAAPI_DISPLAY_TYPE_ANY,
        &postproc->display);
}

static gboolean
gst_vaapipostproc_ensure_uploader(GstVaapiPostproc *postproc)
{
    if (!gst_vaapipostproc_ensure_display(postproc))
        return FALSE;

    if (!postproc->uploader) {
        postproc->uploader = gst_vaapi_uploader_new(postproc->display);
        if (!postproc->uploader)
            return FALSE;
    }

    if (!gst_vaapi_uploader_ensure_display(postproc->uploader,
            postproc->display))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapipostproc_ensure_uploader_caps(GstVaapiPostproc *postproc)
{
#if !GST_CHECK_VERSION(1,0,0)
    if (postproc->is_raw_yuv && !gst_vaapi_uploader_ensure_caps(
            postproc->uploader, postproc->sinkpad_caps, NULL))
        return FALSE;
#endif
    return TRUE;
}

static gboolean
gst_vaapipostproc_ensure_filter(GstVaapiPostproc *postproc)
{
    if (postproc->filter)
        return TRUE;

    if (!gst_vaapipostproc_ensure_display(postproc))
        return FALSE;

    postproc->filter = gst_vaapi_filter_new(postproc->display);
    if (!postproc->filter)
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapipostproc_ensure_filter_caps(GstVaapiPostproc *postproc)
{
    if (!gst_vaapipostproc_ensure_filter(postproc))
        return FALSE;

    postproc->filter_ops = gst_vaapi_filter_get_operations(postproc->filter);
    if (!postproc->filter_ops)
        return FALSE;

    postproc->filter_formats = gst_vaapi_filter_get_formats(postproc->filter);
    if (!postproc->filter_formats)
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapipostproc_create(GstVaapiPostproc *postproc)
{
    if (!gst_vaapipostproc_ensure_display(postproc))
        return FALSE;
    if (!gst_vaapipostproc_ensure_uploader(postproc))
        return FALSE;
    if (!gst_vaapipostproc_ensure_uploader_caps(postproc))
        return FALSE;
    if (gst_vaapipostproc_ensure_filter(postproc))
        postproc->use_vpp = TRUE;
    return TRUE;
}

static void
gst_vaapipostproc_destroy_filter(GstVaapiPostproc *postproc)
{
    if (postproc->filter_formats) {
        g_array_unref(postproc->filter_formats);
        postproc->filter_formats = NULL;
    }

    if (postproc->filter_ops) {
        g_ptr_array_unref(postproc->filter_ops);
        postproc->filter_ops = NULL;
    }
    gst_vaapi_filter_replace(&postproc->filter, NULL);
}

static void
gst_vaapipostproc_destroy(GstVaapiPostproc *postproc)
{
#if GST_CHECK_VERSION(1,0,0)
    g_clear_object(&postproc->sinkpad_buffer_pool);
#endif
    g_clear_object(&postproc->uploader);
    gst_vaapipostproc_destroy_filter(postproc);
    gst_vaapi_display_replace(&postproc->display, NULL);

    gst_caps_replace(&postproc->allowed_sinkpad_caps, NULL);
    gst_caps_replace(&postproc->sinkpad_caps, NULL);
    gst_caps_replace(&postproc->allowed_srcpad_caps, NULL);
    gst_caps_replace(&postproc->srcpad_caps,  NULL);
}

static gboolean
gst_vaapipostproc_start(GstBaseTransform *trans)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);

    if (!gst_vaapipostproc_ensure_display(postproc))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapipostproc_stop(GstBaseTransform *trans)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);

    gst_vaapi_display_replace(&postproc->display, NULL);
    return TRUE;
}

static gboolean
is_interlaced_buffer(GstVaapiPostproc *postproc, GstBuffer *buf)
{
    if (!(postproc->flags & GST_VAAPI_POSTPROC_FLAG_DEINTERLACE))
        return FALSE;

    switch (GST_VIDEO_INFO_INTERLACE_MODE(&postproc->sinkpad_info)) {
    case GST_VIDEO_INTERLACE_MODE_MIXED:
#if GST_CHECK_VERSION(1,0,0)
        if (!GST_BUFFER_FLAG_IS_SET(buf, GST_VIDEO_BUFFER_FLAG_INTERLACED))
            return FALSE;
#else
        if (GST_BUFFER_FLAG_IS_SET(buf, GST_VIDEO_BUFFER_PROGRESSIVE))
            return FALSE;
#endif
        break;
    default:
        break;
    }
    return TRUE;
}

static GstBuffer *
create_output_buffer(GstVaapiPostproc *postproc)
{
    GstBuffer *outbuf;

    /* Create a raw VA video buffer without GstVaapiVideoMeta attached
       to it yet, as this will be done next in the transform() hook */
    outbuf = gst_vaapi_video_buffer_new_empty();
    if (!outbuf)
        goto error_create_buffer;

#if !GST_CHECK_VERSION(1,0,0)
    gst_buffer_set_caps(outbuf, postproc->srcpad_caps);
#endif
    return outbuf;

    /* ERRORS */
error_create_buffer:
    {
        GST_ERROR("failed to create output video buffer");
        return NULL;
    }
}

static inline void
append_output_buffer_metadata(GstBuffer *outbuf, GstBuffer *inbuf, guint flags)
{
    gst_buffer_copy_into(outbuf, inbuf, flags |
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_META | GST_BUFFER_COPY_MEMORY,
        0, -1);
}

static GstFlowReturn
gst_vaapipostproc_process_vpp(GstBaseTransform *trans, GstBuffer *inbuf,
    GstBuffer *outbuf)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);
    GstVaapiVideoMeta *inbuf_meta, *outbuf_meta;
    GstVaapiSurface *inbuf_surface, *outbuf_surface;
    GstVaapiFilterStatus status;
    guint flags;

    /* Validate filters */
    if ((postproc->flags & GST_VAAPI_POSTPROC_FLAG_FORMAT) &&
        !gst_vaapi_filter_set_format(postproc->filter, postproc->format))
        return GST_FLOW_NOT_SUPPORTED;

    inbuf_meta = gst_buffer_get_vaapi_video_meta(inbuf);
    if (!inbuf_meta)
        goto error_invalid_buffer;
    inbuf_surface = gst_vaapi_video_meta_get_surface(inbuf_meta);

    flags = gst_vaapi_video_meta_get_render_flags(inbuf_meta) &
        ~(GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD|
          GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD);

    outbuf_meta = gst_vaapi_video_meta_new_from_pool(postproc->filter_pool);
    if (!outbuf_meta)
        goto error_create_meta;
    outbuf_surface = gst_vaapi_video_meta_get_surface(outbuf_meta);

    status = gst_vaapi_filter_process(postproc->filter, inbuf_surface,
        outbuf_surface, flags);
    if (status != GST_VAAPI_FILTER_STATUS_SUCCESS)
        goto error_process_vpp;

    gst_buffer_copy_into(outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
    gst_buffer_set_vaapi_video_meta(outbuf, outbuf_meta);
    gst_vaapi_video_meta_unref(outbuf_meta);
    return GST_FLOW_OK;

    /* ERRORS */
error_invalid_buffer:
    {
        GST_ERROR("failed to validate source buffer");
        return GST_FLOW_ERROR;
    }
error_create_meta:
    {
        GST_ERROR("failed to create new output buffer meta");
        gst_vaapi_video_meta_unref(outbuf_meta);
        return GST_FLOW_ERROR;
    }
error_process_vpp:
    {
        GST_ERROR("failed to apply VPP filters (error %d)", status);
        gst_vaapi_video_meta_unref(outbuf_meta);
        return GST_FLOW_ERROR;
    }
}

static GstFlowReturn
gst_vaapipostproc_process(GstBaseTransform *trans, GstBuffer *inbuf,
    GstBuffer *outbuf)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);
    GstVaapiVideoMeta *meta;
    GstClockTime timestamp;
    GstFlowReturn ret;
    GstBuffer *fieldbuf;
    guint fieldbuf_flags, outbuf_flags, flags;
    gboolean tff, deint;

    meta = gst_buffer_get_vaapi_video_meta(inbuf);
    if (!meta)
        goto error_invalid_buffer;

    timestamp  = GST_BUFFER_TIMESTAMP(inbuf);
    tff        = GST_BUFFER_FLAG_IS_SET(inbuf, GST_VIDEO_BUFFER_FLAG_TFF);
    deint      = is_interlaced_buffer(postproc, inbuf);

    flags = gst_vaapi_video_meta_get_render_flags(meta) &
        ~(GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD|
          GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD);

    /* First field */
    fieldbuf = create_output_buffer(postproc);
    if (!fieldbuf)
        goto error_create_buffer;
    append_output_buffer_metadata(fieldbuf, inbuf, 0);

    meta = gst_buffer_get_vaapi_video_meta(fieldbuf);
    fieldbuf_flags = flags;
    fieldbuf_flags |= deint ? (
        tff ?
        GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD :
        GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD) :
        GST_VAAPI_PICTURE_STRUCTURE_FRAME;
    gst_vaapi_video_meta_set_render_flags(meta, fieldbuf_flags);

    GST_BUFFER_TIMESTAMP(fieldbuf) = timestamp;
    GST_BUFFER_DURATION(fieldbuf)  = postproc->field_duration;
    ret = gst_pad_push(trans->srcpad, fieldbuf);
    if (ret != GST_FLOW_OK)
        goto error_push_buffer;

    /* Second field */
    append_output_buffer_metadata(outbuf, inbuf, 0);

    meta = gst_buffer_get_vaapi_video_meta(outbuf);
    outbuf_flags = flags;
    outbuf_flags |= deint ? (
        tff ?
        GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD :
        GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD) :
        GST_VAAPI_PICTURE_STRUCTURE_FRAME;
    gst_vaapi_video_meta_set_render_flags(meta, outbuf_flags);

    GST_BUFFER_TIMESTAMP(outbuf) = timestamp + postproc->field_duration;
    GST_BUFFER_DURATION(outbuf)  = postproc->field_duration;
    return GST_FLOW_OK;

    /* ERRORS */
error_invalid_buffer:
    {
        GST_ERROR("failed to validate source buffer");
        return GST_FLOW_ERROR;
    }
error_create_buffer:
    {
        GST_ERROR("failed to create output buffer");
        return GST_FLOW_EOS;
    }
error_push_buffer:
    {
        if (ret != GST_FLOW_FLUSHING)
            GST_ERROR("failed to push output buffer to video sink");
        return GST_FLOW_EOS;
    }
}

static GstFlowReturn
gst_vaapipostproc_passthrough(GstBaseTransform *trans, GstBuffer *inbuf,
    GstBuffer *outbuf)
{
    GstVaapiVideoMeta *meta;

    /* No video processing needed, simply copy buffer metadata */
    meta = gst_buffer_get_vaapi_video_meta(inbuf);
    if (!meta)
        goto error_invalid_buffer;

    append_output_buffer_metadata(outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS);
    return GST_FLOW_OK;

    /* ERRORS */
error_invalid_buffer:
    {
        GST_ERROR("failed to validate source buffer");
        return GST_FLOW_ERROR;
    }
}

static gboolean
is_deinterlace_enabled(GstVaapiPostproc *postproc, GstVideoInfo *vip)
{
    gboolean deinterlace;

    switch (postproc->deinterlace_mode) {
    case GST_VAAPI_DEINTERLACE_MODE_AUTO:
        deinterlace = GST_VIDEO_INFO_IS_INTERLACED(vip);
        break;
    case GST_VAAPI_DEINTERLACE_MODE_INTERLACED:
        deinterlace = TRUE;
        break;
    default:
        deinterlace = FALSE;
        break;
    }
    return deinterlace;
}

static gboolean
video_info_changed(GstVideoInfo *old_vip, GstVideoInfo *new_vip)
{
    if (GST_VIDEO_INFO_FORMAT(old_vip) != GST_VIDEO_INFO_FORMAT(new_vip))
        return TRUE;
    if (GST_VIDEO_INFO_INTERLACE_MODE(old_vip) !=
        GST_VIDEO_INFO_INTERLACE_MODE(new_vip))
        return TRUE;
    if (GST_VIDEO_INFO_WIDTH(old_vip) != GST_VIDEO_INFO_WIDTH(new_vip))
        return TRUE;
    if (GST_VIDEO_INFO_HEIGHT(old_vip) != GST_VIDEO_INFO_HEIGHT(new_vip))
        return TRUE;
    return FALSE;
}

static gboolean
gst_vaapipostproc_update_sink_caps(GstVaapiPostproc *postproc, GstCaps *caps,
    gboolean *caps_changed_ptr)
{
    GstVideoInfo vi;
    gboolean deinterlace;

    if (!gst_video_info_from_caps(&vi, caps))
        return FALSE;

    if (video_info_changed(&vi, &postproc->sinkpad_info))
        postproc->sinkpad_info = vi, *caps_changed_ptr = TRUE;

    deinterlace = is_deinterlace_enabled(postproc, &vi);
    if (deinterlace)
        postproc->flags |= GST_VAAPI_POSTPROC_FLAG_DEINTERLACE;
    postproc->field_duration = gst_util_uint64_scale(
        GST_SECOND, GST_VIDEO_INFO_FPS_D(&vi),
        (1 + deinterlace) * GST_VIDEO_INFO_FPS_N(&vi));

    postproc->is_raw_yuv = GST_VIDEO_INFO_IS_YUV(&vi);
#if !GST_CHECK_VERSION(1,0,0)
    if (postproc->is_raw_yuv) {
        /* Ensure the uploader is set up for upstream allocated buffers */
        GstVaapiUploader * const uploader = postproc->uploader;
        if (!gst_vaapi_uploader_ensure_display(uploader, postproc->display))
            return FALSE;
        if (!gst_vaapi_uploader_ensure_caps(uploader, caps, NULL))
            return FALSE;
    }
#endif
    return TRUE;
}

static gboolean
gst_vaapipostproc_update_src_caps(GstVaapiPostproc *postproc, GstCaps *caps,
    gboolean *caps_changed_ptr)
{
    GstVideoInfo vi;

    if (!gst_video_info_from_caps(&vi, caps))
        return FALSE;

    if (video_info_changed(&vi, &postproc->srcpad_info))
        postproc->srcpad_info = vi, *caps_changed_ptr = TRUE;

    if (postproc->format != GST_VIDEO_INFO_FORMAT(&postproc->sinkpad_info))
        postproc->flags |= GST_VAAPI_POSTPROC_FLAG_FORMAT;

    if ((postproc->width || postproc->height) &&
        postproc->width != GST_VIDEO_INFO_WIDTH(&postproc->sinkpad_info) &&
        postproc->height != GST_VIDEO_INFO_HEIGHT(&postproc->sinkpad_info))
        postproc->flags |= GST_VAAPI_POSTPROC_FLAG_SIZE;
    return TRUE;
}

static gboolean
ensure_allowed_sinkpad_caps(GstVaapiPostproc *postproc)
{
    GstCaps *out_caps, *yuv_caps;

    if (postproc->allowed_sinkpad_caps)
        return TRUE;

    /* Create VA caps */
    out_caps = gst_caps_from_string(GST_VAAPI_SURFACE_CAPS ", "
        GST_CAPS_INTERLACED_MODES);
    if (!out_caps) {
        GST_ERROR("failed to create VA sink caps");
        return FALSE;
    }

    /* Append YUV caps */
    if (gst_vaapipostproc_ensure_uploader(postproc)) {
        yuv_caps = gst_vaapi_uploader_get_caps(postproc->uploader);
        if (yuv_caps)
            gst_caps_append(out_caps, gst_caps_ref(yuv_caps));
        else
            GST_WARNING("failed to create YUV sink caps");
    }
    postproc->allowed_sinkpad_caps = out_caps;

    /* XXX: append VA/VPP filters */
    return TRUE;
}

/* Build list of supported formats */
static gboolean
build_format_list_value(GArray *formats, GValue *out_value)
{
    GValue v_format = { 0, };
    guint i;
#if GST_CHECK_VERSION(1,0,0)
    const gchar *str;

    g_value_init(out_value, GST_TYPE_LIST);

    g_value_init(&v_format, G_TYPE_STRING);
    g_value_set_string(&v_format, "encoded");
    gst_value_list_append_value(out_value, &v_format);

    for (i = 0; i < formats->len; i++) {
        GstVideoFormat const format =
            g_array_index(formats, GstVideoFormat, i);

        str = gst_vaapi_video_format_to_string(format);
        if (!str)
            continue;
        g_value_set_string(&v_format, str);
        gst_value_list_append_value(out_value, &v_format);
    }
#else
    guint32 fourcc;

    g_value_init(out_value, GST_TYPE_LIST);
    g_value_init(&v_format, GST_TYPE_FOURCC);
    for (i = 0; i < formats->len; i++) {
        GstVideoFormat const format =
            g_array_index(formats, GstVideoFormat, i);

        fourcc = gst_video_format_to_fourcc(format);
        if (!fourcc)
            continue;
        gst_value_set_fourcc(&v_format, fourcc);
        gst_value_list_append_value(out_value, &v_format);
    }
#endif

    g_value_unset(&v_format);
    return TRUE;
}

/* Fixup output caps so that to reflect the supported set of pixel formats */
static GstCaps *
expand_allowed_srcpad_caps(GstVaapiPostproc *postproc, GstCaps *caps)
{
    GValue value = { 0, };
    guint i, num_structures;
    gboolean had_filter;

    had_filter = postproc->filter != NULL;
    if (!had_filter && !gst_vaapipostproc_ensure_filter(postproc))
        goto cleanup;
    if (!gst_vaapipostproc_ensure_filter_caps(postproc))
        goto cleanup;

    /* Reset "format" field for each structure */
    if (!build_format_list_value(postproc->filter_formats, &value))
        goto cleanup;

    num_structures = gst_caps_get_size(caps);
    for (i = 0; i < num_structures; i++) {
        GstStructure * const structure = gst_caps_get_structure(caps, i);
        if (!structure)
            continue;
        gst_structure_set_value(structure, "format", &value);
    }
    g_value_unset(&value);

cleanup:
    if (!had_filter)
        gst_vaapipostproc_destroy_filter(postproc);
    return caps;
}

static gboolean
ensure_allowed_srcpad_caps(GstVaapiPostproc *postproc)
{
    GstCaps *out_caps;

    if (postproc->allowed_srcpad_caps)
        return TRUE;

    /* Create initial caps from pad template */
    out_caps = gst_caps_from_string(gst_vaapipostproc_src_caps_str);
    if (!out_caps) {
        GST_ERROR("failed to create VA src caps");
        return FALSE;
    }

    postproc->allowed_srcpad_caps =
        expand_allowed_srcpad_caps(postproc, out_caps);
    return postproc->allowed_srcpad_caps != NULL;
}

static void
find_best_size(GstVaapiPostproc *postproc, GstVideoInfo *vip,
    guint *width_ptr, guint *height_ptr)
{
    guint width, height;

    width  = GST_VIDEO_INFO_WIDTH(vip);
    height = GST_VIDEO_INFO_HEIGHT(vip);
    if (postproc->width && postproc->height) {
        width = postproc->width;
        height = postproc->height;
    }
    else if (postproc->keep_aspect) {
        const gdouble ratio  = (gdouble)width / height;
        if (postproc->width) {
            width = postproc->width;
            height = postproc->width / ratio;
        }
        else if (postproc->height) {
            height = postproc->height;
            width = postproc->height * ratio;
        }
    }
    else if (postproc->width)
        width = postproc->width;
    else if (postproc->height)
        height = postproc->height;

    *width_ptr = width;
    *height_ptr = height;
}

static GstCaps *
gst_vaapipostproc_transform_caps_impl(GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);
    GstVideoInfo vi;
    GstVideoFormat format;
    GstCaps *out_caps;
    guint width, height;
    gint fps_n, fps_d, par_n, par_d;

    if (!gst_caps_is_fixed(caps)) {
        out_caps = NULL;
        if (direction == GST_PAD_SINK) {
            /* Generate the allowed set of caps on the src pad */
            if (ensure_allowed_srcpad_caps(postproc))
                out_caps = gst_caps_ref(postproc->allowed_srcpad_caps);
        }
        else {
            /* Generate the allowed set of caps on the sink pad */
            if (ensure_allowed_sinkpad_caps(postproc))
                out_caps = gst_caps_ref(postproc->allowed_sinkpad_caps);
        }
        return out_caps;
    }

    /* Generate the other pad caps, based on the current pad caps, as
       specified by the direction argument */
    if (!gst_video_info_from_caps(&vi, caps))
        return NULL;

    format = GST_VIDEO_INFO_FORMAT(&vi);
    if (format == GST_VIDEO_FORMAT_UNKNOWN)
        return NULL;

    fps_n = GST_VIDEO_INFO_FPS_N(&vi);
    fps_d = GST_VIDEO_INFO_FPS_D(&vi);
    if (direction == GST_PAD_SINK) {
        if (is_deinterlace_enabled(postproc, &vi)) {
            /* Set double framerate in interlaced mode */
            if (!gst_util_fraction_multiply(fps_n, fps_d, 2, 1, &fps_n, &fps_d))
                return NULL;
        }
        format = GST_VIDEO_FORMAT_ENCODED;

        /* Signal the other pad that we generate only progressive frames */
        GST_VIDEO_INFO_INTERLACE_MODE(&vi) =
            GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

        find_best_size(postproc, &vi, &width, &height);
        gst_video_info_set_format(&vi, format, width, height);
    }
    else {
        if (is_deinterlace_enabled(postproc, &vi)) {
            /* Set half framerate in interlaced mode */
            if (!gst_util_fraction_multiply(fps_n, fps_d, 1, 2, &fps_n, &fps_d))
                return NULL;
        }
    }

    if (format != GST_VIDEO_FORMAT_ENCODED) {
        out_caps = gst_video_info_to_caps(&vi);
        if (!out_caps)
            return NULL;
    }
    else {
        /* XXX: gst_video_info_to_caps() from GStreamer 0.10 does not
           reconstruct suitable caps for "encoded" video formats */
        out_caps = gst_caps_from_string(GST_VAAPI_SURFACE_CAPS_NAME);
        if (!out_caps)
            return NULL;

        par_n = GST_VIDEO_INFO_PAR_N(&vi);
        par_d = GST_VIDEO_INFO_PAR_D(&vi);
        gst_caps_set_simple(out_caps,
            "type", G_TYPE_STRING, "vaapi",
            "opengl", G_TYPE_BOOLEAN, USE_GLX,
            "width", G_TYPE_INT, GST_VIDEO_INFO_WIDTH(&vi),
            "height", G_TYPE_INT, GST_VIDEO_INFO_HEIGHT(&vi),
            "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d,
            NULL);
    }

    gst_caps_set_interlaced(out_caps, &vi);
    return out_caps;
}

#if GST_CHECK_VERSION(1,0,0)
static GstCaps *
gst_vaapipostproc_transform_caps(GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
    GstCaps *out_caps;

    caps = gst_vaapipostproc_transform_caps_impl(trans, direction, caps);
    if (caps && filter) {
        out_caps = gst_caps_intersect_full(caps, filter,
            GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(caps);
        return out_caps;
    }
    return caps;
}
#else
#define gst_vaapipostproc_transform_caps \
    gst_vaapipostproc_transform_caps_impl
#endif

static gboolean
gst_vaapipostproc_transform_size(GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, gsize size,
    GstCaps *othercaps, gsize *othersize)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);

    if (direction == GST_PAD_SINK || !postproc->is_raw_yuv)
        *othersize = 0;
    else
        *othersize = size;
    return TRUE;
}

static GstBuffer *
get_source_buffer(GstVaapiPostproc *postproc, GstBuffer *inbuf)
{
    GstVaapiVideoMeta *meta;
    GstBuffer *outbuf;

    meta = gst_buffer_get_vaapi_video_meta(inbuf);
    if (meta)
        return gst_buffer_ref(inbuf);

#if GST_CHECK_VERSION(1,0,0)
    outbuf = NULL;
    goto error_invalid_buffer;
    return outbuf;

    /* ERRORS */
error_invalid_buffer:
    {
        GST_ERROR("failed to validate source buffer");
        return NULL;
    }
#else
    outbuf = gst_vaapi_uploader_get_buffer(postproc->uploader);
    if (!outbuf)
        goto error_create_buffer;
    if (!gst_vaapi_uploader_process(postproc->uploader, inbuf, outbuf))
        goto error_copy_buffer;

    gst_buffer_copy_metadata(outbuf, inbuf,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);
    return outbuf;

    /* ERRORS */
error_create_buffer:
    {
        GST_ERROR("failed to create buffer");
        return NULL;
    }
error_copy_buffer:
    {
        GST_ERROR("failed to upload buffer to VA surface");
        gst_buffer_unref(outbuf);
        return NULL;
    }
#endif
}

static GstFlowReturn
gst_vaapipostproc_transform(GstBaseTransform *trans, GstBuffer *inbuf,
    GstBuffer *outbuf)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);
    GstBuffer *buf;
    GstFlowReturn ret;

    buf = get_source_buffer(postproc, inbuf);
    if (!buf)
        return GST_FLOW_ERROR;

    ret = GST_FLOW_NOT_SUPPORTED;
    if (postproc->flags) {
        /* Use VA/VPP extensions to process this frame */
        if (postproc->use_vpp &&
            postproc->flags != GST_VAAPI_POSTPROC_FLAG_DEINTERLACE) {
            ret = gst_vaapipostproc_process_vpp(trans, buf, outbuf);
            if (ret != GST_FLOW_NOT_SUPPORTED)
                goto done;
            GST_WARNING("unsupported VPP filters. Disabling");
            postproc->use_vpp = FALSE;
        }

        /* Only append picture structure meta data (top/bottom field) */
        if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_DEINTERLACE) {
            ret = gst_vaapipostproc_process(trans, buf, outbuf);
            if (ret != GST_FLOW_NOT_SUPPORTED)
                goto done;
        }
    }

    /* Fallback: passthrough to the downstream element as is */
    ret = gst_vaapipostproc_passthrough(trans, buf, outbuf);

done:
    gst_buffer_unref(buf);
    return ret;
}

static GstFlowReturn
gst_vaapipostproc_prepare_output_buffer(GstBaseTransform *trans,
    GstBuffer *inbuf,
#if !GST_CHECK_VERSION(1,0,0)
    gint size, GstCaps *caps,
#endif
    GstBuffer **outbuf_ptr)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);

    *outbuf_ptr = create_output_buffer(postproc);
    return *outbuf_ptr ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static gboolean
ensure_sinkpad_buffer_pool(GstVaapiPostproc *postproc, GstCaps *caps)
{
#if GST_CHECK_VERSION(1,0,0)
    GstBufferPool *pool;
    GstCaps *pool_caps;
    GstStructure *config;
    GstVideoInfo vi;
    gboolean need_pool;

    if (!gst_vaapipostproc_ensure_display(postproc))
        return FALSE;

    if (postproc->sinkpad_buffer_pool) {
        config = gst_buffer_pool_get_config(postproc->sinkpad_buffer_pool);
        gst_buffer_pool_config_get_params(config, &pool_caps, NULL, NULL, NULL);
        need_pool = !gst_caps_is_equal(caps, pool_caps);
        gst_structure_free(config);
        if (!need_pool)
            return TRUE;
        g_clear_object(&postproc->sinkpad_buffer_pool);
        postproc->sinkpad_buffer_size = 0;
    }

    pool = gst_vaapi_video_buffer_pool_new(postproc->display);
    if (!pool)
        goto error_create_pool;

    gst_video_info_init(&vi);
    gst_video_info_from_caps(&vi, caps);
    if (GST_VIDEO_INFO_FORMAT(&vi) == GST_VIDEO_FORMAT_ENCODED) {
        GST_DEBUG("assume sink pad buffer pool format is NV12");
        gst_video_info_set_format(&vi, GST_VIDEO_FORMAT_NV12,
            GST_VIDEO_INFO_WIDTH(&vi), GST_VIDEO_INFO_HEIGHT(&vi));
    }
    postproc->sinkpad_buffer_size = vi.size;

    config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_set_params(config, caps,
        postproc->sinkpad_buffer_size, 0, 0);
    gst_buffer_pool_config_add_option(config,
        GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META);
    gst_buffer_pool_config_add_option(config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    if (!gst_buffer_pool_set_config(pool, config))
        goto error_pool_config;
    postproc->sinkpad_buffer_pool = pool;
    return TRUE;

    /* ERRORS */
error_create_pool:
    {
        GST_ERROR("failed to create buffer pool");
        return FALSE;
    }
error_pool_config:
    {
        GST_ERROR("failed to reset buffer pool config");
        gst_object_unref(pool);
        return FALSE;
    }
#else
    return TRUE;
#endif
}

static gboolean
ensure_srcpad_buffer_pool(GstVaapiPostproc *postproc, GstCaps *caps)
{
    GstVideoInfo vi;
    GstVaapiVideoPool *pool;

    gst_video_info_init(&vi);
    gst_video_info_from_caps(&vi, caps);
    gst_video_info_set_format(&vi, postproc->format,
        GST_VIDEO_INFO_WIDTH(&vi), GST_VIDEO_INFO_HEIGHT(&vi));

    if (!video_info_changed(&vi, &postproc->filter_pool_info))
        return TRUE;
    postproc->filter_pool_info = vi;

    pool = gst_vaapi_surface_pool_new(postproc->display,
        &postproc->filter_pool_info);
    if (!pool)
        return FALSE;

    gst_vaapi_video_pool_replace(&postproc->filter_pool, pool);
    gst_vaapi_video_pool_unref(pool);
    return TRUE;
}

static gboolean
gst_vaapipostproc_set_caps(GstBaseTransform *trans, GstCaps *caps,
    GstCaps *out_caps)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);
    gboolean caps_changed = FALSE;

    if (!gst_vaapipostproc_update_sink_caps(postproc, caps, &caps_changed))
        return FALSE;
    if (!gst_vaapipostproc_update_src_caps(postproc, out_caps, &caps_changed))
        return FALSE;

    if (caps_changed) {
        gst_vaapipostproc_destroy(postproc);
        gst_caps_replace(&postproc->sinkpad_caps, caps);
        gst_caps_replace(&postproc->srcpad_caps, out_caps);
        if (!gst_vaapipostproc_create(postproc))
            return FALSE;
    }

    if (!ensure_sinkpad_buffer_pool(postproc, caps))
        return FALSE;
    if (!ensure_srcpad_buffer_pool(postproc, out_caps))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapipostproc_query(GstBaseTransform *trans, GstPadDirection direction,
    GstQuery *query)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);

    GST_INFO_OBJECT(trans, "query type `%s'", GST_QUERY_TYPE_NAME(query));

    if (gst_vaapi_reply_to_query(query, postproc->display)) {
        GST_DEBUG("sharing display %p", postproc->display);
        return TRUE;
    }

    return GST_BASE_TRANSFORM_CLASS(gst_vaapipostproc_parent_class)->query(
        trans, direction, query);
}

#if GST_CHECK_VERSION(1,0,0)
static gboolean
gst_vaapipostproc_propose_allocation(GstBaseTransform *trans,
    GstQuery *decide_query, GstQuery *query)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);
    GstCaps *caps = NULL;
    gboolean need_pool;

    /* Let vaapidecode allocate the video buffers */
    if (!postproc->is_raw_yuv)
        return FALSE;

    gst_query_parse_allocation(query, &caps, &need_pool);

    if (need_pool) {
        if (!caps)
            goto error_no_caps;
        if (!ensure_sinkpad_buffer_pool(postproc, caps))
            return FALSE;
        gst_query_add_allocation_pool(query, postproc->sinkpad_buffer_pool,
            postproc->sinkpad_buffer_size, 0, 0);
    }

    gst_query_add_allocation_meta(query,
        GST_VAAPI_VIDEO_META_API_TYPE, NULL);
    gst_query_add_allocation_meta(query,
        GST_VIDEO_META_API_TYPE, NULL);
    gst_query_add_allocation_meta(query,
        GST_VIDEO_CROP_META_API_TYPE, NULL);
    gst_query_add_allocation_meta(query,
        GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL);
    return TRUE;

    /* ERRORS */
error_no_caps:
    {
        GST_ERROR("no caps specified");
        return FALSE;
    }
}
#endif

static void
gst_vaapipostproc_finalize(GObject *object)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(object);

    gst_vaapipostproc_destroy(postproc);

    G_OBJECT_CLASS(gst_vaapipostproc_parent_class)->finalize(object);
}

static void
gst_vaapipostproc_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(object);

    switch (prop_id) {
    case PROP_FORMAT:
        postproc->format = g_value_get_enum(value);
        break;
    case PROP_WIDTH:
        postproc->width = g_value_get_uint(value);
        break;
    case PROP_HEIGHT:
        postproc->height = g_value_get_uint(value);
        break;
    case PROP_FORCE_ASPECT_RATIO:
        postproc->keep_aspect = g_value_get_boolean(value);
        break;
    case PROP_DEINTERLACE_MODE:
        postproc->deinterlace_mode = g_value_get_enum(value);
        break;
    case PROP_DEINTERLACE_METHOD:
        postproc->deinterlace_method = g_value_get_enum(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapipostproc_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(object);

    switch (prop_id) {
    case PROP_FORMAT:
        g_value_set_enum(value, postproc->format);
        break;
    case PROP_WIDTH:
        g_value_set_uint(value, postproc->width);
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, postproc->height);
        break;
    case PROP_FORCE_ASPECT_RATIO:
        g_value_set_boolean(value, postproc->keep_aspect);
        break;
    case PROP_DEINTERLACE_MODE:
        g_value_set_enum(value, postproc->deinterlace_mode);
        break;
    case PROP_DEINTERLACE_METHOD:
        g_value_set_enum(value, postproc->deinterlace_method);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapipostproc_class_init(GstVaapiPostprocClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass * const trans_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstPadTemplate *pad_template;
    GPtrArray *filter_ops;
    GstVaapiFilterOpInfo *filter_op;

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapipostproc,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    object_class->finalize      = gst_vaapipostproc_finalize;
    object_class->set_property  = gst_vaapipostproc_set_property;
    object_class->get_property  = gst_vaapipostproc_get_property;
    trans_class->start          = gst_vaapipostproc_start;
    trans_class->stop           = gst_vaapipostproc_stop;
    trans_class->transform_caps = gst_vaapipostproc_transform_caps;
    trans_class->transform_size = gst_vaapipostproc_transform_size;
    trans_class->transform      = gst_vaapipostproc_transform;
    trans_class->set_caps       = gst_vaapipostproc_set_caps;
    trans_class->query          = gst_vaapipostproc_query;

#if GST_CHECK_VERSION(1,0,0)
    trans_class->propose_allocation = gst_vaapipostproc_propose_allocation;
#endif

    trans_class->prepare_output_buffer =
        gst_vaapipostproc_prepare_output_buffer;

    gst_element_class_set_static_metadata(element_class,
        "VA-API video postprocessing",
        "Filter/Converter/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

    /* sink pad */
    pad_template = gst_static_pad_template_get(&gst_vaapipostproc_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);

    /* src pad */
    pad_template = gst_static_pad_template_get(&gst_vaapipostproc_src_factory);
    gst_element_class_add_pad_template(element_class, pad_template);

    /**
     * GstVaapiPostproc:deinterlace-mode:
     *
     * This selects whether the deinterlacing should always be applied or if
     * they should only be applied on content that has the "interlaced" flag
     * on the caps.
     */
    g_object_class_install_property
        (object_class,
         PROP_DEINTERLACE_MODE,
         g_param_spec_enum("deinterlace-mode",
                           "Deinterlace mode",
                           "Deinterlace mode to use",
                           GST_VAAPI_TYPE_DEINTERLACE_MODE,
                           DEFAULT_DEINTERLACE_MODE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /**
     * GstVaapiPostproc:deinterlace-method:
     *
     * This selects the deinterlacing method to apply.
     */
    g_object_class_install_property
        (object_class,
         PROP_DEINTERLACE_METHOD,
         g_param_spec_enum("deinterlace-method",
                           "Deinterlace method",
                           "Deinterlace method to use",
                           GST_VAAPI_TYPE_DEINTERLACE_METHOD,
                           DEFAULT_DEINTERLACE_METHOD,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    filter_ops = gst_vaapi_filter_get_operations(NULL);
    if (!filter_ops)
        return;

    /**
     * GstVaapiPostproc:format:
     *
     * The forced output pixel format, expressed as a #GstVideoFormat.
     */
    filter_op = find_filter_op(filter_ops, GST_VAAPI_FILTER_OP_FORMAT);
    if (filter_op)
        g_object_class_install_property(object_class,
            PROP_FORMAT, filter_op->pspec);

    /**
     * GstVaapiPostproc:width:
     *
     * The forced output width in pixels. If set to zero, the width is
     * calculated from the height if aspect ration is preserved, or
     * inherited from the sink caps width
     */
    g_object_class_install_property
        (object_class,
         PROP_WIDTH,
         g_param_spec_uint("width",
                           "Width",
                           "Forced output width",
                           0, G_MAXINT, 0,
                           G_PARAM_READWRITE));

    /**
     * GstVaapiPostproc:height:
     *
     * The forced output height in pixels. If set to zero, the height
     * is calculated from the width if aspect ration is preserved, or
     * inherited from the sink caps height
     */
    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "Height",
                           "Forced output height",
                           0, G_MAXINT, 0,
                           G_PARAM_READWRITE));

    /**
     * GstVaapiPostproc:force-aspect-ratio:
     *
     * When enabled, scaling respects video aspect ratio; when
     * disabled, the video is distorted to fit the width and height
     * properties.
     */
    g_object_class_install_property
        (object_class,
         PROP_FORCE_ASPECT_RATIO,
         g_param_spec_boolean("force-aspect-ratio",
                              "Force aspect ratio",
                              "When enabled, scaling will respect original aspect ratio",
                              TRUE,
                              G_PARAM_READWRITE));

    g_ptr_array_unref(filter_ops);
}

static void
gst_vaapipostproc_init(GstVaapiPostproc *postproc)
{
    postproc->format                    = DEFAULT_FORMAT;
    postproc->deinterlace_mode          = DEFAULT_DEINTERLACE_MODE;
    postproc->deinterlace_method        = DEFAULT_DEINTERLACE_METHOD;
    postproc->field_duration            = GST_CLOCK_TIME_NONE;
    postproc->keep_aspect               = TRUE;

    gst_video_info_init(&postproc->sinkpad_info);
    gst_video_info_init(&postproc->srcpad_info);
    gst_video_info_init(&postproc->filter_pool_info);
}
