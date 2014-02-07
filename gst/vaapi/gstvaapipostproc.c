/*
 *  gstvaapipostproc.c - VA-API video postprocessing
 *
 *  Copyright (C) 2012-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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
#if GST_CHECK_VERSION(1,1,0)
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(
        GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE, "{ ENCODED, NV12, I420, YV12 }") ", "
#else
    GST_VAAPI_SURFACE_CAPS ", "
#endif
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
#if GST_CHECK_VERSION(1,1,0)
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(
        GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE, "{ ENCODED, NV12, I420, YV12 }") ", "
#else
    GST_VAAPI_SURFACE_CAPS ", "
#endif
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

G_DEFINE_TYPE_WITH_CODE(
    GstVaapiPostproc,
    gst_vaapipostproc,
    GST_TYPE_BASE_TRANSFORM,
    GST_VAAPI_PLUGIN_BASE_INIT_INTERFACES)

enum {
    PROP_0,

    PROP_FORMAT,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_FORCE_ASPECT_RATIO,
    PROP_DEINTERLACE_MODE,
    PROP_DEINTERLACE_METHOD,
    PROP_DENOISE,
    PROP_SHARPEN,
    PROP_HUE,
    PROP_SATURATION,
    PROP_BRIGHTNESS,
    PROP_CONTRAST,
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

static void
ds_reset(GstVaapiDeinterlaceState *ds)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(ds->buffers); i++)
        gst_buffer_replace(&ds->buffers[i], NULL);
    ds->buffers_index = 0;
    ds->num_surfaces = 0;
    ds->deint = FALSE;
    ds->tff = FALSE;
}

static void
ds_add_buffer(GstVaapiDeinterlaceState *ds, GstBuffer *buf)
{
    gst_buffer_replace(&ds->buffers[ds->buffers_index], buf);
    ds->buffers_index = (ds->buffers_index + 1) % G_N_ELEMENTS(ds->buffers);
}

static inline GstBuffer *
ds_get_buffer(GstVaapiDeinterlaceState *ds, guint index)
{
    /* Note: the index increases towards older buffers.
       i.e. buffer at index 0 means the immediately preceding buffer
       in the history, buffer at index 1 means the one preceding the
       surface at index 0, etc. */
    const guint n = ds->buffers_index + G_N_ELEMENTS(ds->buffers) - index - 1;
    return ds->buffers[n % G_N_ELEMENTS(ds->buffers)];
}

static void
ds_set_surfaces(GstVaapiDeinterlaceState *ds)
{
    GstVaapiVideoMeta *meta;
    guint i;

    ds->num_surfaces = 0;
    for (i = 0; i < G_N_ELEMENTS(ds->buffers); i++) {
        GstBuffer * const buf = ds_get_buffer(ds, i);
        if (!buf)
            break;

        meta = gst_buffer_get_vaapi_video_meta(buf);
        ds->surfaces[ds->num_surfaces++] =
            gst_vaapi_video_meta_get_surface(meta);
    }
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
    return gst_vaapi_plugin_base_ensure_display(GST_VAAPI_PLUGIN_BASE(postproc));
}

