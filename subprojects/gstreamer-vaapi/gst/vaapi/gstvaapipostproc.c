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
 * SECTION:element-vaapipostproc
 * @short_description: A VA-API base video postprocessing filter
 *
 * vaapipostproc consists in various postprocessing algorithms to be
 * applied to VA surfaces.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 videotestsrc ! vaapipostproc ! video/x-raw, width=1920, height=1080 ! vaapisink
 * ]|
 */

#include "gstcompat.h"
#include <gst/video/video.h>

#include <gst/vaapi/gstvaapivalue.h>

#include "gstvaapipostproc.h"
#include "gstvaapipostprocutil.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideobuffer.h"
#include "gstvaapivideobufferpool.h"
#include "gstvaapivideomemory.h"

#define GST_PLUGIN_NAME "vaapipostproc"
#define GST_PLUGIN_DESC "A VA-API video postprocessing filter"

GST_DEBUG_CATEGORY_STATIC (gst_debug_vaapipostproc);
#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT gst_debug_vaapipostproc
#else
#define GST_CAT_DEFAULT NULL
#endif

/* Default templates */
/* *INDENT-OFF* */
static const char gst_vaapipostproc_sink_caps_str[] =
  GST_VAAPI_MAKE_SURFACE_CAPS ", "
  GST_CAPS_INTERLACED_MODES "; "
  GST_VIDEO_CAPS_MAKE (GST_VAAPI_FORMATS_ALL) ", "
  GST_CAPS_INTERLACED_MODES "; "
  GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_DMABUF,
      GST_VAAPI_FORMATS_ALL) ", "
  GST_CAPS_INTERLACED_MODES;
/* *INDENT-ON* */

/* *INDENT-OFF* */
static const char gst_vaapipostproc_src_caps_str[] =
  GST_VAAPI_MAKE_SURFACE_CAPS ", "
  GST_CAPS_INTERLACED_FALSE "; "
#if (GST_VAAPI_USE_GLX || GST_VAAPI_USE_EGL)
  GST_VAAPI_MAKE_GLTEXUPLOAD_CAPS "; "
#endif
  GST_VIDEO_CAPS_MAKE (GST_VAAPI_FORMATS_ALL) ", "
  GST_CAPS_INTERLACED_MODES;
/* *INDENT-ON* */

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_vaapipostproc_sink_factory =
  GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (gst_vaapipostproc_sink_caps_str));
/* *INDENT-ON* */

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_vaapipostproc_src_factory =
  GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (gst_vaapipostproc_src_caps_str));
/* *INDENT-ON* */

static void gst_vaapipostproc_colorbalance_init (gpointer iface, gpointer data);

G_DEFINE_TYPE_WITH_CODE (GstVaapiPostproc, gst_vaapipostproc,
    GST_TYPE_BASE_TRANSFORM, GST_VAAPI_PLUGIN_BASE_INIT_INTERFACES
    G_IMPLEMENT_INTERFACE (GST_TYPE_COLOR_BALANCE,
        gst_vaapipostproc_colorbalance_init));

GST_VAAPI_PLUGIN_BASE_DEFINE_SET_CONTEXT (gst_vaapipostproc_parent_class);

static GstVideoFormat native_formats[] =
    { GST_VIDEO_FORMAT_NV12, GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_I420 };

enum
{
  PROP_0,

#ifndef GST_REMOVE_DEPRECATED
  PROP_FORMAT,
  PROP_WIDTH,
  PROP_HEIGHT,
#endif
  PROP_FORCE_ASPECT_RATIO,
  PROP_DEINTERLACE_MODE,
  PROP_DEINTERLACE_METHOD,
  PROP_DENOISE,
  PROP_SHARPEN,
  PROP_HUE,
  PROP_SATURATION,
  PROP_BRIGHTNESS,
  PROP_CONTRAST,
  PROP_SCALE_METHOD,
  PROP_VIDEO_DIRECTION,
  PROP_CROP_LEFT,
  PROP_CROP_RIGHT,
  PROP_CROP_TOP,
  PROP_CROP_BOTTOM,
  PROP_HDR_TONE_MAP,
#ifndef GST_REMOVE_DEPRECATED
  PROP_SKIN_TONE_ENHANCEMENT,
#endif
  PROP_SKIN_TONE_ENHANCEMENT_LEVEL,
};

#define GST_VAAPI_TYPE_HDR_TONE_MAP \
    gst_vaapi_hdr_tone_map_get_type()

static GType
gst_vaapi_hdr_tone_map_get_type (void)
{
  static gsize g_type = 0;

  static const GEnumValue enum_values[] = {
    {GST_VAAPI_HDR_TONE_MAP_AUTO,
        "Auto detection", "auto"},
    {GST_VAAPI_HDR_TONE_MAP_DISABLED,
        "Disable HDR tone mapping", "disabled"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&g_type)) {
    const GType type =
        g_enum_register_static ("GstVaapiHDRToneMap", enum_values);
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}

#define GST_VAAPI_TYPE_DEINTERLACE_MODE \
    gst_vaapi_deinterlace_mode_get_type()

static GType
gst_vaapi_deinterlace_mode_get_type (void)
{
  static GType deinterlace_mode_type = 0;

  static const GEnumValue mode_types[] = {
    {GST_VAAPI_DEINTERLACE_MODE_AUTO,
        "Auto detection", "auto"},
    {GST_VAAPI_DEINTERLACE_MODE_INTERLACED,
        "Force deinterlacing", "interlaced"},
    {GST_VAAPI_DEINTERLACE_MODE_DISABLED,
        "Never deinterlace", "disabled"},
    {0, NULL, NULL},
  };

  if (!deinterlace_mode_type) {
    deinterlace_mode_type =
        g_enum_register_static ("GstVaapiDeinterlaceMode", mode_types);
  }
  return deinterlace_mode_type;
}

static void
ds_reset (GstVaapiDeinterlaceState * ds)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (ds->buffers); i++)
    gst_buffer_replace (&ds->buffers[i], NULL);
  ds->buffers_index = 0;
  ds->num_surfaces = 0;
  ds->deint = FALSE;
  ds->tff = FALSE;
}

static void
ds_add_buffer (GstVaapiDeinterlaceState * ds, GstBuffer * buf)
{
  gst_buffer_replace (&ds->buffers[ds->buffers_index], buf);
  ds->buffers_index = (ds->buffers_index + 1) % G_N_ELEMENTS (ds->buffers);
}

static inline GstBuffer *
ds_get_buffer (GstVaapiDeinterlaceState * ds, guint index)
{
  /* Note: the index increases towards older buffers.
     i.e. buffer at index 0 means the immediately preceding buffer
     in the history, buffer at index 1 means the one preceding the
     surface at index 0, etc. */
  const guint n = ds->buffers_index + G_N_ELEMENTS (ds->buffers) - index - 1;
  return ds->buffers[n % G_N_ELEMENTS (ds->buffers)];
}

static void
ds_set_surfaces (GstVaapiDeinterlaceState * ds)
{
  GstVaapiVideoMeta *meta;
  guint i;

  ds->num_surfaces = 0;
  for (i = 0; i < G_N_ELEMENTS (ds->buffers); i++) {
    GstBuffer *const buf = ds_get_buffer (ds, i);
    if (!buf)
      break;

    meta = gst_buffer_get_vaapi_video_meta (buf);
    ds->surfaces[ds->num_surfaces++] = gst_vaapi_video_meta_get_surface (meta);
  }
}

static GstVaapiFilterOpInfo *
find_filter_op (GPtrArray * filter_ops, GstVaapiFilterOp op)
{
  guint i;

  if (filter_ops) {
    for (i = 0; i < filter_ops->len; i++) {
      GstVaapiFilterOpInfo *const filter_op = g_ptr_array_index (filter_ops, i);
      if (filter_op->op == op)
        return filter_op;
    }
  }
  return NULL;
}

static inline gboolean
gst_vaapipostproc_ensure_display (GstVaapiPostproc * postproc)
{
  return
      gst_vaapi_plugin_base_ensure_display (GST_VAAPI_PLUGIN_BASE (postproc));
}

static gboolean
gst_vaapipostproc_ensure_filter (GstVaapiPostproc * postproc)
{
  if (postproc->filter)
    return TRUE;

  if (!gst_vaapipostproc_ensure_display (postproc))
    return FALSE;

  gst_caps_replace (&postproc->allowed_srcpad_caps, NULL);
  gst_caps_replace (&postproc->allowed_sinkpad_caps, NULL);

  postproc->filter =
      gst_vaapi_filter_new (GST_VAAPI_PLUGIN_BASE_DISPLAY (postproc));
  if (!postproc->filter)
    return FALSE;
  return TRUE;
}

static gboolean
gst_vaapipostproc_ensure_filter_caps (GstVaapiPostproc * postproc)
{
  if (!gst_vaapipostproc_ensure_filter (postproc))
    return FALSE;

  if (!postproc->filter_ops) {
    postproc->filter_ops = gst_vaapi_filter_get_operations (postproc->filter);
    if (!postproc->filter_ops)
      return FALSE;
  }

  if (!postproc->filter_formats) {
    postproc->filter_formats = gst_vaapi_filter_get_formats
        (postproc->filter, NULL, NULL, NULL, NULL);
    if (!postproc->filter_formats)
      return FALSE;
  }
  return TRUE;
}

static gboolean
gst_vaapipostproc_create (GstVaapiPostproc * postproc)
{
  if (!gst_vaapi_plugin_base_open (GST_VAAPI_PLUGIN_BASE (postproc)))
    return FALSE;
  if (!gst_vaapipostproc_ensure_display (postproc))
    return FALSE;

  postproc->use_vpp = FALSE;
  postproc->has_vpp = gst_vaapipostproc_ensure_filter (postproc);
  return TRUE;
}

static void
gst_vaapipostproc_destroy_filter (GstVaapiPostproc * postproc)
{
  if (postproc->filter_formats) {
    g_array_unref (postproc->filter_formats);
    postproc->filter_formats = NULL;
  }

  if (postproc->filter_ops) {
    g_ptr_array_unref (postproc->filter_ops);
    postproc->filter_ops = NULL;
  }
  if (postproc->cb_channels) {
    g_list_free_full (postproc->cb_channels, g_object_unref);
    postproc->cb_channels = NULL;
  }
  gst_vaapi_filter_replace (&postproc->filter, NULL);
  gst_vaapi_video_pool_replace (&postproc->filter_pool, NULL);
}

static void
gst_vaapipostproc_destroy (GstVaapiPostproc * postproc)
{
  ds_reset (&postproc->deinterlace_state);
  gst_vaapipostproc_destroy_filter (postproc);

  gst_caps_replace (&postproc->allowed_sinkpad_caps, NULL);
  gst_caps_replace (&postproc->allowed_srcpad_caps, NULL);
  gst_vaapi_plugin_base_close (GST_VAAPI_PLUGIN_BASE (postproc));
}

static gboolean
gst_vaapipostproc_start (GstBaseTransform * trans)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);

  ds_reset (&postproc->deinterlace_state);
  if (!gst_vaapi_plugin_base_open (GST_VAAPI_PLUGIN_BASE (postproc)))
    return FALSE;
  g_mutex_lock (&postproc->postproc_lock);
  gst_vaapipostproc_ensure_filter (postproc);
  g_mutex_unlock (&postproc->postproc_lock);

  return TRUE;
}

