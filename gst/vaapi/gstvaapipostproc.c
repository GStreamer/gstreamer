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

#define GST_PLUGIN_NAME "vaapipostproc"
#define GST_PLUGIN_DESC "A video postprocessing filter"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapipostproc);
#define GST_CAT_DEFAULT gst_debug_vaapipostproc

/* Default templates */
static const char gst_vaapipostproc_sink_caps_str[] =
    GST_VAAPI_SURFACE_CAPS ", "
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

    PROP_DEINTERLACE_MODE,
    PROP_DEINTERLACE_METHOD,
};

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

static inline gboolean
gst_vaapipostproc_ensure_display(GstVaapiPostproc *postproc)
{
    return gst_vaapi_ensure_display(postproc, GST_VAAPI_DISPLAY_TYPE_ANY,
        &postproc->display);
}

static gboolean
gst_vaapipostproc_create(GstVaapiPostproc *postproc)
{
    if (!gst_vaapipostproc_ensure_display(postproc))
        return FALSE;
    return TRUE;
}

static void
gst_vaapipostproc_destroy(GstVaapiPostproc *postproc)
{
    gst_vaapi_display_replace(&postproc->display, NULL);

    gst_caps_replace(&postproc->sinkpad_caps, NULL);
    gst_caps_replace(&postproc->srcpad_caps,  NULL);
    gst_caps_replace(&postproc->allowed_caps, NULL);
}

static gboolean
gst_vaapipostproc_reset(GstVaapiPostproc *postproc)
{
    gst_vaapipostproc_destroy(postproc);
    return gst_vaapipostproc_create(postproc);
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
gst_vaapipostproc_process(GstBaseTransform *trans, GstBuffer *inbuf,
    GstBuffer *outbuf)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);
    GstVaapiVideoMeta *meta;
    GstClockTime timestamp;
    GstFlowReturn ret;
    GstBuffer *fieldbuf;
    guint fieldbuf_flags, outbuf_flags, flags;
    gboolean tff;

    meta = gst_buffer_get_vaapi_video_meta(inbuf);
    if (!meta)
        goto error_invalid_buffer;

    timestamp  = GST_BUFFER_TIMESTAMP(inbuf);
    tff        = GST_BUFFER_FLAG_IS_SET(inbuf, GST_VIDEO_BUFFER_FLAG_TFF);

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
    fieldbuf_flags |= postproc->deinterlace ? (
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
    outbuf_flags |= postproc->deinterlace ? (
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

    if (!gst_video_info_from_caps(&vi, caps))
        return FALSE;

    if (video_info_changed(&vi, &postproc->sinkpad_info))
        postproc->sinkpad_info = vi, *caps_changed_ptr = TRUE;

    postproc->deinterlace = is_deinterlace_enabled(postproc, &vi);
    postproc->field_duration = gst_util_uint64_scale(
        GST_SECOND, GST_VIDEO_INFO_FPS_D(&vi),
        (1 + postproc->deinterlace) * GST_VIDEO_INFO_FPS_N(&vi));
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
    return TRUE;
}

static gboolean
ensure_allowed_caps(GstVaapiPostproc *postproc)
{
    if (postproc->allowed_caps)
        return TRUE;

    postproc->allowed_caps =
        gst_caps_from_string(gst_vaapipostproc_sink_caps_str);
    if (!postproc->allowed_caps)
        return FALSE;

    /* XXX: append VA/VPP filters */
    return TRUE;
}

static GstCaps *
gst_vaapipostproc_transform_caps_impl(GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);
    GstVideoInfo vi;
    GstVideoFormat format;
    GstCaps *out_caps;
    gint fps_n, fps_d, par_n, par_d;

    if (!gst_caps_is_fixed(caps)) {
        if (direction == GST_PAD_SINK)
            return gst_caps_from_string(gst_vaapipostproc_src_caps_str);

        /* Generate the allowed set of caps on the sink pad */
        if (!ensure_allowed_caps(postproc))
            return NULL;
        return gst_caps_ref(postproc->allowed_caps);
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

            /* Signal the other pad that we generate only progressive frames */
            GST_VIDEO_INFO_INTERLACE_MODE(&vi) =
                GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
        }
    }
    else {
        if (is_deinterlace_enabled(postproc, &vi)) {
            /* Set half framerate in interlaced mode */
            if (!gst_util_fraction_multiply(fps_n, fps_d, 1, 2, &fps_n, &fps_d))
                return NULL;
        }
    }

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

static GstFlowReturn
gst_vaapipostproc_transform(GstBaseTransform *trans, GstBuffer *inbuf,
    GstBuffer *outbuf)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);
    GstFlowReturn ret;

    if (postproc->deinterlace)
        ret = gst_vaapipostproc_process(trans, inbuf, outbuf);
    else
        ret = gst_vaapipostproc_passthrough(trans, inbuf, outbuf);
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
        if (!gst_vaapipostproc_reset(postproc))
            return FALSE;
        gst_caps_replace(&postproc->sinkpad_caps, caps);
        gst_caps_replace(&postproc->srcpad_caps, out_caps);
    }
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

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapipostproc,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    object_class->finalize      = gst_vaapipostproc_finalize;
    object_class->set_property  = gst_vaapipostproc_set_property;
    object_class->get_property  = gst_vaapipostproc_get_property;
    trans_class->start          = gst_vaapipostproc_start;
    trans_class->stop           = gst_vaapipostproc_stop;
    trans_class->transform_caps = gst_vaapipostproc_transform_caps;
    trans_class->transform      = gst_vaapipostproc_transform;
    trans_class->set_caps       = gst_vaapipostproc_set_caps;
    trans_class->query          = gst_vaapipostproc_query;

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
}

static void
gst_vaapipostproc_init(GstVaapiPostproc *postproc)
{
    postproc->deinterlace               = FALSE;
    postproc->deinterlace_mode          = DEFAULT_DEINTERLACE_MODE;
    postproc->deinterlace_method        = DEFAULT_DEINTERLACE_METHOD;
    postproc->field_duration            = GST_CLOCK_TIME_NONE;

    gst_video_info_init(&postproc->sinkpad_info);
    gst_video_info_init(&postproc->srcpad_info);
}