static gboolean
gst_vaapipostproc_ensure_uploader(GstVaapiPostproc *postproc)
{
    if (!gst_vaapipostproc_ensure_display(postproc))
        return FALSE;
    if (!gst_vaapi_plugin_base_ensure_uploader(GST_VAAPI_PLUGIN_BASE(postproc)))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapipostproc_ensure_filter(GstVaapiPostproc *postproc)
{
    if (postproc->filter)
        return TRUE;

    if (!gst_vaapipostproc_ensure_display(postproc))
        return FALSE;

    postproc->filter = gst_vaapi_filter_new(
        GST_VAAPI_PLUGIN_BASE_DISPLAY(postproc));
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
    if (!gst_vaapi_plugin_base_open(GST_VAAPI_PLUGIN_BASE(postproc)))
        return FALSE;
    if (!gst_vaapipostproc_ensure_display(postproc))
        return FALSE;
    if (!gst_vaapipostproc_ensure_uploader(postproc))
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
    gst_vaapi_video_pool_replace(&postproc->filter_pool, NULL);
}

static void
gst_vaapipostproc_destroy(GstVaapiPostproc *postproc)
{
    ds_reset(&postproc->deinterlace_state);
    gst_vaapipostproc_destroy_filter(postproc);

    gst_caps_replace(&postproc->allowed_sinkpad_caps, NULL);
    gst_caps_replace(&postproc->allowed_srcpad_caps, NULL);
    gst_vaapi_plugin_base_close(GST_VAAPI_PLUGIN_BASE(postproc));
}

static gboolean
gst_vaapipostproc_start(GstBaseTransform *trans)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);

    ds_reset(&postproc->deinterlace_state);
    if (!gst_vaapi_plugin_base_open(GST_VAAPI_PLUGIN_BASE(postproc)))
        return FALSE;
    if (!gst_vaapipostproc_ensure_display(postproc))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapipostproc_stop(GstBaseTransform *trans)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);

    ds_reset(&postproc->deinterlace_state);
    gst_vaapi_plugin_base_close(GST_VAAPI_PLUGIN_BASE(postproc));
    return TRUE;
}