static gboolean
gst_vaapipostproc_stop (GstBaseTransform * trans)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);

  g_mutex_lock (&postproc->postproc_lock);
  ds_reset (&postproc->deinterlace_state);
  gst_vaapi_plugin_base_close (GST_VAAPI_PLUGIN_BASE (postproc));

  postproc->field_duration = GST_CLOCK_TIME_NONE;
  gst_video_info_init (&postproc->sinkpad_info);
  gst_video_info_init (&postproc->srcpad_info);
  gst_video_info_init (&postproc->filter_pool_info);
  g_mutex_unlock (&postproc->postproc_lock);

  return TRUE;
}

static gboolean
should_deinterlace_buffer (GstVaapiPostproc * postproc, GstBuffer * buf)
{
  if (!(postproc->flags & GST_VAAPI_POSTPROC_FLAG_DEINTERLACE) ||
      postproc->deinterlace_mode == GST_VAAPI_DEINTERLACE_MODE_DISABLED)
    return FALSE;

  if (postproc->deinterlace_mode == GST_VAAPI_DEINTERLACE_MODE_INTERLACED)
    return TRUE;

  g_assert (postproc->deinterlace_mode == GST_VAAPI_DEINTERLACE_MODE_AUTO);

  switch (GST_VIDEO_INFO_INTERLACE_MODE (&postproc->sinkpad_info)) {
    case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
      return TRUE;
    case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
      return FALSE;
    case GST_VIDEO_INTERLACE_MODE_MIXED:
      if (GST_BUFFER_FLAG_IS_SET (buf, GST_VIDEO_BUFFER_FLAG_INTERLACED))
        return TRUE;
      break;
    default:
      GST_ERROR_OBJECT (postproc,
          "unhandled \"interlace-mode\", disabling deinterlacing");
      break;
  }
  return FALSE;
}

static GstBuffer *
create_output_buffer (GstVaapiPostproc * postproc)
{
  GstBuffer *outbuf;

  GstBufferPool *const pool =
      GST_VAAPI_PLUGIN_BASE_SRC_PAD_BUFFER_POOL (postproc);
  GstFlowReturn ret;

  g_return_val_if_fail (pool != NULL, NULL);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE))
    goto error_activate_pool;

  outbuf = NULL;
  ret = gst_buffer_pool_acquire_buffer (pool, &outbuf, NULL);
  if (ret != GST_FLOW_OK || !outbuf)
    goto error_create_buffer;
  return outbuf;

  /* ERRORS */
error_activate_pool:
  {
    GST_ERROR_OBJECT (postproc, "failed to activate output video buffer pool");
    return NULL;
  }
error_create_buffer:
  {
    GST_ERROR_OBJECT (postproc, "failed to create output video buffer");
    return NULL;
  }
}

static inline GstBuffer *
create_output_dump_buffer (GstVaapiPostproc * postproc)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (postproc);

  return
      gst_buffer_new_allocate (GST_VAAPI_PLUGIN_BASE_OTHER_ALLOCATOR (plugin),
      GST_VIDEO_INFO_SIZE (GST_VAAPI_PLUGIN_BASE_SRC_PAD_INFO (plugin)),
      &GST_VAAPI_PLUGIN_BASE_OTHER_ALLOCATOR_PARAMS (plugin));
}

static void
copy_metadata (GstVaapiPostproc * postproc, GstBuffer * outbuf,
    GstBuffer * inbuf)
{
  GstBaseTransformClass *bclass = GST_BASE_TRANSFORM_GET_CLASS (postproc);
  GstBaseTransform *trans = GST_BASE_TRANSFORM (postproc);

  if (inbuf == outbuf)
    return;
  if (!bclass->copy_metadata)
    return;
  if (!bclass->copy_metadata (trans, inbuf, outbuf)) {
    /* something failed, post a warning */
    GST_ELEMENT_WARNING (trans, STREAM, NOT_IMPLEMENTED,
        ("could not copy metadata"), (NULL));
  }
}

static gboolean
append_output_buffer_metadata (GstVaapiPostproc * postproc, GstBuffer * outbuf,
    GstBuffer * inbuf, guint flags)
{
  GstVaapiVideoMeta *inbuf_meta, *outbuf_meta;
  GstVaapiSurfaceProxy *proxy;

  gst_buffer_copy_into (outbuf, inbuf, flags | GST_BUFFER_COPY_FLAGS, 0, -1);

  copy_metadata (postproc, outbuf, inbuf);

  /* GstVaapiVideoMeta */
  inbuf_meta = gst_buffer_get_vaapi_video_meta (inbuf);
  g_return_val_if_fail (inbuf_meta != NULL, FALSE);
  proxy = gst_vaapi_video_meta_get_surface_proxy (inbuf_meta);

  outbuf_meta = gst_buffer_get_vaapi_video_meta (outbuf);
  g_return_val_if_fail (outbuf_meta != NULL, FALSE);
  proxy = gst_vaapi_surface_proxy_copy (proxy);
  if (!proxy)
    return FALSE;

  gst_vaapi_video_meta_set_surface_proxy (outbuf_meta, proxy);
  gst_vaapi_surface_proxy_unref (proxy);
  return TRUE;
}

static gboolean
deint_method_is_advanced (GstVaapiDeinterlaceMethod deint_method)
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
get_next_deint_method (GstVaapiDeinterlaceMethod deint_method)
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
set_best_deint_method (GstVaapiPostproc * postproc, guint flags,
    GstVaapiDeinterlaceMethod * deint_method_ptr)
{
  GstVaapiDeinterlaceMethod deint_method = postproc->deinterlace_method;
  gboolean success;

  for (;;) {
    success = gst_vaapi_filter_set_deinterlacing (postproc->filter,
        deint_method, flags);
    if (success || deint_method == GST_VAAPI_DEINTERLACE_METHOD_BOB)
      break;
    deint_method = get_next_deint_method (deint_method);
  }
  *deint_method_ptr = deint_method;
  return success;
}

static gboolean
should_hdr_tone_map (GstVaapiPostproc * const postproc, const GstCaps * caps)
{
  switch (postproc->hdr_tone_map) {
    case GST_VAAPI_HDR_TONE_MAP_AUTO:
    {
      GstVideoMasteringDisplayInfo minfo;
      return gst_video_mastering_display_info_from_caps (&minfo, caps);
    }
    case GST_VAAPI_HDR_TONE_MAP_DISABLED:
      return FALSE;
    default:
      GST_ERROR_OBJECT (postproc, "unhandled \"hdr-tone-map\" option");
      break;
  }
  return FALSE;
}

static gboolean
configure_hdr_tone_map (GstVaapiPostproc * const postproc, const GstCaps * caps)
{
  gboolean enable;

  g_return_val_if_fail (postproc->has_vpp, FALSE);

  enable = should_hdr_tone_map (postproc, caps);

  if (!gst_vaapi_filter_set_hdr_tone_map (postproc->filter, enable))
    goto fail_configure_hdr_tone_map;

  if (enable) {
    GstVideoMasteringDisplayInfo minfo;
    GstVideoContentLightLevel linfo;

    gst_video_mastering_display_info_from_caps (&minfo, caps);
    gst_video_content_light_level_from_caps (&linfo, caps);

    if (!gst_vaapi_filter_set_hdr_tone_map_meta (postproc->filter, &minfo,
            &linfo))
      goto fail_configure_hdr_tone_map;

    postproc->flags |= GST_VAAPI_POSTPROC_FLAG_HDR_TONE_MAP;
  } else {
    postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_HDR_TONE_MAP);
  }

  return TRUE;

fail_configure_hdr_tone_map:
  {
    postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_HDR_TONE_MAP);
    return FALSE;
  }
}

static gboolean
check_filter_update (GstVaapiPostproc * postproc)
{
  guint filter_flag = postproc->flags;
  guint op_flag;
  gint i;

  if (!postproc->has_vpp)
    return FALSE;

  for (i = GST_VAAPI_FILTER_OP_DENOISE;
      i <= GST_VAAPI_FILTER_OP_SKINTONE_LEVEL; i++) {
    op_flag = (filter_flag >> i) & 1;
    if (op_flag)
      return TRUE;
  }

  return FALSE;
}

static gboolean
update_filter (GstVaapiPostproc * postproc)
{
  /* Validate filters */
  if ((postproc->flags & GST_VAAPI_POSTPROC_FLAG_FORMAT) &&
      !gst_vaapi_filter_set_format (postproc->filter, postproc->format))
    return FALSE;

  if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_DENOISE) {
    if (!gst_vaapi_filter_set_denoising_level (postproc->filter,
            postproc->denoise_level))
      return FALSE;

    if (gst_vaapi_filter_get_denoising_level_default (postproc->filter) ==
        postproc->denoise_level)
      postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_DENOISE);
  }

  if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_SHARPEN) {
    if (!gst_vaapi_filter_set_sharpening_level (postproc->filter,
            postproc->sharpen_level))
      return FALSE;

    if (gst_vaapi_filter_get_sharpening_level_default (postproc->filter) ==
        postproc->sharpen_level)
      postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_SHARPEN);
  }

  if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_HUE) {
    if (!gst_vaapi_filter_set_hue (postproc->filter, postproc->hue))
      return FALSE;

    if (gst_vaapi_filter_get_hue_default (postproc->filter) == postproc->hue)
      postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_HUE);
  }

  if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_SATURATION) {
    if (!gst_vaapi_filter_set_saturation (postproc->filter,
            postproc->saturation))
      return FALSE;

    if (gst_vaapi_filter_get_saturation_default (postproc->filter) ==
        postproc->saturation)
      postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_SATURATION);
  }

  if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_BRIGHTNESS) {
    if (!gst_vaapi_filter_set_brightness (postproc->filter,
            postproc->brightness))
      return FALSE;

    if (gst_vaapi_filter_get_brightness_default (postproc->filter) ==
        postproc->brightness)
      postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_BRIGHTNESS);
  }

  if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_CONTRAST) {
    if (!gst_vaapi_filter_set_contrast (postproc->filter, postproc->contrast))
      return FALSE;

    if (gst_vaapi_filter_get_contrast_default (postproc->filter) ==
        postproc->contrast)
      postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_CONTRAST);
  }

  if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_SCALE) {
    if (!gst_vaapi_filter_set_scaling (postproc->filter,
            postproc->scale_method))
      return FALSE;

    if (gst_vaapi_filter_get_scaling_default (postproc->filter) ==
        postproc->scale_method)
      postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_SCALE);
  }

  if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_VIDEO_DIRECTION) {
    GstVideoOrientationMethod method = postproc->video_direction;
    if (method == GST_VIDEO_ORIENTATION_AUTO)
      method = postproc->tag_video_direction;

    if (!gst_vaapi_filter_set_video_direction (postproc->filter, method)) {
      GST_ELEMENT_WARNING (postproc, LIBRARY, SETTINGS,
          ("Unsupported video direction '%s' by driver.",
              gst_vaapi_enum_type_get_nick
              (GST_TYPE_VIDEO_ORIENTATION_METHOD, method)),
          ("video direction transformation ignored"));

      /* Don't return FALSE because other filters might be set */
    }

    if (gst_vaapi_filter_get_video_direction_default (postproc->filter) ==
        method)
      postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_VIDEO_DIRECTION);
  }

  if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_CROP)
    if ((postproc->crop_left | postproc->crop_right | postproc->crop_top
            | postproc->crop_bottom) == 0)
      postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_CROP);

  if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_SKINTONE_LEVEL) {
    if (!gst_vaapi_filter_set_skintone_level (postproc->filter,
            postproc->skintone_value))
      return FALSE;

    if (gst_vaapi_filter_get_skintone_level_default (postproc->filter) ==
        postproc->skintone_value)
      postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_SKINTONE_LEVEL);

#ifndef GST_REMOVE_DEPRECATED
    /*
     * When use skin tone level property, disable old skin tone property always
     */
    postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_SKINTONE);
#endif
  } else {
#ifndef GST_REMOVE_DEPRECATED
    if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_SKINTONE) {
      if (!gst_vaapi_filter_set_skintone (postproc->filter,
              postproc->skintone_enhance))
        return FALSE;

      if (gst_vaapi_filter_get_skintone_default (postproc->filter) ==
          postproc->skintone_enhance)
        postproc->flags &= ~(GST_VAAPI_POSTPROC_FLAG_SKINTONE);
    }
#endif
  }

  return TRUE;
}

static void
gst_vaapipostproc_set_passthrough (GstBaseTransform * trans)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  gboolean filter_updated = FALSE;

  if (check_filter_update (postproc) && update_filter (postproc)) {
    /* check again if changed value is default */
    filter_updated = check_filter_update (postproc);
  }

  gst_base_transform_set_passthrough (trans, postproc->same_caps
      && !filter_updated);
}

static gboolean
replace_to_dumb_buffer_if_required (GstVaapiPostproc * postproc,
    GstBuffer ** fieldbuf)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (postproc);
  GstBuffer *newbuf;

  if (!GST_VAAPI_PLUGIN_BASE_COPY_OUTPUT_FRAME (postproc))
    return TRUE;

  newbuf = create_output_dump_buffer (postproc);
  if (!newbuf)
    return FALSE;

  if (!gst_vaapi_plugin_copy_va_buffer (plugin, *fieldbuf, newbuf)) {
    gst_buffer_unref (newbuf);
    return FALSE;
  }

  gst_buffer_replace (fieldbuf, newbuf);
  gst_buffer_unref (newbuf);

  return TRUE;
}

static gboolean
use_vpp_crop (GstVaapiPostproc * postproc)
{
  return !(postproc->forward_crop
      && !(postproc->flags & GST_VAAPI_POSTPROC_FLAG_CROP));
}

static void
rotate_crop_meta (GstVaapiPostproc * const postproc, const GstVideoMeta * vmeta,
    GstVideoCropMeta * crop)
{
  guint tmp;

  g_return_if_fail (postproc->has_vpp);

  /* The video meta is required since the caps width/height are smaller,
   * which would not result in a usable GstVideoInfo for mapping the
   * buffer. */
  if (!vmeta || !crop)
    return;

  switch (gst_vaapi_filter_get_video_direction (postproc->filter)) {
    case GST_VIDEO_ORIENTATION_HORIZ:
      crop->x = vmeta->width - crop->width - crop->x;
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      crop->y = vmeta->height - crop->height - crop->y;
      break;
    case GST_VIDEO_ORIENTATION_90R:
      tmp = crop->x;
      crop->x = vmeta->height - crop->height - crop->y;
      crop->y = tmp;
      G_PRIMITIVE_SWAP (guint, crop->width, crop->height);
      break;
    case GST_VIDEO_ORIENTATION_180:
      crop->x = vmeta->width - crop->width - crop->x;
      crop->y = vmeta->height - crop->height - crop->y;
      break;
    case GST_VIDEO_ORIENTATION_90L:
      tmp = crop->x;
      crop->x = crop->y;
      crop->y = vmeta->width - crop->width - tmp;
      G_PRIMITIVE_SWAP (guint, crop->width, crop->height);
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      tmp = crop->x;
      crop->x = vmeta->height - crop->height - crop->y;
      crop->y = vmeta->width - crop->width - tmp;
      G_PRIMITIVE_SWAP (guint, crop->width, crop->height);
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      G_PRIMITIVE_SWAP (guint, crop->x, crop->y);
      G_PRIMITIVE_SWAP (guint, crop->width, crop->height);
      break;
    default:
      break;
  }
}

static GstFlowReturn
gst_vaapipostproc_process_vpp (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  GstVaapiDeinterlaceState *const ds = &postproc->deinterlace_state;
  GstVaapiVideoMeta *inbuf_meta, *outbuf_meta;
  GstVaapiSurface *inbuf_surface, *outbuf_surface;
  GstVaapiSurfaceProxy *proxy;
  GstVaapiFilterStatus status;
  GstClockTime timestamp;
  GstFlowReturn ret;
  GstBuffer *fieldbuf;
  GstVaapiDeinterlaceMethod deint_method;
  guint flags, deint_flags;
  gboolean tff, deint, deint_refs, deint_changed, discont;
  const GstVideoCropMeta *crop_meta;
  GstVaapiRectangle *crop_rect = NULL;
  GstVaapiRectangle tmp_rect;

  inbuf_meta = gst_buffer_get_vaapi_video_meta (inbuf);
  if (!inbuf_meta)
    goto error_invalid_buffer;
  inbuf_surface = gst_vaapi_video_meta_get_surface (inbuf_meta);

  if (use_vpp_crop (postproc)) {
    crop_rect = &tmp_rect;
    crop_rect->x = postproc->crop_left;
    crop_rect->y = postproc->crop_top;
    crop_rect->width = GST_VIDEO_INFO_WIDTH (&postproc->sinkpad_info)
        - (postproc->crop_left + postproc->crop_right);
    crop_rect->height = GST_VIDEO_INFO_HEIGHT (&postproc->sinkpad_info)
        - (postproc->crop_top + postproc->crop_bottom);

    crop_meta = gst_buffer_get_video_crop_meta (inbuf);
    if (crop_meta) {
      crop_rect->x += crop_meta->x;
      crop_rect->y += crop_meta->y;
    }
  }

  if (!crop_rect)
    crop_rect = (GstVaapiRectangle *)
        gst_vaapi_video_meta_get_render_rect (inbuf_meta);

  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  tff = GST_BUFFER_FLAG_IS_SET (inbuf, GST_VIDEO_BUFFER_FLAG_TFF);
  discont = GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_DISCONT);
  deint = should_deinterlace_buffer (postproc, inbuf);

  /* Drop references if deinterlacing conditions changed */
  deint_changed = deint != ds->deint;
  if (deint_changed || (ds->num_surfaces > 0 && tff != ds->tff))
    ds_reset (ds);

  deint_method = postproc->deinterlace_method;
  deint_refs = deint_method_is_advanced (deint_method);
  if (deint_refs && 0) {
    GstBuffer *const prev_buf = ds_get_buffer (ds, 0);
    GstClockTime prev_pts, pts = GST_BUFFER_TIMESTAMP (inbuf);
    /* Reset deinterlacing state when there is a discontinuity */
    if (prev_buf && (prev_pts = GST_BUFFER_TIMESTAMP (prev_buf)) != pts) {
      const GstClockTimeDiff pts_diff = GST_CLOCK_DIFF (prev_pts, pts);
      if (pts_diff < 0 || (postproc->field_duration > 0 &&
              pts_diff >= postproc->field_duration * 3 - 1))
        ds_reset (ds);
    }
  }

  ds->deint = deint;
  ds->tff = tff;

  flags = gst_vaapi_video_meta_get_render_flags (inbuf_meta) &
      ~GST_VAAPI_PICTURE_STRUCTURE_MASK;

  /* First field */
  if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_DEINTERLACE) {
    fieldbuf = create_output_buffer (postproc);
    if (!fieldbuf)
      goto error_create_buffer;

    outbuf_meta = gst_buffer_get_vaapi_video_meta (fieldbuf);
    if (!outbuf_meta)
      goto error_create_meta;

    if (!gst_vaapi_video_meta_get_surface_proxy (outbuf_meta)) {
      proxy =
          gst_vaapi_surface_proxy_new_from_pool (GST_VAAPI_SURFACE_POOL
          (postproc->filter_pool));
      if (!proxy)
        goto error_create_proxy;
      gst_vaapi_video_meta_set_surface_proxy (outbuf_meta, proxy);
      gst_vaapi_surface_proxy_unref (proxy);
    }

    if (deint) {
      deint_flags = (tff ? GST_VAAPI_DEINTERLACE_FLAG_TOPFIELD : 0);
      if (tff)
        deint_flags |= GST_VAAPI_DEINTERLACE_FLAG_TFF;
      if (!set_best_deint_method (postproc, deint_flags, &deint_method))
        goto error_op_deinterlace;

      if (deint_method != postproc->deinterlace_method) {
        GST_DEBUG ("unsupported deinterlace-method %u. Using %u instead",
            postproc->deinterlace_method, deint_method);
        postproc->deinterlace_method = deint_method;
        deint_refs = deint_method_is_advanced (deint_method);
      }

      if (deint_refs) {
        ds_set_surfaces (ds);
        if (!gst_vaapi_filter_set_deinterlacing_references (postproc->filter,
                ds->surfaces, ds->num_surfaces, NULL, 0))
          goto error_op_deinterlace;
      }
    } else if (deint_changed) {
      // Reset internal filter to non-deinterlacing mode
      deint_method = GST_VAAPI_DEINTERLACE_METHOD_NONE;
      if (!gst_vaapi_filter_set_deinterlacing (postproc->filter,
              deint_method, 0))
        goto error_op_deinterlace;
    }

    outbuf_surface = gst_vaapi_video_meta_get_surface (outbuf_meta);
    gst_vaapi_filter_set_cropping_rectangle (postproc->filter, crop_rect);
    status = gst_vaapi_filter_process (postproc->filter, inbuf_surface,
        outbuf_surface, flags);
    if (status != GST_VAAPI_FILTER_STATUS_SUCCESS)
      goto error_process_vpp;

    copy_metadata (postproc, fieldbuf, inbuf);
    GST_BUFFER_TIMESTAMP (fieldbuf) = timestamp;
    GST_BUFFER_DURATION (fieldbuf) = postproc->field_duration;
    if (discont) {
      GST_BUFFER_FLAG_SET (fieldbuf, GST_BUFFER_FLAG_DISCONT);
      discont = FALSE;
    }

    if (!replace_to_dumb_buffer_if_required (postproc, &fieldbuf))
      goto error_copy_buffer;

    ret = gst_pad_push (trans->srcpad, fieldbuf);
    if (ret != GST_FLOW_OK)
      goto error_push_buffer;
  }
  fieldbuf = NULL;

  /* Second field */
  outbuf_meta = gst_buffer_get_vaapi_video_meta (outbuf);
  if (!outbuf_meta)
    goto error_create_meta;

  if (!gst_vaapi_video_meta_get_surface_proxy (outbuf_meta)) {
    proxy =
        gst_vaapi_surface_proxy_new_from_pool (GST_VAAPI_SURFACE_POOL
        (postproc->filter_pool));
    if (!proxy)
      goto error_create_proxy;
    gst_vaapi_video_meta_set_surface_proxy (outbuf_meta, proxy);
    gst_vaapi_surface_proxy_unref (proxy);
  }

  if (deint) {
    deint_flags = (tff ? 0 : GST_VAAPI_DEINTERLACE_FLAG_TOPFIELD);
    if (tff)
      deint_flags |= GST_VAAPI_DEINTERLACE_FLAG_TFF;
    if (!gst_vaapi_filter_set_deinterlacing (postproc->filter,
            deint_method, deint_flags))
      goto error_op_deinterlace;

    if (deint_refs
        && !gst_vaapi_filter_set_deinterlacing_references (postproc->filter,
            ds->surfaces, ds->num_surfaces, NULL, 0))
      goto error_op_deinterlace;
  } else if (deint_changed
      && !gst_vaapi_filter_set_deinterlacing (postproc->filter, deint_method,
          0))
    goto error_op_deinterlace;

  outbuf_surface = gst_vaapi_video_meta_get_surface (outbuf_meta);
  gst_vaapi_filter_set_cropping_rectangle (postproc->filter, crop_rect);
  status = gst_vaapi_filter_process (postproc->filter, inbuf_surface,
      outbuf_surface, flags);
  if (status != GST_VAAPI_FILTER_STATUS_SUCCESS)
    goto error_process_vpp;

  if (!(postproc->flags & GST_VAAPI_POSTPROC_FLAG_DEINTERLACE))
    gst_buffer_copy_into (outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
  else {
    GST_BUFFER_TIMESTAMP (outbuf) = timestamp + postproc->field_duration;
    GST_BUFFER_DURATION (outbuf) = postproc->field_duration;
    if (discont) {
      GST_BUFFER_FLAG_SET (fieldbuf, GST_BUFFER_FLAG_DISCONT);
      discont = FALSE;
    }
  }

  copy_metadata (postproc, outbuf, inbuf);

  rotate_crop_meta (postproc, gst_buffer_get_video_meta (inbuf),
      gst_buffer_get_video_crop_meta (outbuf));

  if (deint && deint_refs)
    ds_add_buffer (ds, inbuf);
  postproc->use_vpp = TRUE;
  return GST_FLOW_OK;

  /* ERRORS */
error_invalid_buffer:
  {
    GST_ERROR_OBJECT (postproc, "failed to validate source buffer");
    return GST_FLOW_ERROR;
  }
error_create_buffer:
  {
    GST_ERROR_OBJECT (postproc, "failed to create output buffer");
    return GST_FLOW_ERROR;
  }
error_create_meta:
  {
    GST_ERROR_OBJECT (postproc, "failed to create new output buffer meta");
    gst_buffer_replace (&fieldbuf, NULL);
    return GST_FLOW_ERROR;
  }
error_create_proxy:
  {
    GST_ERROR_OBJECT (postproc, "failed to create surface proxy from pool");
    gst_buffer_replace (&fieldbuf, NULL);
    return GST_FLOW_ERROR;
  }
error_op_deinterlace:
  {
    GST_ERROR_OBJECT (postproc, "failed to apply deinterlacing filter");
    gst_buffer_replace (&fieldbuf, NULL);
    return GST_FLOW_NOT_SUPPORTED;
  }
error_process_vpp:
  {
    GST_ERROR_OBJECT (postproc, "failed to apply VPP filters (error %d)",
        status);
    gst_buffer_replace (&fieldbuf, NULL);
    return GST_FLOW_ERROR;
  }
error_copy_buffer:
  {
    GST_ERROR_OBJECT (postproc, "failed to copy field buffer to dumb buffer");
    gst_buffer_replace (&fieldbuf, NULL);
    return GST_FLOW_ERROR;
  }
error_push_buffer:
  {
    GST_DEBUG_OBJECT (postproc, "failed to push output buffer: %s",
        gst_flow_get_name (ret));
    return ret;
  }
}