static gboolean
should_deinterlace_buffer(GstVaapiPostproc *postproc, GstBuffer *buf)
{
    if (!(postproc->flags & GST_VAAPI_POSTPROC_FLAG_DEINTERLACE) ||
        postproc->deinterlace_mode == GST_VAAPI_DEINTERLACE_MODE_DISABLED)
        return FALSE;

    if (postproc->deinterlace_mode == GST_VAAPI_DEINTERLACE_MODE_INTERLACED)
        return TRUE;

    g_assert(postproc->deinterlace_mode == GST_VAAPI_DEINTERLACE_MODE_AUTO);

    switch (GST_VIDEO_INFO_INTERLACE_MODE(&postproc->sinkpad_info)) {
    case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
        return TRUE;
    case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
        return FALSE;
    case GST_VIDEO_INTERLACE_MODE_MIXED:
#if GST_CHECK_VERSION(1,0,0)
        if (GST_BUFFER_FLAG_IS_SET(buf, GST_VIDEO_BUFFER_FLAG_INTERLACED))
            return TRUE;
#else
        if (!GST_BUFFER_FLAG_IS_SET(buf, GST_VIDEO_BUFFER_PROGRESSIVE))
            return TRUE;
#endif
        break;
    default:
        GST_ERROR("unhandled \"interlace-mode\", disabling deinterlacing" );
        break;
    }
    return FALSE;
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
    gst_buffer_set_caps(outbuf, GST_VAAPI_PLUGIN_BASE_SRC_PAD_CAPS(postproc));
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

static gboolean
deint_method_is_advanced(GstVaapiDeinterlaceMethod deint_method)
{
    gboolean is_advanced;

    switch (deint_method) {
    case GST_VAAPI_DEINTERLACE_METHOD_MOTION_ADAPTIVE:
    case GST_VAAPI_DEINTERLACE_METHOD_MOTION_COMPENSATED:
        is_advanced = TRUE;
        break;
    default:
        is_advanced = FALSE;
        break;
    }
    return is_advanced;
}

static GstVaapiDeinterlaceMethod
get_next_deint_method(GstVaapiDeinterlaceMethod deint_method)
{
    switch (deint_method) {
    case GST_VAAPI_DEINTERLACE_METHOD_MOTION_COMPENSATED:
        deint_method = GST_VAAPI_DEINTERLACE_METHOD_MOTION_ADAPTIVE;
        break;
    default:
        /* Default to basic "bob" for all others */
        deint_method = GST_VAAPI_DEINTERLACE_METHOD_BOB;
        break;
    }
    return deint_method;
}

static gboolean
set_best_deint_method(GstVaapiPostproc *postproc, guint flags,
    GstVaapiDeinterlaceMethod *deint_method_ptr)
{
    GstVaapiDeinterlaceMethod deint_method = postproc->deinterlace_method;
    gboolean success;

    for (;;) {
        success = gst_vaapi_filter_set_deinterlacing(postproc->filter,
            deint_method, flags);
        if (success || deint_method == GST_VAAPI_DEINTERLACE_METHOD_BOB)
            break;
        deint_method = get_next_deint_method(deint_method);
    }
    *deint_method_ptr = deint_method;
    return success;
}

static GstFlowReturn
gst_vaapipostproc_process_vpp(GstBaseTransform *trans, GstBuffer *inbuf,
    GstBuffer *outbuf)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);
    GstVaapiDeinterlaceState * const ds = &postproc->deinterlace_state;
    GstVaapiVideoMeta *inbuf_meta, *outbuf_meta;
    GstVaapiSurface *inbuf_surface, *outbuf_surface;
    GstVaapiFilterStatus status;
    GstClockTime timestamp;
    GstFlowReturn ret;
    GstBuffer *fieldbuf;
    GstVaapiDeinterlaceMethod deint_method;
    guint flags, deint_flags;
    gboolean tff, deint, deint_refs, deint_changed;
    GstVaapiRectangle *crop_rect = NULL;

    /* Validate filters */
    if ((postproc->flags & GST_VAAPI_POSTPROC_FLAG_FORMAT) &&
        !gst_vaapi_filter_set_format(postproc->filter, postproc->format))
        return GST_FLOW_NOT_SUPPORTED;

    if ((postproc->flags & GST_VAAPI_POSTPROC_FLAG_DENOISE) &&
        !gst_vaapi_filter_set_denoising_level(postproc->filter,
            postproc->denoise_level))
        return GST_FLOW_NOT_SUPPORTED;

    if ((postproc->flags & GST_VAAPI_POSTPROC_FLAG_SHARPEN) &&
        !gst_vaapi_filter_set_sharpening_level(postproc->filter,
            postproc->sharpen_level))
        return GST_FLOW_NOT_SUPPORTED;

    if ((postproc->flags & GST_VAAPI_POSTPROC_FLAG_HUE) &&
        !gst_vaapi_filter_set_hue(postproc->filter,
            postproc->hue))
        return GST_FLOW_NOT_SUPPORTED;

    if ((postproc->flags & GST_VAAPI_POSTPROC_FLAG_SATURATION) &&
        !gst_vaapi_filter_set_saturation(postproc->filter,
            postproc->saturation))
        return GST_FLOW_NOT_SUPPORTED;

    if ((postproc->flags & GST_VAAPI_POSTPROC_FLAG_BRIGHTNESS) &&
        !gst_vaapi_filter_set_brightness(postproc->filter,
            postproc->brightness))
        return GST_FLOW_NOT_SUPPORTED;

    if ((postproc->flags & GST_VAAPI_POSTPROC_FLAG_CONTRAST) &&
        !gst_vaapi_filter_set_contrast(postproc->filter,
            postproc->contrast))
        return GST_FLOW_NOT_SUPPORTED;

    inbuf_meta = gst_buffer_get_vaapi_video_meta(inbuf);
    if (!inbuf_meta)
        goto error_invalid_buffer;
    inbuf_surface = gst_vaapi_video_meta_get_surface(inbuf_meta);

#if GST_CHECK_VERSION(1,0,0)
    GstVideoCropMeta * const crop_meta =
        gst_buffer_get_video_crop_meta(inbuf);
    if (crop_meta) {
        GstVaapiRectangle tmp_rect;
        crop_rect = &tmp_rect;
        crop_rect->x = crop_meta->x;
        crop_rect->y = crop_meta->y;
        crop_rect->width = crop_meta->width;
        crop_rect->height = crop_meta->height;
    }