static GstFlowReturn
gst_vaapipostproc_process (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  GstVaapiVideoMeta *meta;
  GstClockTime timestamp;
  GstFlowReturn ret;
  GstBuffer *fieldbuf;
  guint fieldbuf_flags, outbuf_flags, flags;
  gboolean tff, deint;

  meta = gst_buffer_get_vaapi_video_meta (inbuf);
  if (!meta)
    goto error_invalid_buffer;

  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  tff = GST_BUFFER_FLAG_IS_SET (inbuf, GST_VIDEO_BUFFER_FLAG_TFF);
  deint = should_deinterlace_buffer (postproc, inbuf);

  flags = gst_vaapi_video_meta_get_render_flags (meta) &
      ~GST_VAAPI_PICTURE_STRUCTURE_MASK;

  /* First field */
  fieldbuf = create_output_buffer (postproc);
  if (!fieldbuf)
    goto error_create_buffer;
  append_output_buffer_metadata (postproc, fieldbuf, inbuf, 0);

  meta = gst_buffer_get_vaapi_video_meta (fieldbuf);
  fieldbuf_flags = flags;
  fieldbuf_flags |= deint ? (tff ?
      GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD :
      GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD) :
      GST_VAAPI_PICTURE_STRUCTURE_FRAME;
  gst_vaapi_video_meta_set_render_flags (meta, fieldbuf_flags);

  GST_BUFFER_TIMESTAMP (fieldbuf) = timestamp;
  GST_BUFFER_DURATION (fieldbuf) = postproc->field_duration;

  if (!replace_to_dumb_buffer_if_required (postproc, &fieldbuf))
    goto error_copy_buffer;

  ret = gst_pad_push (trans->srcpad, fieldbuf);
  if (ret != GST_FLOW_OK)
    goto error_push_buffer;

  /* Second field */
  append_output_buffer_metadata (postproc, outbuf, inbuf, 0);

  meta = gst_buffer_get_vaapi_video_meta (outbuf);
  outbuf_flags = flags;
  outbuf_flags |= deint ? (tff ?
      GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD :
      GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD) :
      GST_VAAPI_PICTURE_STRUCTURE_FRAME;
  gst_vaapi_video_meta_set_render_flags (meta, outbuf_flags);

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp + postproc->field_duration;
  GST_BUFFER_DURATION (outbuf) = postproc->field_duration;
  return GST_FLOW_OK;

  /* ERRORS */
error_invalid_buffer:
  {
    GST_ERROR_OBJECT (postproc, "failed to validate source buffer");
    return GST_FLOW_ERROR;
  }
error_create_buffer:
  {
    GST_ERROR_OBJECT (postproc, "failed to create output buffer");
    return GST_FLOW_EOS;
  }
error_copy_buffer:
  {
    GST_ERROR_OBJECT (postproc, "failed to copy field buffer to dumb buffer");
    gst_buffer_replace (&fieldbuf, NULL);
    return GST_FLOW_ERROR;
  }
error_push_buffer:
  {
    GST_DEBUG_OBJECT (postproc, "failed to push output buffer: %s",
        gst_flow_get_name (ret));
    return ret;
  }
}

static GstFlowReturn
gst_vaapipostproc_passthrough (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  GstVaapiVideoMeta *meta;

  /* No video processing needed, simply copy buffer metadata */
  meta = gst_buffer_get_vaapi_video_meta (inbuf);
  if (!meta)
    goto error_invalid_buffer;

  append_output_buffer_metadata (postproc, outbuf, inbuf,
      GST_BUFFER_COPY_TIMESTAMPS);
  return GST_FLOW_OK;

  /* ERRORS */
error_invalid_buffer:
  {
    GST_ERROR_OBJECT (postproc, "failed to validate source buffer");
    return GST_FLOW_ERROR;
  }
}

static gboolean
video_info_changed (GstVideoInfo * old_vip, GstVideoInfo * new_vip)
{
  if (gst_video_info_changed (old_vip, new_vip))
    return TRUE;
  if (GST_VIDEO_INFO_INTERLACE_MODE (old_vip) !=
      GST_VIDEO_INFO_INTERLACE_MODE (new_vip))
    return TRUE;
  return FALSE;
}

static gboolean
video_info_update (GstCaps * caps, GstVideoInfo * info,
    gboolean * caps_changed_ptr)
{
  GstVideoInfo vi;

  if (!gst_video_info_from_caps (&vi, caps))
    return FALSE;

  *caps_changed_ptr = FALSE;
  if (video_info_changed (info, &vi)) {
    *caps_changed_ptr = TRUE;
    *info = vi;
  }

  return TRUE;
}

static gboolean
gst_vaapipostproc_update_sink_caps (GstVaapiPostproc * postproc, GstCaps * caps,
    gboolean * caps_changed_ptr)
{
  GstVideoInfo vi;
  gboolean deinterlace;

  GST_INFO_OBJECT (postproc, "new sink caps = %" GST_PTR_FORMAT, caps);

  if (!video_info_update (caps, &postproc->sinkpad_info, caps_changed_ptr))
    return FALSE;

  vi = postproc->sinkpad_info;
  deinterlace = is_deinterlace_enabled (postproc, &vi);
  if (deinterlace)
    postproc->flags |= GST_VAAPI_POSTPROC_FLAG_DEINTERLACE;
  postproc->field_duration = GST_VIDEO_INFO_FPS_N (&vi) > 0 ?
      gst_util_uint64_scale (GST_SECOND, GST_VIDEO_INFO_FPS_D (&vi),
      (1 + deinterlace) * GST_VIDEO_INFO_FPS_N (&vi)) : 0;

  postproc->get_va_surfaces = gst_caps_has_vaapi_surface (caps);
  return TRUE;
}

static gboolean
gst_vaapipostproc_update_src_caps (GstVaapiPostproc * postproc, GstCaps * caps,
    gboolean * caps_changed_ptr)
{
  GST_INFO_OBJECT (postproc, "new src caps = %" GST_PTR_FORMAT, caps);

  if (!video_info_update (caps, &postproc->srcpad_info, caps_changed_ptr))
    return FALSE;

  if (postproc->format != GST_VIDEO_INFO_FORMAT (&postproc->sinkpad_info) &&
      postproc->format != DEFAULT_FORMAT)
    postproc->flags |= GST_VAAPI_POSTPROC_FLAG_FORMAT;

  if (GST_VIDEO_INFO_WIDTH (&postproc->srcpad_info) !=
      GST_VIDEO_INFO_WIDTH (&postproc->sinkpad_info)
      || GST_VIDEO_INFO_HEIGHT (&postproc->srcpad_info) !=
      GST_VIDEO_INFO_HEIGHT (&postproc->sinkpad_info))
    postproc->flags |= GST_VAAPI_POSTPROC_FLAG_SIZE;

  return TRUE;
}

static gboolean
ensure_allowed_sinkpad_caps (GstVaapiPostproc * postproc)
{
  GstCaps *out_caps = NULL;
  guint mem_types;
  gint min_width, min_height, max_width, max_height;
  GArray *mem_formats = NULL;
  gboolean ret = TRUE;
  guint i, num_structure;

  if (postproc->allowed_sinkpad_caps) {
    ret = TRUE;
    goto out;
  }

  if (!GST_VAAPI_PLUGIN_BASE_DISPLAY (postproc) ||
      !gst_vaapipostproc_ensure_filter_caps (postproc)) {
    ret = FALSE;
    goto out;
  }

  mem_types = gst_vaapi_filter_get_memory_types (postproc->filter);
  mem_formats = gst_vaapi_filter_get_formats (postproc->filter, &min_width,
      &min_height, &max_width, &max_height);

  out_caps = gst_vaapi_build_caps_from_formats (mem_formats, min_width,
      min_height, max_width, max_height, mem_types);
  if (!out_caps) {
    GST_WARNING_OBJECT (postproc, "failed to create VA sink caps");
    ret = FALSE;
    goto out;
  }

  /* For raw yuv caps, we need to replace va attrib formats with all image formats */
  num_structure = gst_caps_get_size (out_caps);
  for (i = 0; i < num_structure; i++) {
    GstStructure *structure;
    GstCapsFeatures *features = gst_caps_get_features (out_caps, i);
    GValue v_formats = G_VALUE_INIT;

    structure = gst_caps_get_structure (out_caps, i);
    if (!structure)
      continue;

    if (gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY)) {
      mem_formats = gst_vaapi_display_get_image_formats
          (GST_VAAPI_PLUGIN_BASE_DISPLAY (postproc));
      if (!gst_vaapi_value_set_format_list (&v_formats, mem_formats)) {
        ret = FALSE;
        goto out;
      }
      gst_structure_set_value (structure, "format", &v_formats);
      g_value_unset (&v_formats);
    }
  }

  postproc->allowed_sinkpad_caps = out_caps;
  out_caps = NULL;
  GST_INFO_OBJECT (postproc, "postproc sink allowed caps is %" GST_PTR_FORMAT,
      postproc->allowed_sinkpad_caps);