#endif
    if (!crop_rect)
        crop_rect = (GstVaapiRectangle *)
            gst_vaapi_video_meta_get_render_rect(inbuf_meta);

    timestamp  = GST_BUFFER_TIMESTAMP(inbuf);
    tff        = GST_BUFFER_FLAG_IS_SET(inbuf, GST_VIDEO_BUFFER_FLAG_TFF);
    deint      = should_deinterlace_buffer(postproc, inbuf);

    /* Drop references if deinterlacing conditions changed */
    deint_changed = deint != ds->deint;
    if (deint_changed || (ds->num_surfaces > 0 && tff != ds->tff))
        ds_reset(ds);

    deint_method = postproc->deinterlace_method;
    deint_refs = deint_method_is_advanced(deint_method);
    if (deint_refs) {
        GstBuffer * const prev_buf = ds_get_buffer(ds, 0);
        GstClockTime prev_pts, pts = GST_BUFFER_TIMESTAMP(inbuf);
        /* Reset deinterlacing state when there is a discontinuity */
        if (prev_buf && (prev_pts = GST_BUFFER_TIMESTAMP(prev_buf)) != pts) {
            const GstClockTimeDiff pts_diff = GST_CLOCK_DIFF(prev_pts, pts);
            if (pts_diff < 0 || pts_diff > postproc->field_duration * 2)
                ds_reset(ds);
        }
    }

    ds->deint = deint;
    ds->tff = tff;

    flags = gst_vaapi_video_meta_get_render_flags(inbuf_meta) &
        ~GST_VAAPI_PICTURE_STRUCTURE_MASK;

    /* First field */
    if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_DEINTERLACE) {
        fieldbuf = create_output_buffer(postproc);
        if (!fieldbuf)
            goto error_create_buffer;

        outbuf_meta = gst_vaapi_video_meta_new_from_pool(postproc->filter_pool);
        if (!outbuf_meta)
            goto error_create_meta;
        outbuf_surface = gst_vaapi_video_meta_get_surface(outbuf_meta);

        if (deint) {
            deint_flags = (tff ? GST_VAAPI_DEINTERLACE_FLAG_TOPFIELD : 0);
            if (tff)
                deint_flags |= GST_VAAPI_DEINTERLACE_FLAG_TFF;
            if (!set_best_deint_method(postproc, deint_flags, &deint_method))
                goto error_op_deinterlace;

            if (deint_method != postproc->deinterlace_method) {
                GST_DEBUG("unsupported deinterlace-method %u. Using %u instead",
                          postproc->deinterlace_method, deint_method);
                postproc->deinterlace_method = deint_method;
                deint_refs = deint_method_is_advanced(deint_method);
            }

            if (deint_refs) {
                ds_set_surfaces(ds);
                if (!gst_vaapi_filter_set_deinterlacing_references(
                        postproc->filter, ds->surfaces, ds->num_surfaces,
                        NULL, 0))
                    goto error_op_deinterlace;
            }
        }
        else if (deint_changed) {
            // Reset internal filter to non-deinterlacing mode
            deint_method = GST_VAAPI_DEINTERLACE_METHOD_NONE;
            if (!gst_vaapi_filter_set_deinterlacing(postproc->filter,
                    deint_method, 0))
                goto error_op_deinterlace;
        }

        gst_vaapi_filter_set_cropping_rectangle(postproc->filter, crop_rect);
        status = gst_vaapi_filter_process(postproc->filter, inbuf_surface,
            outbuf_surface, flags);
        if (status != GST_VAAPI_FILTER_STATUS_SUCCESS)
            goto error_process_vpp;

        gst_buffer_set_vaapi_video_meta(fieldbuf, outbuf_meta);
        gst_vaapi_video_meta_unref(outbuf_meta);

        GST_BUFFER_TIMESTAMP(fieldbuf) = timestamp;
        GST_BUFFER_DURATION(fieldbuf)  = postproc->field_duration;
        ret = gst_pad_push(trans->srcpad, fieldbuf);
        if (ret != GST_FLOW_OK)
            goto error_push_buffer;
    }
    fieldbuf = NULL;

    /* Second field */
    outbuf_meta = gst_vaapi_video_meta_new_from_pool(postproc->filter_pool);
    if (!outbuf_meta)
        goto error_create_meta;
    outbuf_surface = gst_vaapi_video_meta_get_surface(outbuf_meta);

    if (deint) {
        deint_flags = (tff ? 0 : GST_VAAPI_DEINTERLACE_FLAG_TOPFIELD);
        if (tff)
            deint_flags |= GST_VAAPI_DEINTERLACE_FLAG_TFF;
        if (!gst_vaapi_filter_set_deinterlacing(postproc->filter,
                deint_method, deint_flags))
            goto error_op_deinterlace;

        if (deint_refs && !gst_vaapi_filter_set_deinterlacing_references(
                postproc->filter, ds->surfaces, ds->num_surfaces, NULL, 0))
            goto error_op_deinterlace;
    }
    else if (deint_changed && !gst_vaapi_filter_set_deinterlacing(
                 postproc->filter, deint_method, 0))
        goto error_op_deinterlace;

    gst_vaapi_filter_set_cropping_rectangle(postproc->filter, crop_rect);
    status = gst_vaapi_filter_process(postproc->filter, inbuf_surface,
        outbuf_surface, flags);
    if (status != GST_VAAPI_FILTER_STATUS_SUCCESS)
        goto error_process_vpp;

    if (!(postproc->flags & GST_VAAPI_POSTPROC_FLAG_DEINTERLACE))
        gst_buffer_copy_into(outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
    else {
        GST_BUFFER_TIMESTAMP(outbuf) = timestamp + postproc->field_duration;
        GST_BUFFER_DURATION(outbuf)  = postproc->field_duration;
    }
    gst_buffer_set_vaapi_video_meta(outbuf, outbuf_meta);
    gst_vaapi_video_meta_unref(outbuf_meta);

    if (deint && deint_refs)
        ds_add_buffer(ds, inbuf);
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
        return GST_FLOW_ERROR;
    }
error_create_meta:
    {
        GST_ERROR("failed to create new output buffer meta");
        gst_buffer_replace(&fieldbuf, NULL);
        gst_vaapi_video_meta_unref(outbuf_meta);
        return GST_FLOW_ERROR;
    }
error_op_deinterlace:
    {
        GST_ERROR("failed to apply deinterlacing filter");
        gst_buffer_replace(&fieldbuf, NULL);
        gst_vaapi_video_meta_unref(outbuf_meta);
        return GST_FLOW_NOT_SUPPORTED;
    }
error_process_vpp:
    {
        GST_ERROR("failed to apply VPP filters (error %d)", status);
        gst_buffer_replace(&fieldbuf, NULL);
        gst_vaapi_video_meta_unref(outbuf_meta);
        return GST_FLOW_ERROR;
    }
error_push_buffer:
    {
        if (ret != GST_FLOW_FLUSHING)
            GST_ERROR("failed to push output buffer to video sink");
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
    deint      = should_deinterlace_buffer(postproc, inbuf);

    flags = gst_vaapi_video_meta_get_render_flags(meta) &
        ~GST_VAAPI_PICTURE_STRUCTURE_MASK;

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
#if GST_CHECK_VERSION(1,1,0)
    out_caps = gst_static_pad_template_get_caps(
        &gst_vaapipostproc_sink_factory);
#else
    out_caps = gst_caps_from_string(GST_VAAPI_SURFACE_CAPS ", "
        GST_CAPS_INTERLACED_MODES);
#endif
    if (!out_caps) {
        GST_ERROR("failed to create VA sink caps");
        return FALSE;
    }

    /* Append YUV caps */
    if (gst_vaapipostproc_ensure_uploader(postproc)) {
        yuv_caps = GST_VAAPI_PLUGIN_BASE_UPLOADER_CAPS(postproc);
        if (yuv_caps) {
            out_caps = gst_caps_make_writable(out_caps);
            gst_caps_append(out_caps, gst_caps_copy(yuv_caps));
        }
        else
            GST_WARNING("failed to create YUV sink caps");
    }
    postproc->allowed_sinkpad_caps = out_caps;

    /* XXX: append VA/VPP filters */
    return TRUE;
}