out:
  if (out_caps)
    gst_caps_unref (out_caps);
  if (mem_formats)
    g_array_unref (mem_formats);

  return ret;
}

/* Fixup output caps so that to reflect the supported set of pixel formats */
static GstCaps *
expand_allowed_srcpad_caps (GstVaapiPostproc * postproc, GstCaps * caps)
{
  GValue value = G_VALUE_INIT, v_format = G_VALUE_INIT;
  guint i, num_structures;
  gint gl_upload_meta_idx = -1;

  if (postproc->filter == NULL)
    goto cleanup;
  if (!gst_vaapipostproc_ensure_filter_caps (postproc))
    goto cleanup;

  /* Reset "format" field for each structure */
  if (!gst_vaapi_value_set_format_list (&value, postproc->filter_formats))
    goto cleanup;
  if (gst_vaapi_value_set_format (&v_format, GST_VIDEO_FORMAT_ENCODED)) {
    gst_value_list_prepend_value (&value, &v_format);
    g_value_unset (&v_format);
  }

  num_structures = gst_caps_get_size (caps);
  for (i = 0; i < num_structures; i++) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, i);
    GstStructure *structure;

    structure = gst_caps_get_structure (caps, i);
    if (!structure)
      continue;

    gst_vaapi_filter_append_caps (postproc->filter, structure);

    if (gst_caps_features_contains (features,
            GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META)) {
      gl_upload_meta_idx = i;
      continue;
    }

    gst_structure_set_value (structure, "format", &value);
  }
  g_value_unset (&value);

  if ((GST_VAAPI_PLUGIN_BASE_SRC_PAD_CAN_DMABUF (postproc)
          || !gst_vaapi_display_has_opengl (GST_VAAPI_PLUGIN_BASE_DISPLAY
              (postproc)))
      && gl_upload_meta_idx > -1) {
    gst_caps_remove_structure (caps, gl_upload_meta_idx);
  }

cleanup:
  return caps;
}

static gboolean
ensure_allowed_srcpad_caps (GstVaapiPostproc * postproc)
{
  GstCaps *out_caps;

  if (postproc->allowed_srcpad_caps)
    return TRUE;

  /* Create initial caps from pad template */
  out_caps = gst_caps_from_string (gst_vaapipostproc_src_caps_str);
  if (!out_caps) {
    GST_ERROR_OBJECT (postproc, "failed to create VA src caps");
    return FALSE;
  }

  postproc->allowed_srcpad_caps =
      expand_allowed_srcpad_caps (postproc, out_caps);
  return postproc->allowed_srcpad_caps != NULL;
}

static GstCaps *
gst_vaapipostproc_transform_caps_impl (GstBaseTransform * trans,
    GstPadDirection direction)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);

  /* Generate the sink pad caps, that could be fixated afterwards */
  if (direction == GST_PAD_SRC) {
    if (!ensure_allowed_sinkpad_caps (postproc))
      return gst_caps_from_string (gst_vaapipostproc_sink_caps_str);
    return gst_caps_ref (postproc->allowed_sinkpad_caps);
  }

  /* Generate complete set of src pad caps */
  if (!ensure_allowed_srcpad_caps (postproc))
    return NULL;
  return gst_vaapipostproc_transform_srccaps (postproc);
}

static GstCaps *
gst_vaapipostproc_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  GstCaps *out_caps;

  GST_DEBUG_OBJECT (trans,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  g_mutex_lock (&postproc->postproc_lock);
  out_caps = gst_vaapipostproc_transform_caps_impl (trans, direction);
  g_mutex_unlock (&postproc->postproc_lock);

  if (out_caps && filter) {
    GstCaps *intersection;

    intersection = gst_caps_intersect_full (out_caps, filter,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (out_caps);
    out_caps = intersection;
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, out_caps);

  return out_caps;
}

static GstCaps *
gst_vaapipostproc_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  GstCaps *outcaps = NULL;
  gboolean same_caps, filter_updated = FALSE;

  GST_DEBUG_OBJECT (trans, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT " in direction %s", othercaps, caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SRC) {
    /* @TODO: we can do better */
    outcaps = gst_caps_fixate (othercaps);
    goto done;
  }

  g_mutex_lock (&postproc->postproc_lock);
  postproc->has_vpp = gst_vaapipostproc_ensure_filter_caps (postproc);
  if (check_filter_update (postproc) && update_filter (postproc)) {
    /* check again if changed value is default */
    filter_updated = check_filter_update (postproc);
  }

  outcaps = gst_vaapipostproc_fixate_srccaps (postproc, caps, othercaps);
  g_mutex_unlock (&postproc->postproc_lock);
  if (!outcaps)
    goto done;

  /* set passthrough according to caps changes or filter changes */
  same_caps = gst_caps_is_equal (caps, outcaps);
  gst_base_transform_set_passthrough (trans, same_caps && !filter_updated);

done:
  if (outcaps)
    GST_DEBUG_OBJECT (trans, "fixated othercaps to %" GST_PTR_FORMAT, outcaps);
  gst_caps_unref (othercaps);

  return outcaps;
}

static gboolean
gst_vaapipostproc_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, gsize size,
    GstCaps * othercaps, gsize * othersize)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);

  if (direction == GST_PAD_SINK || postproc->get_va_surfaces)
    *othersize = 0;
  else
    *othersize = size;
  return TRUE;
}

static gboolean
gst_vaapipostproc_transform_meta (GstBaseTransform * trans, GstBuffer * outbuf,
    GstMeta * meta, GstBuffer * inbuf)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);

  /* don't copy GstVideoCropMeta if we are using vpp crop */
  if (meta->info->api == GST_VIDEO_CROP_META_API_TYPE
      && use_vpp_crop (postproc))
    return FALSE;

  /* don't copy GstParentBufferMeta if use_vpp */
  if (meta->info->api == GST_PARENT_BUFFER_META_API_TYPE && postproc->use_vpp)
    return FALSE;

  return TRUE;
}

static GstFlowReturn
gst_vaapipostproc_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (postproc);
  GstBuffer *buf, *sys_buf = NULL;
  GstFlowReturn ret;

  ret = gst_vaapi_plugin_base_get_input_buffer (plugin, inbuf, &buf);
  if (ret != GST_FLOW_OK)
    return GST_FLOW_ERROR;

  if (GST_VAAPI_PLUGIN_BASE_COPY_OUTPUT_FRAME (trans)) {
    GstBuffer *va_buf = create_output_buffer (postproc);
    if (!va_buf) {
      ret = GST_FLOW_ERROR;
      goto done;
    }
    sys_buf = outbuf;
    outbuf = va_buf;
  }

  ret = GST_FLOW_NOT_SUPPORTED;
  if (postproc->flags) {
    /* Use VA/VPP extensions to process this frame */
    if (postproc->has_vpp) {
      ret = gst_vaapipostproc_process_vpp (trans, buf, outbuf);
      if (ret != GST_FLOW_NOT_SUPPORTED)
        goto done;
      GST_WARNING_OBJECT (postproc, "unsupported VPP filters. Disabling");
    }

    /* Only append picture structure meta data (top/bottom field) */
    if (postproc->flags & GST_VAAPI_POSTPROC_FLAG_DEINTERLACE) {
      ret = gst_vaapipostproc_process (trans, buf, outbuf);
      if (ret != GST_FLOW_NOT_SUPPORTED)
        goto done;
    }
  }

  /* Fallback: passthrough to the downstream element as is */
  ret = gst_vaapipostproc_passthrough (trans, buf, outbuf);

done:
  if (sys_buf)
    copy_metadata (postproc, sys_buf, buf);

  gst_buffer_unref (buf);

  if (sys_buf) {
    if (!gst_vaapi_plugin_copy_va_buffer (plugin, outbuf, sys_buf))
      return GST_FLOW_ERROR;

    gst_buffer_unref (outbuf);
    outbuf = sys_buf;
  }

  return ret;
}

static gboolean
ensure_buffer_pool (GstVaapiPostproc * postproc, GstVideoInfo * vi)
{
  GstVaapiVideoPool *pool;

  if (!vi)
    return FALSE;

  gst_video_info_change_format (vi, postproc->format,
      GST_VIDEO_INFO_WIDTH (vi), GST_VIDEO_INFO_HEIGHT (vi));

  if (postproc->filter_pool
      && !video_info_changed (&postproc->filter_pool_info, vi))
    return TRUE;
  postproc->filter_pool_info = *vi;

  pool =
      gst_vaapi_surface_pool_new_full (GST_VAAPI_PLUGIN_BASE_DISPLAY (postproc),
      &postproc->filter_pool_info, 0);
  if (!pool)
    return FALSE;

  gst_vaapi_video_pool_replace (&postproc->filter_pool, pool);
  gst_vaapi_video_pool_unref (pool);
  return TRUE;
}

static GstFlowReturn
gst_vaapipostproc_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf_ptr)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  const GstVideoMeta *video_meta;
  GstVideoInfo info;

  if (gst_base_transform_is_passthrough (trans)) {
    *outbuf_ptr = inbuf;
    return GST_FLOW_OK;
  }

  /* If we are not using vpp crop (i.e. forwarding crop meta to downstream)
   * then, ensure our output buffer pool is sized and rotated for uncropped
   * output */
  if (gst_buffer_get_video_crop_meta (inbuf) && !use_vpp_crop (postproc)) {
    /* The video meta is required since the caps width/height are smaller,
     * which would not result in a usable GstVideoInfo for mapping the
     * buffer. */
    video_meta = gst_buffer_get_video_meta (inbuf);
    if (!video_meta)
      return GST_FLOW_ERROR;

    info = postproc->srcpad_info;
    info.width = video_meta->width;
    info.height = video_meta->height;

    if (postproc->has_vpp) {
      /* compensate for rotation if needed */
      switch (gst_vaapi_filter_get_video_direction (postproc->filter)) {
        case GST_VIDEO_ORIENTATION_90R:
        case GST_VIDEO_ORIENTATION_UL_LR:
        case GST_VIDEO_ORIENTATION_90L:
        case GST_VIDEO_ORIENTATION_UR_LL:
          G_PRIMITIVE_SWAP (guint, info.width, info.height);
        default:
          break;
      }
    }

    ensure_buffer_pool (postproc, &info);
  }

  if (GST_VAAPI_PLUGIN_BASE_COPY_OUTPUT_FRAME (trans)) {
    *outbuf_ptr = create_output_dump_buffer (postproc);
  } else {
    *outbuf_ptr = create_output_buffer (postproc);
  }

  if (!*outbuf_ptr)
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static gboolean
ensure_srcpad_buffer_pool (GstVaapiPostproc * postproc, GstCaps * caps)
{
  GstVideoInfo vi;

  if (!gst_video_info_from_caps (&vi, caps))
    return FALSE;

  return ensure_buffer_pool (postproc, &vi);
}

static gboolean
is_native_video_format (GstVideoFormat format)
{
  guint i = 0;
  for (i = 0; i < G_N_ELEMENTS (native_formats); i++)
    if (native_formats[i] == format)
      return TRUE;
  return FALSE;
}