/* Fixup output caps so that to reflect the supported set of pixel formats */
static GstCaps *
expand_allowed_srcpad_caps(GstVaapiPostproc *postproc, GstCaps *caps)
{
    GValue value = G_VALUE_INIT, v_format = G_VALUE_INIT;
    guint i, num_structures;
    gboolean had_filter;

    had_filter = postproc->filter != NULL;
    if (!had_filter && !gst_vaapipostproc_ensure_filter(postproc))
        goto cleanup;
    if (!gst_vaapipostproc_ensure_filter_caps(postproc))
        goto cleanup;

    /* Reset "format" field for each structure */
    if (!gst_vaapi_value_set_format_list(&value, postproc->filter_formats))
        goto cleanup;
    if (gst_vaapi_value_set_format(&v_format, GST_VIDEO_FORMAT_ENCODED)) {
        gst_value_list_prepend_value(&value, &v_format);
        g_value_unset(&v_format);
    }

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

    /* Generate the sink pad caps, that could be fixated afterwards */
    if (direction == GST_PAD_SRC) {
        if (!ensure_allowed_sinkpad_caps(postproc))
            return NULL;
        return gst_caps_ref(postproc->allowed_sinkpad_caps);
    }

    /* Generate complete set of src pad caps if non-fixated sink pad
       caps are provided */
    if (!gst_caps_is_fixed(caps)) {
        if (!ensure_allowed_srcpad_caps(postproc))
            return NULL;
        return gst_caps_ref(postproc->allowed_srcpad_caps);
    }

    /* Generate the expected src pad caps, from the current fixated
       sink pad caps */
    if (!gst_video_info_from_caps(&vi, caps))
        return NULL;

    // Set double framerate in interlaced mode
    if (is_deinterlace_enabled(postproc, &vi)) {
        gint fps_n = GST_VIDEO_INFO_FPS_N(&vi);
        gint fps_d = GST_VIDEO_INFO_FPS_D(&vi);
        if (!gst_util_fraction_multiply(fps_n, fps_d, 2, 1, &fps_n, &fps_d))
            return NULL;
        GST_VIDEO_INFO_FPS_N(&vi) = fps_n;
        GST_VIDEO_INFO_FPS_D(&vi) = fps_d;
    }

    // Signal the other pad that we only generate progressive frames
    GST_VIDEO_INFO_INTERLACE_MODE(&vi) = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

    // Update size from user-specified parameters
#if GST_CHECK_VERSION(1,1,0)
    format = postproc->format;
#else
    format = GST_VIDEO_FORMAT_ENCODED;
#endif
    find_best_size(postproc, &vi, &width, &height);
    gst_video_info_set_format(&vi, format, width, height);

#if GST_CHECK_VERSION(1,1,0)
    out_caps = gst_video_info_to_caps(&vi);
    if (!out_caps)
        return NULL;

    gst_caps_set_features(out_caps, 0,
        gst_caps_features_new(GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE, NULL));
#else
    /* XXX: gst_video_info_to_caps() from GStreamer 0.10 does not
       reconstruct suitable caps for "encoded" video formats */
    out_caps = gst_caps_from_string(GST_VAAPI_SURFACE_CAPS_NAME);
    if (!out_caps)
        return NULL;

    gst_caps_set_simple(out_caps,
        "type", G_TYPE_STRING, "vaapi",
        "opengl", G_TYPE_BOOLEAN, USE_GLX,
        "width", G_TYPE_INT, GST_VIDEO_INFO_WIDTH(&vi),
        "height", G_TYPE_INT, GST_VIDEO_INFO_HEIGHT(&vi),
        "framerate", GST_TYPE_FRACTION, GST_VIDEO_INFO_FPS_N(&vi),
            GST_VIDEO_INFO_FPS_D(&vi),
        "pixel-aspect-ratio", GST_TYPE_FRACTION, GST_VIDEO_INFO_PAR_N(&vi),
            GST_VIDEO_INFO_PAR_D(&vi),
        NULL);

    gst_caps_set_interlaced(out_caps, &vi);
#endif
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

#if GST_CHECK_VERSION(1,0,0)
typedef gsize GstBaseTransformSizeType;
#else
typedef guint GstBaseTransformSizeType;
#endif

static gboolean
gst_vaapipostproc_transform_size(GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, GstBaseTransformSizeType size,
    GstCaps *othercaps, GstBaseTransformSizeType *othersize)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);

    if (direction == GST_PAD_SINK || !postproc->is_raw_yuv)
        *othersize = 0;
    else
        *othersize = size;
    return TRUE;
}

static GstFlowReturn
gst_vaapipostproc_transform(GstBaseTransform *trans, GstBuffer *inbuf,
    GstBuffer *outbuf)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(trans);
    GstBuffer *buf;
    GstFlowReturn ret;

    ret = gst_vaapi_plugin_base_get_input_buffer(
        GST_VAAPI_PLUGIN_BASE(postproc), inbuf, &buf);
    if (ret != GST_FLOW_OK)
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
ensure_srcpad_buffer_pool(GstVaapiPostproc *postproc, GstCaps *caps)
{
    GstVideoInfo vi;
    GstVaapiVideoPool *pool;

    gst_video_info_init(&vi);
    gst_video_info_from_caps(&vi, caps);
    gst_video_info_set_format(&vi, postproc->format,
        GST_VIDEO_INFO_WIDTH(&vi), GST_VIDEO_INFO_HEIGHT(&vi));

    if (postproc->filter_pool && !video_info_changed(&vi, &postproc->filter_pool_info))
        return TRUE;
    postproc->filter_pool_info = vi;

    pool = gst_vaapi_surface_pool_new(GST_VAAPI_PLUGIN_BASE_DISPLAY(postproc),
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
        if (!gst_vaapipostproc_create(postproc))
            return FALSE;
        if (!gst_vaapi_plugin_base_set_caps(GST_VAAPI_PLUGIN_BASE(trans),
                caps, out_caps))
            return FALSE;
    }

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

    if (gst_vaapi_reply_to_query(query, GST_VAAPI_PLUGIN_BASE_DISPLAY(postproc))) {
        GST_DEBUG("sharing display %p", GST_VAAPI_PLUGIN_BASE_DISPLAY(postproc));
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
    GstVaapiPluginBase * const plugin = GST_VAAPI_PLUGIN_BASE(trans);

    /* Let vaapidecode allocate the video buffers */
    if (!postproc->is_raw_yuv)
        return FALSE;
    if (!gst_vaapi_plugin_base_propose_allocation(plugin, query))
        return FALSE;
    return TRUE;
}
#endif

static void
gst_vaapipostproc_finalize(GObject *object)
{
    GstVaapiPostproc * const postproc = GST_VAAPIPOSTPROC(object);

    gst_vaapipostproc_destroy(postproc);

    gst_vaapi_plugin_base_finalize(GST_VAAPI_PLUGIN_BASE(postproc));
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
     case PROP_DENOISE:
         postproc->denoise_level = g_value_get_float(value);
         postproc->flags |= GST_VAAPI_POSTPROC_FLAG_DENOISE;
         break;
     case PROP_SHARPEN:
         postproc->sharpen_level = g_value_get_float(value);
         postproc->flags |= GST_VAAPI_POSTPROC_FLAG_SHARPEN;
         break;
    case PROP_HUE:
        postproc->hue = g_value_get_float(value);
        postproc->flags |= GST_VAAPI_POSTPROC_FLAG_HUE;
        break;
    case PROP_SATURATION:
        postproc->saturation = g_value_get_float(value);
        postproc->flags |= GST_VAAPI_POSTPROC_FLAG_SATURATION;
        break;
    case PROP_BRIGHTNESS:
        postproc->brightness = g_value_get_float(value);
        postproc->flags |= GST_VAAPI_POSTPROC_FLAG_BRIGHTNESS;
        break;
    case PROP_CONTRAST:
        postproc->contrast = g_value_get_float(value);
        postproc->flags |= GST_VAAPI_POSTPROC_FLAG_CONTRAST;
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
    case PROP_DENOISE:
        g_value_set_float(value, postproc->denoise_level);
        break;
    case PROP_SHARPEN:
        g_value_set_float(value, postproc->sharpen_level);
        break;
    case PROP_HUE:
        g_value_set_float(value, postproc->hue);
        break;
    case PROP_SATURATION:
        g_value_set_float(value, postproc->saturation);
        break;
    case PROP_BRIGHTNESS:
        g_value_set_float(value, postproc->brightness);
        break;
    case PROP_CONTRAST:
        g_value_set_float(value, postproc->contrast);
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

    gst_vaapi_plugin_base_class_init(GST_VAAPI_PLUGIN_BASE_CLASS(klass));

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
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /**
     * GstVaapiPostproc:denoise:
     *
     * The level of noise reduction to apply.
     */
    filter_op = find_filter_op(filter_ops, GST_VAAPI_FILTER_OP_DENOISE);
    if (filter_op)
        g_object_class_install_property(object_class,
            PROP_DENOISE, filter_op->pspec);

    /**
     * GstVaapiPostproc:sharpen:
     *
     * The level of sharpening to apply for positive values, or the
     * level of blurring for negative values.
     */
    filter_op = find_filter_op(filter_ops, GST_VAAPI_FILTER_OP_SHARPEN);
    if (filter_op)
        g_object_class_install_property(object_class,
            PROP_SHARPEN, filter_op->pspec);

    /**
     * GstVaapiPostproc:hue:
     *
     * The color hue, expressed as a float value. Range is -180.0 to
     * 180.0. Default value is 0.0 and represents no modification.
     */
    filter_op = find_filter_op(filter_ops, GST_VAAPI_FILTER_OP_HUE);
    if (filter_op)
        g_object_class_install_property(object_class,
            PROP_HUE, filter_op->pspec);

    /**
     * GstVaapiPostproc:saturation:
     *
     * The color saturation, expressed as a float value. Range is 0.0
     * to 2.0. Default value is 1.0 and represents no modification.
     */
    filter_op = find_filter_op(filter_ops, GST_VAAPI_FILTER_OP_SATURATION);
    if (filter_op)
        g_object_class_install_property(object_class,
            PROP_SATURATION, filter_op->pspec);

    /**
     * GstVaapiPostproc:brightness:
     *
     * The color brightness, expressed as a float value. Range is -1.0
     * to 1.0. Default value is 0.0 and represents no modification.
     */
    filter_op = find_filter_op(filter_ops, GST_VAAPI_FILTER_OP_BRIGHTNESS);
    if (filter_op)
        g_object_class_install_property(object_class,
            PROP_BRIGHTNESS, filter_op->pspec);

    /**
     * GstVaapiPostproc:contrast:
     *
     * The color contrast, expressed as a float value. Range is 0.0 to
     * 2.0. Default value is 1.0 and represents no modification.
     */
    filter_op = find_filter_op(filter_ops, GST_VAAPI_FILTER_OP_CONTRAST);
    if (filter_op)
        g_object_class_install_property(object_class,
            PROP_CONTRAST, filter_op->pspec);

    g_ptr_array_unref(filter_ops);
}

static void
gst_vaapipostproc_init(GstVaapiPostproc *postproc)
{
    gst_vaapi_plugin_base_init(GST_VAAPI_PLUGIN_BASE(postproc), GST_CAT_DEFAULT);

    postproc->format                    = DEFAULT_FORMAT;
    postproc->deinterlace_mode          = DEFAULT_DEINTERLACE_MODE;
    postproc->deinterlace_method        = DEFAULT_DEINTERLACE_METHOD;
    postproc->field_duration            = GST_CLOCK_TIME_NONE;
    postproc->keep_aspect               = TRUE;

    gst_video_info_init(&postproc->sinkpad_info);
    gst_video_info_init(&postproc->srcpad_info);
    gst_video_info_init(&postproc->filter_pool_info);
}