static gboolean
gst_vaapipostproc_set_caps (GstBaseTransform * trans, GstCaps * caps,
    GstCaps * out_caps)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  gboolean sink_caps_changed = FALSE;
  gboolean src_caps_changed = FALSE;
  GstVideoInfo vinfo;
  gboolean ret = FALSE;

  g_mutex_lock (&postproc->postproc_lock);
  if (!gst_vaapipostproc_update_sink_caps (postproc, caps, &sink_caps_changed))
    goto done;
  /* HACK: This is a workaround to deal with the va-intel-driver for non-native
   * formats while doing advanced deinterlacing. The format of reference surfaces must
   * be same as the format used by the driver internally for motion adaptive
   * deinterlacing and motion compensated deinterlacing */
  if (!gst_video_info_from_caps (&vinfo, caps))
    goto done;
  if (deint_method_is_advanced (postproc->deinterlace_method)
      && !is_native_video_format (GST_VIDEO_INFO_FORMAT (&vinfo))) {
    GST_WARNING_OBJECT (postproc,
        "Advanced deinterlacing requires the native video formats used by the driver internally");
    goto done;
  }
  if (!gst_vaapipostproc_update_src_caps (postproc, out_caps,
          &src_caps_changed))
    goto done;

  if (sink_caps_changed || src_caps_changed) {
    gst_vaapipostproc_destroy (postproc);
    if (!gst_vaapipostproc_create (postproc))
      goto done;
    if (!gst_vaapi_plugin_base_set_caps (GST_VAAPI_PLUGIN_BASE (trans),
            caps, out_caps))
      goto done;
  }

  if (postproc->has_vpp) {
    if (!gst_vaapi_filter_set_colorimetry (postproc->filter,
            &GST_VIDEO_INFO_COLORIMETRY (GST_VAAPI_PLUGIN_BASE_SINK_PAD_INFO
                (postproc)),
            &GST_VIDEO_INFO_COLORIMETRY (GST_VAAPI_PLUGIN_BASE_SRC_PAD_INFO
                (postproc))))
      goto done;

    if (!configure_hdr_tone_map (postproc,
            GST_VAAPI_PLUGIN_BASE_SINK_PAD_CAPS (postproc)))
      GST_WARNING_OBJECT (postproc,
          "Failed to configure HDR tone mapping."
          "  The driver may not support it.");
  }

  if (!ensure_srcpad_buffer_pool (postproc, out_caps))
    goto done;

  postproc->same_caps = gst_caps_is_equal (caps, out_caps);

  if (!src_caps_changed) {
    /* set passthrough according to caps changes or filter changes */
    gst_vaapipostproc_set_passthrough (trans);
  }

  ret = TRUE;

done:
  g_mutex_unlock (&postproc->postproc_lock);

  /* Updates the srcpad caps and send the caps downstream */
  if (ret && src_caps_changed)
    gst_base_transform_update_src_caps (trans, out_caps);

  return ret;
}

static gboolean
gst_vaapipostproc_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  GstElement *const element = GST_ELEMENT (trans);

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT) {
    if (gst_vaapi_handle_context_query (element, query)) {
      GST_DEBUG_OBJECT (postproc, "sharing display %" GST_PTR_FORMAT,
          GST_VAAPI_PLUGIN_BASE_DISPLAY (postproc));
      return TRUE;
    }
  }

  return
      GST_BASE_TRANSFORM_CLASS (gst_vaapipostproc_parent_class)->query (trans,
      direction, query);
}

static gboolean
gst_vaapipostproc_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (trans);
  GstCaps *allocation_caps;
  GstStructure *structure;
  gint allocation_width, allocation_height;
  gint negotiated_width, negotiated_height;

  /* passthrough query, we just bypass to the peer */
  if (decide_query == NULL) {
    return GST_BASE_TRANSFORM_CLASS
        (gst_vaapipostproc_parent_class)->propose_allocation (trans,
        decide_query, query);
  }

  /* advertise to upstream that we can handle crop meta */
  if (decide_query)
    gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

  negotiated_width = GST_VIDEO_INFO_WIDTH (&postproc->sinkpad_info);
  negotiated_height = GST_VIDEO_INFO_HEIGHT (&postproc->sinkpad_info);

  if (negotiated_width == 0 || negotiated_height == 0)
    goto bail;

  allocation_caps = NULL;
  gst_query_parse_allocation (query, &allocation_caps, NULL);
  if (!allocation_caps)
    goto bail;

  structure = gst_caps_get_structure (allocation_caps, 0);
  if (!gst_structure_get_int (structure, "width", &allocation_width))
    goto bail;
  if (!gst_structure_get_int (structure, "height", &allocation_height))
    goto bail;

  if (allocation_width != negotiated_width
      || allocation_height != negotiated_height) {
    g_mutex_lock (&postproc->postproc_lock);
    postproc->flags |= GST_VAAPI_POSTPROC_FLAG_SIZE;
    g_mutex_unlock (&postproc->postproc_lock);
  }

bail:
  /* Let vaapidecode allocate the video buffers */
  if (postproc->get_va_surfaces)
    return FALSE;
  if (!gst_vaapi_plugin_base_propose_allocation (plugin, query))
    return FALSE;
  return TRUE;
}

static gboolean
gst_vaapipostproc_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);

  g_mutex_lock (&postproc->postproc_lock);
  /* Let downstream handle the crop meta if they support it */
  postproc->forward_crop = (gst_query_find_allocation_meta (query,
          GST_VIDEO_CROP_META_API_TYPE, NULL) &&
      gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL));
  GST_DEBUG_OBJECT (postproc, "use_vpp_crop=%d", use_vpp_crop (postproc));
  g_mutex_unlock (&postproc->postproc_lock);

  return gst_vaapi_plugin_base_decide_allocation (GST_VAAPI_PLUGIN_BASE (trans),
      query);
}

static void
get_scale_factor (GstVaapiPostproc * const postproc, gdouble * w_factor,
    gdouble * h_factor)
{
  gdouble wd = GST_VIDEO_INFO_WIDTH (&postproc->srcpad_info);
  gdouble hd = GST_VIDEO_INFO_HEIGHT (&postproc->srcpad_info);

  g_return_if_fail (postproc->has_vpp);

  switch (gst_vaapi_filter_get_video_direction (postproc->filter)) {
    case GST_VIDEO_ORIENTATION_90R:
    case GST_VIDEO_ORIENTATION_90L:
    case GST_VIDEO_ORIENTATION_UR_LL:
    case GST_VIDEO_ORIENTATION_UL_LR:
      G_PRIMITIVE_SWAP (gdouble, wd, hd);
      break;
    default:
      break;
  }

  *w_factor = GST_VIDEO_INFO_WIDTH (&postproc->sinkpad_info)
      - (postproc->crop_left + postproc->crop_right);
  *w_factor /= wd;

  *h_factor = GST_VIDEO_INFO_HEIGHT (&postproc->sinkpad_info)
      - (postproc->crop_top + postproc->crop_bottom);
  *h_factor /= hd;
}

static gboolean
gst_vaapipostproc_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  gdouble new_x = 0, new_y = 0, x = 0, y = 0, w_factor = 1, h_factor = 1;
  gboolean ret;

  GST_TRACE_OBJECT (postproc, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event = gst_event_make_writable (event);

      if (postproc->has_vpp &&
          gst_navigation_event_get_coordinates (event, &x, &y)) {
        GST_DEBUG_OBJECT (postproc, "converting %fx%f", x, y);

        /* video-direction compensation */
        switch (gst_vaapi_filter_get_video_direction (postproc->filter)) {
          case GST_VIDEO_ORIENTATION_90R:
            new_x = y;
            new_y = GST_VIDEO_INFO_WIDTH (&postproc->srcpad_info) - 1 - x;
            break;
          case GST_VIDEO_ORIENTATION_90L:
            new_x = GST_VIDEO_INFO_HEIGHT (&postproc->srcpad_info) - 1 - y;
            new_y = x;
            break;
          case GST_VIDEO_ORIENTATION_UR_LL:
            new_x = GST_VIDEO_INFO_HEIGHT (&postproc->srcpad_info) - 1 - y;
            new_y = GST_VIDEO_INFO_WIDTH (&postproc->srcpad_info) - 1 - x;
            break;
          case GST_VIDEO_ORIENTATION_UL_LR:
            new_x = y;
            new_y = x;
            break;
          case GST_VIDEO_ORIENTATION_180:
            new_x = GST_VIDEO_INFO_WIDTH (&postproc->srcpad_info) - 1 - x;
            new_y = GST_VIDEO_INFO_HEIGHT (&postproc->srcpad_info) - 1 - y;
            break;
          case GST_VIDEO_ORIENTATION_HORIZ:
            new_x = GST_VIDEO_INFO_WIDTH (&postproc->srcpad_info) - 1 - x;
            new_y = y;
            break;
          case GST_VIDEO_ORIENTATION_VERT:
            new_x = x;
            new_y = GST_VIDEO_INFO_HEIGHT (&postproc->srcpad_info) - 1 - y;
            break;
          default:
            new_x = x;
            new_y = y;
            break;
        }

        /* scale compensation */
        get_scale_factor (postproc, &w_factor, &h_factor);
        new_x *= w_factor;
        new_y *= h_factor;

        /* crop compensation */
        new_x += postproc->crop_left;
        new_y += postproc->crop_top;

        GST_DEBUG_OBJECT (postproc, "to %fx%f", new_x, new_y);
        gst_navigation_event_set_coordinates (event, new_x, new_y);
      }
      break;
    default:
      break;
  }

  ret =
      GST_BASE_TRANSFORM_CLASS (gst_vaapipostproc_parent_class)->src_event
      (trans, event);

  return ret;
}

static gboolean
gst_vaapipostproc_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (trans);
  GstTagList *taglist;
  GstVideoOrientationMethod method;
  gboolean ret;
  gboolean do_reconf;

  GST_DEBUG_OBJECT (postproc, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      gst_event_parse_tag (event, &taglist);

      do_reconf = gst_video_orientation_from_tag (taglist, &method);

      if (do_reconf) {
        postproc->tag_video_direction = method;
        postproc->flags |= GST_VAAPI_POSTPROC_FLAG_VIDEO_DIRECTION;
        gst_base_transform_reconfigure_src (trans);
      }
      break;
    default:
      break;
  }

  ret =
      GST_BASE_TRANSFORM_CLASS (gst_vaapipostproc_parent_class)->sink_event
      (trans, event);

  return ret;
}

static void
gst_vaapipostproc_finalize (GObject * object)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (object);

  gst_vaapipostproc_destroy (postproc);

  g_mutex_clear (&postproc->postproc_lock);
  gst_vaapi_plugin_base_finalize (GST_VAAPI_PLUGIN_BASE (postproc));
  G_OBJECT_CLASS (gst_vaapipostproc_parent_class)->finalize (object);
}

static void
gst_vaapipostproc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (object);
  gboolean do_reconf = FALSE;

  g_mutex_lock (&postproc->postproc_lock);
  switch (prop_id) {
#ifndef GST_REMOVE_DEPRECATED
    case PROP_FORMAT:
      postproc->format = g_value_get_enum (value);
      break;
    case PROP_WIDTH:
    {
      guint prev_width = postproc->width;
      postproc->width = g_value_get_uint (value);
      do_reconf = (prev_width != postproc->width);
      break;
    }
    case PROP_HEIGHT:
    {
      guint prev_height = postproc->height;
      postproc->height = g_value_get_uint (value);
      do_reconf = (prev_height != postproc->height);
      break;
    }
#endif
    case PROP_FORCE_ASPECT_RATIO:
      postproc->keep_aspect = g_value_get_boolean (value);
      break;
    case PROP_DEINTERLACE_MODE:
      postproc->deinterlace_mode = g_value_get_enum (value);
      break;
    case PROP_DEINTERLACE_METHOD:
      postproc->deinterlace_method = g_value_get_enum (value);
      break;
    case PROP_DENOISE:
      postproc->denoise_level = g_value_get_float (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_DENOISE;
      break;
    case PROP_SHARPEN:
      postproc->sharpen_level = g_value_get_float (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_SHARPEN;
      break;
    case PROP_HUE:
      postproc->hue = g_value_get_float (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_HUE;
      break;
    case PROP_SATURATION:
      postproc->saturation = g_value_get_float (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_SATURATION;
      break;
    case PROP_BRIGHTNESS:
      postproc->brightness = g_value_get_float (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_BRIGHTNESS;
      break;
    case PROP_CONTRAST:
      postproc->contrast = g_value_get_float (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_CONTRAST;
      break;
    case PROP_SCALE_METHOD:
      postproc->scale_method = g_value_get_enum (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_SCALE;
      break;
    case PROP_VIDEO_DIRECTION:
      postproc->video_direction = g_value_get_enum (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_VIDEO_DIRECTION;
      break;
#ifndef GST_REMOVE_DEPRECATED
    case PROP_SKIN_TONE_ENHANCEMENT:
      postproc->skintone_enhance = g_value_get_boolean (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_SKINTONE;
      break;
#endif
    case PROP_SKIN_TONE_ENHANCEMENT_LEVEL:
      postproc->skintone_value = g_value_get_uint (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_SKINTONE_LEVEL;
      break;
    case PROP_CROP_LEFT:
    {
      guint prev_crop_left = postproc->crop_left;
      postproc->crop_left = g_value_get_uint (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_CROP;
      do_reconf = (prev_crop_left != postproc->crop_left);
      break;
    }
    case PROP_CROP_RIGHT:
    {
      guint prev_crop_right = postproc->crop_right;
      postproc->crop_right = g_value_get_uint (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_CROP;
      do_reconf = (prev_crop_right != postproc->crop_right);
      break;
    }
    case PROP_CROP_TOP:
    {
      guint prev_crop_top = postproc->crop_top;
      postproc->crop_top = g_value_get_uint (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_CROP;
      do_reconf = (prev_crop_top != postproc->crop_top);
      break;
    }
    case PROP_CROP_BOTTOM:
    {
      guint prev_crop_bottom = postproc->crop_bottom;
      postproc->crop_bottom = g_value_get_uint (value);
      postproc->flags |= GST_VAAPI_POSTPROC_FLAG_CROP;
      do_reconf = (prev_crop_bottom != postproc->crop_bottom);
      break;
    }
    case PROP_HDR_TONE_MAP:
      postproc->hdr_tone_map = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&postproc->postproc_lock);

  if (do_reconf || check_filter_update (postproc))
    gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM (postproc));
}

static void
gst_vaapipostproc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (object);

  g_mutex_lock (&postproc->postproc_lock);
  switch (prop_id) {
#ifndef GST_REMOVE_DEPRECATED
    case PROP_FORMAT:
      g_value_set_enum (value, postproc->format);
      break;
    case PROP_WIDTH:
      g_value_set_uint (value, postproc->width);
      break;
    case PROP_HEIGHT:
      g_value_set_uint (value, postproc->height);
      break;
#endif
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, postproc->keep_aspect);
      break;
    case PROP_DEINTERLACE_MODE:
      g_value_set_enum (value, postproc->deinterlace_mode);
      break;
    case PROP_DEINTERLACE_METHOD:
      g_value_set_enum (value, postproc->deinterlace_method);
      break;
    case PROP_DENOISE:
      g_value_set_float (value, postproc->denoise_level);
      break;
    case PROP_SHARPEN:
      g_value_set_float (value, postproc->sharpen_level);
      break;
    case PROP_HUE:
      g_value_set_float (value, postproc->hue);
      break;
    case PROP_SATURATION:
      g_value_set_float (value, postproc->saturation);
      break;
    case PROP_BRIGHTNESS:
      g_value_set_float (value, postproc->brightness);
      break;
    case PROP_CONTRAST:
      g_value_set_float (value, postproc->contrast);
      break;
    case PROP_SCALE_METHOD:
      g_value_set_enum (value, postproc->scale_method);
      break;
    case PROP_VIDEO_DIRECTION:
      g_value_set_enum (value, postproc->video_direction);
      break;
#ifndef GST_REMOVE_DEPRECATED
    case PROP_SKIN_TONE_ENHANCEMENT:
      g_value_set_boolean (value, postproc->skintone_enhance);
      break;
#endif
    case PROP_SKIN_TONE_ENHANCEMENT_LEVEL:
      g_value_set_uint (value, postproc->skintone_value);
      break;
    case PROP_CROP_LEFT:
      g_value_set_uint (value, postproc->crop_left);
      break;
    case PROP_CROP_RIGHT:
      g_value_set_uint (value, postproc->crop_right);
      break;
    case PROP_CROP_TOP:
      g_value_set_uint (value, postproc->crop_top);
      break;
    case PROP_CROP_BOTTOM:
      g_value_set_uint (value, postproc->crop_bottom);
      break;
    case PROP_HDR_TONE_MAP:
      g_value_set_enum (value, postproc->hdr_tone_map);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&postproc->postproc_lock);
}

static void
gst_vaapipostproc_class_init (GstVaapiPostprocClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *const trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GPtrArray *filter_ops;
  GstVaapiFilterOpInfo *filter_op;

  GST_DEBUG_CATEGORY_INIT (gst_debug_vaapipostproc,
      GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

  gst_vaapi_plugin_base_class_init (GST_VAAPI_PLUGIN_BASE_CLASS (klass));

  object_class->finalize = gst_vaapipostproc_finalize;
  object_class->set_property = gst_vaapipostproc_set_property;
  object_class->get_property = gst_vaapipostproc_get_property;
  trans_class->start = gst_vaapipostproc_start;
  trans_class->stop = gst_vaapipostproc_stop;
  trans_class->fixate_caps = gst_vaapipostproc_fixate_caps;
  trans_class->transform_caps = gst_vaapipostproc_transform_caps;
  trans_class->transform_size = gst_vaapipostproc_transform_size;
  trans_class->transform_meta = gst_vaapipostproc_transform_meta;
  trans_class->transform = gst_vaapipostproc_transform;
  trans_class->set_caps = gst_vaapipostproc_set_caps;
  trans_class->query = gst_vaapipostproc_query;
  trans_class->propose_allocation = gst_vaapipostproc_propose_allocation;
  trans_class->decide_allocation = gst_vaapipostproc_decide_allocation;
  trans_class->src_event = gst_vaapipostproc_src_event;
  trans_class->sink_event = gst_vaapipostproc_sink_event;

  trans_class->prepare_output_buffer = gst_vaapipostproc_prepare_output_buffer;

  element_class->set_context = gst_vaapi_base_set_context;
  gst_element_class_set_static_metadata (element_class,
      "VA-API video postprocessing",
      "Filter/Converter/Effect/Video/Scaler/Deinterlace/Hardware",
      GST_PLUGIN_DESC, "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

  /* sink pad */
  gst_element_class_add_static_pad_template (element_class,
      &gst_vaapipostproc_sink_factory);

  /* src pad */
  gst_element_class_add_static_pad_template (element_class,
      &gst_vaapipostproc_src_factory);

  /**
   * GstVaapiPostproc:hdr-tone-map:
   *
   * Selects whether HDR tone mapping should not be applied or if it
   * should be only applied on content that has the HDR meta on the caps.
   */
  g_object_class_install_property
      (object_class,
      PROP_HDR_TONE_MAP,
      g_param_spec_enum ("hdr-tone-map",
          "HDR Tone Map",
          "Apply HDR tone mapping algorithm",
          GST_VAAPI_TYPE_HDR_TONE_MAP,
          GST_VAAPI_HDR_TONE_MAP_AUTO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiPostproc:deinterlace-mode:
   *
   * This selects whether the deinterlacing should always be applied
   * or if they should only be applied on content that has the
   * "interlaced" flag on the caps.
   */
  g_object_class_install_property
      (object_class,
      PROP_DEINTERLACE_MODE,
      g_param_spec_enum ("deinterlace-mode",
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
      g_param_spec_enum ("deinterlace-method",
          "Deinterlace method",
          "Deinterlace method to use",
          GST_VAAPI_TYPE_DEINTERLACE_METHOD,
          DEFAULT_DEINTERLACE_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  filter_ops = gst_vaapi_filter_get_operations (NULL);
  if (!filter_ops)
    return;

#ifndef GST_REMOVE_DEPRECATED
  /**
   * GstVaapiPostproc:format:
   *
   * The forced output pixel format, expressed as a #GstVideoFormat.
   */
  filter_op = find_filter_op (filter_ops, GST_VAAPI_FILTER_OP_FORMAT);
  if (filter_op)
    g_object_class_install_property (object_class,
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
      g_param_spec_uint ("width",
          "Width",
          "Forced output width",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiPostproc:height:
   *
   * The forced output height in pixels. If set to zero, the height is
   * calculated from the width if aspect ration is preserved, or
   * inherited from the sink caps height
   */
  g_object_class_install_property
      (object_class,
      PROP_HEIGHT,
      g_param_spec_uint ("height",
          "Height",
          "Forced output height",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  /**
   * GstVaapiPostproc:crop-left:
   *
   * The number of pixels to crop at left.
   */
  g_object_class_install_property
      (object_class,
      PROP_CROP_LEFT,
      g_param_spec_uint ("crop-left",
          "Crop Left",
          "Pixels to crop at left",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiPostproc:crop-right:
   *
   * The number of pixels to crop at right.
   */
  g_object_class_install_property
      (object_class,
      PROP_CROP_RIGHT,
      g_param_spec_uint ("crop-right",
          "Crop Right",
          "Pixels to crop at right",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
  * GstVaapiPostproc:crop-top:
  *
  * The number of pixels to crop at top.
  */
  g_object_class_install_property
      (object_class,
      PROP_CROP_TOP,
      g_param_spec_uint ("crop-top",
          "Crop Top",
          "Pixels to crop at top",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiPostproc:crop-bottom:
   *
   * The number of pixels to crop at bottom.
   */
  g_object_class_install_property
      (object_class,
      PROP_CROP_BOTTOM,
      g_param_spec_uint ("crop-bottom",
          "Crop Bottom",
          "Pixels to crop at bottom",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiPostproc:force-aspect-ratio:
   *
   * When enabled, scaling respects video aspect ratio; when disabled,
   * the video is distorted to fit the width and height properties.
   */
  g_object_class_install_property
      (object_class,
      PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiPostproc:denoise:
   *
   * The level of noise reduction to apply.
   */
  filter_op = find_filter_op (filter_ops, GST_VAAPI_FILTER_OP_DENOISE);
  if (filter_op)
    g_object_class_install_property (object_class,
        PROP_DENOISE, filter_op->pspec);

  /**
   * GstVaapiPostproc:sharpen:
   *
   * The level of sharpening to apply for positive values, or the
   * level of blurring for negative values.
   */
  filter_op = find_filter_op (filter_ops, GST_VAAPI_FILTER_OP_SHARPEN);
  if (filter_op)
    g_object_class_install_property (object_class,
        PROP_SHARPEN, filter_op->pspec);

  /**
   * GstVaapiPostproc:hue:
   *
   * The color hue, expressed as a float value. Range is -180.0 to
   * 180.0. Default value is 0.0 and represents no modification.
   */
  filter_op = find_filter_op (filter_ops, GST_VAAPI_FILTER_OP_HUE);
  if (filter_op)
    g_object_class_install_property (object_class, PROP_HUE, filter_op->pspec);

  /**
   * GstVaapiPostproc:saturation:
   *
   * The color saturation, expressed as a float value. Range is 0.0 to
   * 2.0. Default value is 1.0 and represents no modification.
   */
  filter_op = find_filter_op (filter_ops, GST_VAAPI_FILTER_OP_SATURATION);
  if (filter_op)
    g_object_class_install_property (object_class,
        PROP_SATURATION, filter_op->pspec);

  /**
   * GstVaapiPostproc:brightness:
   *
   * The color brightness, expressed as a float value. Range is -1.0
   * to 1.0. Default value is 0.0 and represents no modification.
   */
  filter_op = find_filter_op (filter_ops, GST_VAAPI_FILTER_OP_BRIGHTNESS);
  if (filter_op)
    g_object_class_install_property (object_class,
        PROP_BRIGHTNESS, filter_op->pspec);

  /**
   * GstVaapiPostproc:contrast:
   *
   * The color contrast, expressed as a float value. Range is 0.0 to
   * 2.0. Default value is 1.0 and represents no modification.
   */
  filter_op = find_filter_op (filter_ops, GST_VAAPI_FILTER_OP_CONTRAST);
  if (filter_op)
    g_object_class_install_property (object_class,
        PROP_CONTRAST, filter_op->pspec);

  /**
   * GstVaapiPostproc:scale-method:
   *
   * The scaling method to use, expressed as an enum value. See
   * #GstVaapiScaleMethod.
   */
  filter_op = find_filter_op (filter_ops, GST_VAAPI_FILTER_OP_SCALING);
  if (filter_op)
    g_object_class_install_property (object_class,
        PROP_SCALE_METHOD, filter_op->pspec);

  /**
   * GstVaapiPostproc:video-direction:
   *
   * The video-direction to use, expressed as an enum value. See
   * #GstVideoDirection.
   */
  filter_op = find_filter_op (filter_ops, GST_VAAPI_FILTER_OP_VIDEO_DIRECTION);
  if (filter_op)
    g_object_class_install_property (object_class,
        PROP_VIDEO_DIRECTION, filter_op->pspec);

#ifndef GST_REMOVE_DEPRECATED
  /**
   * GstVaapiPostproc:skin-tone-enhancement:
   *
   * Apply the skin tone enhancement algorithm.
   */
  filter_op = find_filter_op (filter_ops, GST_VAAPI_FILTER_OP_SKINTONE);
  if (filter_op)
    g_object_class_install_property (object_class,
        PROP_SKIN_TONE_ENHANCEMENT, filter_op->pspec);
#endif

  /**
   * GstVaapiPostproc:skin-tone-enhancement-setting:
   *
   * Apply the skin tone enhancement algorithm with specified value.
   */
  filter_op = find_filter_op (filter_ops, GST_VAAPI_FILTER_OP_SKINTONE_LEVEL);
  if (filter_op)
    g_object_class_install_property (object_class,
        PROP_SKIN_TONE_ENHANCEMENT_LEVEL, filter_op->pspec);

  g_ptr_array_unref (filter_ops);
}

static float *
find_value_ptr (GstVaapiPostproc * postproc, GstVaapiFilterOp op)
{
  switch (op) {
    case GST_VAAPI_FILTER_OP_HUE:
      return &postproc->hue;
    case GST_VAAPI_FILTER_OP_SATURATION:
      return &postproc->saturation;
    case GST_VAAPI_FILTER_OP_BRIGHTNESS:
      return &postproc->brightness;
    case GST_VAAPI_FILTER_OP_CONTRAST:
      return &postproc->contrast;
    default:
      return NULL;
  }
}

static void
cb_set_default_value (GstVaapiPostproc * postproc, GPtrArray * filter_ops,
    GstVaapiFilterOp op)
{
  GstVaapiFilterOpInfo *filter_op;
  GParamSpecFloat *pspec;
  float *var;

  filter_op = find_filter_op (filter_ops, op);
  if (!filter_op)
    return;
  var = find_value_ptr (postproc, op);
  if (!var)
    return;
  pspec = G_PARAM_SPEC_FLOAT (filter_op->pspec);
  *var = pspec->default_value;
}

static void
skintone_set_default_value (GstVaapiPostproc * postproc, GPtrArray * filter_ops)
{
  GstVaapiFilterOpInfo *filter_op;
  GParamSpecUInt *pspec;

  filter_op = find_filter_op (filter_ops, GST_VAAPI_FILTER_OP_SKINTONE_LEVEL);
  if (!filter_op)
    return;
  pspec = G_PARAM_SPEC_UINT (filter_op->pspec);
  postproc->skintone_value = pspec->default_value;
}

static void
gst_vaapipostproc_init (GstVaapiPostproc * postproc)
{
  GPtrArray *filter_ops;
  guint i;

  gst_vaapi_plugin_base_init (GST_VAAPI_PLUGIN_BASE (postproc),
      GST_CAT_DEFAULT);

  g_mutex_init (&postproc->postproc_lock);
  postproc->format = DEFAULT_FORMAT;
  postproc->hdr_tone_map = GST_VAAPI_HDR_TONE_MAP_AUTO;
  postproc->deinterlace_mode = DEFAULT_DEINTERLACE_MODE;
  postproc->deinterlace_method = DEFAULT_DEINTERLACE_METHOD;
  postproc->field_duration = GST_CLOCK_TIME_NONE;
  postproc->keep_aspect = TRUE;
  postproc->get_va_surfaces = TRUE;
  postproc->forward_crop = FALSE;

  /* AUTO is not valid for tag_video_direction, this is just to
   * ensure we setup the method as sink event tag */
  postproc->tag_video_direction = GST_VIDEO_ORIENTATION_AUTO;

  filter_ops = gst_vaapi_filter_get_operations (NULL);
  if (filter_ops) {
    for (i = GST_VAAPI_FILTER_OP_HUE; i <= GST_VAAPI_FILTER_OP_CONTRAST; i++)
      cb_set_default_value (postproc, filter_ops, i);

    skintone_set_default_value (postproc, filter_ops);
    g_ptr_array_unref (filter_ops);
  }

  gst_video_info_init (&postproc->sinkpad_info);
  gst_video_info_init (&postproc->srcpad_info);
  gst_video_info_init (&postproc->filter_pool_info);
}

/* ------------------------------------------------------------------------ */
/* --- GstColorBalance interface                                        --- */
/* ------------------------------------------------------------------------ */

#define CB_CHANNEL_FACTOR 1000.0

typedef struct
{
  GstVaapiFilterOp op;
  const gchar *name;
} ColorBalanceChannel;

ColorBalanceChannel cb_channels[] = {
  {
      GST_VAAPI_FILTER_OP_HUE, "VA_FILTER_HUE"}, {
      GST_VAAPI_FILTER_OP_SATURATION, "VA_FILTER_SATURATION"}, {
      GST_VAAPI_FILTER_OP_BRIGHTNESS, "VA_FILTER_BRIGHTNESS"}, {
      GST_VAAPI_FILTER_OP_CONTRAST, "VA_FILTER_CONTRAST"},
};

static void
cb_channels_init (GstVaapiPostproc * postproc)
{
  GPtrArray *filter_ops;
  GstVaapiFilterOpInfo *filter_op;
  GParamSpecFloat *pspec;
  GstColorBalanceChannel *channel;
  guint i;

  if (postproc->cb_channels)
    return;

  g_mutex_lock (&postproc->postproc_lock);
  if (!gst_vaapipostproc_ensure_filter (postproc)) {
    g_mutex_unlock (&postproc->postproc_lock);
    return;
  }
  g_mutex_unlock (&postproc->postproc_lock);

  filter_ops = postproc->filter_ops ? g_ptr_array_ref (postproc->filter_ops)
      : gst_vaapi_filter_get_operations (postproc->filter);
  if (!filter_ops)
    return;

  for (i = 0; i < G_N_ELEMENTS (cb_channels); i++) {
    filter_op = find_filter_op (filter_ops, cb_channels[i].op);
    if (!filter_op)
      continue;

    pspec = G_PARAM_SPEC_FLOAT (filter_op->pspec);
    channel = g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, NULL);
    channel->label = g_strdup (cb_channels[i].name);
    channel->min_value = pspec->minimum * CB_CHANNEL_FACTOR;
    channel->max_value = pspec->maximum * CB_CHANNEL_FACTOR;

    postproc->cb_channels = g_list_prepend (postproc->cb_channels, channel);
  }

  g_ptr_array_unref (filter_ops);
}

static const GList *
gst_vaapipostproc_colorbalance_list_channels (GstColorBalance * balance)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (balance);

  cb_channels_init (postproc);
  return postproc->cb_channels;
}

static gfloat *
cb_get_value_ptr (GstVaapiPostproc * postproc,
    GstColorBalanceChannel * channel, GstVaapiPostprocFlags * flags)
{
  guint i;
  gfloat *ret = NULL;

  for (i = 0; i < G_N_ELEMENTS (cb_channels); i++) {
    if (g_ascii_strcasecmp (cb_channels[i].name, channel->label) == 0)
      break;
  }
  if (i >= G_N_ELEMENTS (cb_channels))
    return NULL;

  ret = find_value_ptr (postproc, cb_channels[i].op);
  if (flags)
    *flags = 1 << cb_channels[i].op;
  return ret;
}

static void
gst_vaapipostproc_colorbalance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (balance);
  GstVaapiPostprocFlags flags;
  gfloat new_val, *var;

  value = CLAMP (value, channel->min_value, channel->max_value);
  new_val = (gfloat) value / CB_CHANNEL_FACTOR;

  var = cb_get_value_ptr (postproc, channel, &flags);
  if (var) {
    *var = new_val;
    g_mutex_lock (&postproc->postproc_lock);
    postproc->flags |= flags;
    g_mutex_unlock (&postproc->postproc_lock);
    gst_color_balance_value_changed (balance, channel, value);
    if (check_filter_update (postproc))
      gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM (postproc));
    return;
  }

  GST_WARNING_OBJECT (postproc, "unknown channel %s", channel->label);
}

static gint
gst_vaapipostproc_colorbalance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel)
{
  GstVaapiPostproc *const postproc = GST_VAAPIPOSTPROC (balance);
  gfloat *var;
  gint new_val;

  var = cb_get_value_ptr (postproc, channel, NULL);
  if (var) {
    new_val = (gint) ((*var) * CB_CHANNEL_FACTOR);
    new_val = CLAMP (new_val, channel->min_value, channel->max_value);
    return new_val;
  }

  GST_WARNING_OBJECT (postproc, "unknown channel %s", channel->label);
  return G_MININT;
}

static GstColorBalanceType
gst_vaapipostproc_colorbalance_get_balance_type (GstColorBalance * balance)
{
  return GST_COLOR_BALANCE_HARDWARE;
}

static void
gst_vaapipostproc_colorbalance_init (gpointer iface, gpointer data)
{
  GstColorBalanceInterface *cbface = iface;
  cbface->list_channels = gst_vaapipostproc_colorbalance_list_channels;
  cbface->set_value = gst_vaapipostproc_colorbalance_set_value;
  cbface->get_value = gst_vaapipostproc_colorbalance_get_value;
  cbface->get_balance_type = gst_vaapipostproc_colorbalance_get_balance_type;
}
