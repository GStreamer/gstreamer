/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the0
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-vapostproc
 * @title: vapostproc
 * @short_description: A VA-API base video postprocessing filter
 *
 * vapostproc applies different video filters to VA surfaces. These
 * filters vary depending on the installed and chosen
 * [VA-API](https://01.org/linuxmedia/vaapi) driver, but usually
 * resizing and color conversion are available.
 *
 * The generated surfaces can be mapped onto main memory as video
 * frames.
 *
 * Use gst-inspect-1.0 to introspect the available capabilities of the
 * driver's post-processor entry point.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc ! "video/x-raw,format=(string)NV12" ! vapostproc ! autovideosink
 * ```
 *
 * Cropping is supported via buffers' crop meta. It's only done if the
 * postproccessor is not in passthrough mode or if downstream doesn't
 * support the crop meta API.
 *
 * ### Cropping example
 * ```
 * gst-launch-1.0 videotestsrc ! "video/x-raw,format=(string)NV12" ! videocrop bottom=50 left=100 ! vapostproc ! autovideosink
 * ```
 *
 * If the VA driver support color balance filter, with controls such
 * as hue, brightness, contrast, etc., those controls are exposed both
 * as element properties and through the #GstColorBalance interface.
 *
 * Since: 1.20
 *
 */

/* ToDo:
 *
 * + deinterlacing
 * + HDR tone mapping
 * + colorimetry
 * + cropping
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvavpp.h"

#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include <va/va_drmcommon.h>

#include "gstvaallocator.h"
#include "gstvacaps.h"
#include "gstvadisplay_priv.h"
#include "gstvafilter.h"
#include "gstvapool.h"
#include "gstvautils.h"

GST_DEBUG_CATEGORY_STATIC (gst_va_vpp_debug);
#define GST_CAT_DEFAULT gst_va_vpp_debug

#define GST_VA_VPP(obj)           ((GstVaVpp *) obj)
#define GST_VA_VPP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVaVppClass))
#define GST_VA_VPP_CLASS(klass)    ((GstVaVppClass *) klass)

#define SWAP(a, b) do { const __typeof__ (a) t = a; a = b; b = t; } while (0)

typedef struct _GstVaVpp GstVaVpp;
typedef struct _GstVaVppClass GstVaVppClass;

struct _GstVaVppClass
{
  /* GstVideoFilter overlaps functionality */
  GstBaseTransformClass parent_class;

  gchar *render_device_path;
};

struct _GstVaVpp
{
  GstBaseTransform parent;

  GstVaDisplay *display;
  GstVaFilter *filter;

  GstCaps *incaps;
  GstCaps *outcaps;
  GstCaps *alloccaps;
  GstVideoInfo in_info;
  GstVideoInfo out_info;
  gboolean negotiated;

  GstBufferPool *sinkpad_pool;
  GstVideoInfo sinkpad_info;

  GstBufferPool *other_pool;
  GstVideoInfo srcpad_info;

  gboolean rebuild_filters;
  gboolean forward_crop;
  guint op_flags;

  /* filters */
  float denoise;
  float sharpen;
  float skintone;
  float brightness;
  float contrast;
  float hue;
  float saturation;
  gboolean auto_contrast;
  gboolean auto_brightness;
  gboolean auto_saturation;
  GstVideoOrientationMethod direction;
  GstVideoOrientationMethod prev_direction;
  GstVideoOrientationMethod tag_direction;

  GList *channels;
};

static GstElementClass *parent_class = NULL;

struct CData
{
  gchar *render_device_path;
  gchar *description;
};

/* convertions that disable passthrough */
enum
{
  VPP_CONVERT_SIZE = 1 << 0,
  VPP_CONVERT_FORMAT = 1 << 1,
  VPP_CONVERT_FILTERS = 1 << 2,
  VPP_CONVERT_DIRECTION = 1 << 3,
  VPP_CONVERT_FEATURE = 1 << 4,
  VPP_CONVERT_CROP = 1 << 5,
  VPP_CONVERT_DUMMY = 1 << 6,
};

extern GRecMutex GST_VA_SHARED_LOCK;

/* *INDENT-OFF* */
static const gchar *caps_str = GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("memory:VAMemory",
            "{ NV12, I420, YV12, YUY2, RGBA, BGRA, P010_10LE, ARGB, ABGR }") " ;"
            GST_VIDEO_CAPS_MAKE ("{ VUYA, GRAY8, NV12, NV21, YUY2, UYVY, YV12, "
            "I420, P010_10LE, RGBA, BGRA, ARGB, ABGR  }");
/* *INDENT-ON* */

#define META_TAG_COLORSPACE meta_tag_colorspace_quark
static GQuark meta_tag_colorspace_quark;
#define META_TAG_SIZE meta_tag_size_quark
static GQuark meta_tag_size_quark;
#define META_TAG_ORIENTATION meta_tag_orientation_quark
static GQuark meta_tag_orientation_quark;
#define META_TAG_VIDEO meta_tag_video_quark
static GQuark meta_tag_video_quark;

static void gst_va_vpp_colorbalance_init (gpointer iface, gpointer data);
static void gst_va_vpp_rebuild_filters (GstVaVpp * self);

static void
gst_va_vpp_dispose (GObject * object)
{
  GstVaVpp *self = GST_VA_VPP (object);

  if (self->channels)
    g_list_free_full (g_steal_pointer (&self->channels), g_object_unref);

  if (self->sinkpad_pool) {
    gst_buffer_pool_set_active (self->sinkpad_pool, FALSE);
    gst_clear_object (&self->sinkpad_pool);
  }

  if (self->other_pool) {
    gst_buffer_pool_set_active (self->other_pool, FALSE);
    gst_clear_object (&self->other_pool);
  }

  gst_clear_caps (&self->incaps);
  gst_clear_caps (&self->outcaps);
  gst_clear_caps (&self->alloccaps);

  gst_clear_object (&self->filter);
  gst_clear_object (&self->display);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_va_vpp_update_passthrough (GstVaVpp * self, gboolean reconf)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (self);
  gboolean old, new;

  old = gst_base_transform_is_passthrough (trans);

  GST_OBJECT_LOCK (self);
  new = (self->op_flags == 0);
  GST_OBJECT_UNLOCK (self);

  if (old != new) {
    GST_INFO_OBJECT (self, "%s passthrough", new ? "enabling" : "disabling");
    if (reconf)
      gst_base_transform_reconfigure_src (trans);
    gst_base_transform_set_passthrough (trans, new);
  }
}

static void
_update_properties_unlocked (GstVaVpp * self)
{
  if (!self->filter)
    return;

  if ((self->direction != GST_VIDEO_ORIENTATION_AUTO
          && self->direction != self->prev_direction)
      || (self->direction == GST_VIDEO_ORIENTATION_AUTO
          && self->tag_direction != self->prev_direction)) {

    GstVideoOrientationMethod direction =
        (self->direction == GST_VIDEO_ORIENTATION_AUTO) ?
        self->tag_direction : self->direction;

    if (!gst_va_filter_set_orientation (self->filter, direction)) {
      if (self->direction == GST_VIDEO_ORIENTATION_AUTO)
        self->tag_direction = self->prev_direction;
      else
        self->direction = self->prev_direction;

      self->op_flags &= ~VPP_CONVERT_DIRECTION;

      /* FIXME: unlocked bus warning message */
      GST_WARNING_OBJECT (self,
          "Driver cannot set resquested orientation. Setting it back.");
    } else {
      self->prev_direction = direction;

      self->op_flags |= VPP_CONVERT_DIRECTION;

      gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM (self));
    }
  } else {
    self->op_flags &= ~VPP_CONVERT_DIRECTION;
  }
}

static void
gst_va_vpp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaVpp *self = GST_VA_VPP (object);

  GST_OBJECT_LOCK (object);
  switch (prop_id) {
    case GST_VA_FILTER_PROP_DENOISE:
      self->denoise = g_value_get_float (value);
      g_atomic_int_set (&self->rebuild_filters, TRUE);
      break;
    case GST_VA_FILTER_PROP_SHARPEN:
      self->sharpen = g_value_get_float (value);
      g_atomic_int_set (&self->rebuild_filters, TRUE);
      break;
    case GST_VA_FILTER_PROP_SKINTONE:
      if (G_VALUE_TYPE (value) == G_TYPE_BOOLEAN)
        self->skintone = (float) g_value_get_boolean (value);
      else
        self->skintone = g_value_get_float (value);
      g_atomic_int_set (&self->rebuild_filters, TRUE);
      break;
    case GST_VA_FILTER_PROP_VIDEO_DIR:{
      GstVideoOrientationMethod direction = g_value_get_enum (value);
      self->prev_direction = (direction == GST_VIDEO_ORIENTATION_AUTO) ?
          self->tag_direction : self->direction;
      self->direction = direction;
      break;
    }
    case GST_VA_FILTER_PROP_HUE:
      self->hue = g_value_get_float (value);
      g_atomic_int_set (&self->rebuild_filters, TRUE);
      break;
    case GST_VA_FILTER_PROP_SATURATION:
      self->saturation = g_value_get_float (value);
      g_atomic_int_set (&self->rebuild_filters, TRUE);
      break;
    case GST_VA_FILTER_PROP_BRIGHTNESS:
      self->brightness = g_value_get_float (value);
      g_atomic_int_set (&self->rebuild_filters, TRUE);
      break;
    case GST_VA_FILTER_PROP_CONTRAST:
      self->contrast = g_value_get_float (value);
      g_atomic_int_set (&self->rebuild_filters, TRUE);
      break;
    case GST_VA_FILTER_PROP_AUTO_SATURATION:
      self->auto_saturation = g_value_get_boolean (value);
      g_atomic_int_set (&self->rebuild_filters, TRUE);
      break;
    case GST_VA_FILTER_PROP_AUTO_BRIGHTNESS:
      self->auto_brightness = g_value_get_boolean (value);
      g_atomic_int_set (&self->rebuild_filters, TRUE);
      break;
    case GST_VA_FILTER_PROP_AUTO_CONTRAST:
      self->auto_contrast = g_value_get_boolean (value);
      g_atomic_int_set (&self->rebuild_filters, TRUE);
      break;
    case GST_VA_FILTER_PROP_DISABLE_PASSTHROUGH:{
      gboolean disable_passthrough = g_value_get_boolean (value);
      if (disable_passthrough)
        self->op_flags |= VPP_CONVERT_DUMMY;
      else
        self->op_flags &= ~VPP_CONVERT_DUMMY;
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  _update_properties_unlocked (self);
  GST_OBJECT_UNLOCK (object);

  /* no reconfig here because it's done in
   * _update_properties_unlocked() */
  gst_va_vpp_update_passthrough (self, FALSE);
}

static void
gst_va_vpp_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVaVpp *self = GST_VA_VPP (object);

  GST_OBJECT_LOCK (object);
  switch (prop_id) {
    case GST_VA_FILTER_PROP_DENOISE:
      g_value_set_float (value, self->denoise);
      break;
    case GST_VA_FILTER_PROP_SHARPEN:
      g_value_set_float (value, self->sharpen);
      break;
    case GST_VA_FILTER_PROP_SKINTONE:
      if (G_VALUE_TYPE (value) == G_TYPE_BOOLEAN)
        g_value_set_boolean (value, self->skintone > 0);
      else
        g_value_set_float (value, self->skintone);
      break;
    case GST_VA_FILTER_PROP_VIDEO_DIR:
      g_value_set_enum (value, self->direction);
      break;
    case GST_VA_FILTER_PROP_HUE:
      g_value_set_float (value, self->hue);
      break;
    case GST_VA_FILTER_PROP_SATURATION:
      g_value_set_float (value, self->saturation);
      break;
    case GST_VA_FILTER_PROP_BRIGHTNESS:
      g_value_set_float (value, self->brightness);
      break;
    case GST_VA_FILTER_PROP_CONTRAST:
      g_value_set_float (value, self->contrast);
      break;
    case GST_VA_FILTER_PROP_AUTO_SATURATION:
      g_value_set_boolean (value, self->auto_saturation);
      break;
    case GST_VA_FILTER_PROP_AUTO_BRIGHTNESS:
      g_value_set_boolean (value, self->auto_brightness);
      break;
    case GST_VA_FILTER_PROP_AUTO_CONTRAST:
      g_value_set_boolean (value, self->auto_contrast);
      break;
    case GST_VA_FILTER_PROP_DISABLE_PASSTHROUGH:
      g_value_set_boolean (value, (self->op_flags & VPP_CONVERT_DUMMY));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (object);
}

static GstStateChangeReturn
gst_va_vpp_change_state (GstElement * element, GstStateChange transition)
{
  GstVaVpp *self = GST_VA_VPP (element);
  GstVaVppClass *klass = GST_VA_VPP_GET_CLASS (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_va_ensure_element_data (element, klass->render_device_path,
              &self->display))
        goto open_failed;
      if (!self->filter)
        self->filter = gst_va_filter_new (self->display);
      if (!gst_va_filter_open (self->filter))
        goto open_failed;
      _update_properties_unlocked (self);
      gst_va_vpp_rebuild_filters (self);
      gst_va_vpp_update_passthrough (self, FALSE);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_va_filter_close (self->filter);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_clear_object (&self->filter);
      gst_clear_object (&self->display);
      break;
    default:
      break;
  }

  return ret;

  /* Errors */
open_failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT, (NULL), ("Failed to open VPP"));
    return GST_STATE_CHANGE_FAILURE;
  }
}

static void
gst_va_vpp_set_context (GstElement * element, GstContext * context)
{
  GstVaDisplay *old_display, *new_display;
  GstVaVpp *self = GST_VA_VPP (element);
  GstVaVppClass *klass = GST_VA_VPP_GET_CLASS (self);
  gboolean ret;

  old_display = self->display ? gst_object_ref (self->display) : NULL;
  ret = gst_va_handle_set_context (element, context, klass->render_device_path,
      &self->display);
  new_display = self->display ? gst_object_ref (self->display) : NULL;

  if (!ret
      || (old_display && new_display && old_display != new_display
          && self->filter)) {
    GST_ELEMENT_WARNING (element, RESOURCE, BUSY,
        ("Can't replace VA display while operating"), (NULL));
  }

  gst_clear_object (&old_display);
  gst_clear_object (&new_display);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static GstAllocator *
_create_allocator (GstVaVpp * self, GstCaps * caps)
{
  GstAllocator *allocator = NULL;

  if (gst_caps_is_dmabuf (caps)) {
    allocator = gst_va_dmabuf_allocator_new (self->display);
  } else {
    GArray *surface_formats = gst_va_filter_get_surface_formats (self->filter);
    allocator = gst_va_allocator_new (self->display, surface_formats);
  }

  return allocator;
}

static GstBufferPool *
_create_sinkpad_bufferpool (GstCaps * caps, guint size, guint min_buffers,
    guint max_buffers, guint usage_hint, GstAllocator * allocator,
    GstAllocationParams * alloc_params)
{
  GstBufferPool *pool;
  GstStructure *config;

  pool = gst_va_pool_new ();

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min_buffers,
      max_buffers);
  gst_buffer_pool_config_set_va_allocation_params (config, usage_hint);
  gst_buffer_pool_config_set_allocator (config, allocator, alloc_params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (!gst_buffer_pool_set_config (pool, config))
    gst_clear_object (&pool);

  return pool;
}

/* Answer the allocation query downstream. */
static gboolean
gst_va_vpp_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstVaVpp *self = GST_VA_VPP (trans);
  GstVideoInfo info;
  GstBufferPool *pool;
  GstCaps *caps;
  guint size;

  gst_clear_caps (&self->alloccaps);

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query)) {
    self->forward_crop = FALSE;
    return FALSE;
  }

  self->forward_crop =
      (gst_query_find_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE,
          NULL)
      && gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL));

  gst_query_parse_allocation (query, &caps, NULL);
  if (caps == NULL)
    return FALSE;
  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  self->alloccaps = gst_caps_ref (caps);

  /* passthrough, we're done */
  if (decide_query == NULL)
    return TRUE;

  size = GST_VIDEO_INFO_SIZE (&info);

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstAllocator *allocator = NULL;
    GstAllocationParams params = { 0, };
    gboolean update_allocator = FALSE;
    guint usage_hint = VA_SURFACE_ATTRIB_USAGE_HINT_GENERIC;    /* it migth be used by a va decoder */

    if (gst_query_get_n_allocation_params (query) > 0) {
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
      if (!GST_IS_VA_DMABUF_ALLOCATOR (allocator)
          && !GST_IS_VA_ALLOCATOR (allocator))
        gst_clear_object (&allocator);
      update_allocator = TRUE;
    } else {
      gst_allocation_params_init (&params);
    }

    if (!allocator) {
      if (!(allocator = _create_allocator (self, caps)))
        return FALSE;
    }

    pool = _create_sinkpad_bufferpool (caps, size, 1, 0, usage_hint, allocator,
        &params);
    if (!pool) {
      gst_object_unref (allocator);
      goto config_failed;
    }

    if (update_allocator)
      gst_query_set_nth_allocation_param (query, 0, allocator, &params);
    else
      gst_query_add_allocation_param (query, allocator, &params);

    gst_query_add_allocation_pool (query, pool, size, 1, 0);

    GST_DEBUG_OBJECT (self,
        "proposing %" GST_PTR_FORMAT " with allocator %" GST_PTR_FORMAT,
        pool, allocator);

    gst_object_unref (allocator);
    gst_object_unref (pool);

    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
    gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);
  }

  return TRUE;

  /* ERRORS */
config_failed:
  {
    GST_ERROR_OBJECT (self, "failed to set config");
    return FALSE;
  }
}

static GstBufferPool *
_create_other_pool (GstAllocator * allocator,
    GstAllocationParams * params, GstCaps * caps, guint size)
{
  GstBufferPool *pool = NULL;
  GstStructure *config;

  pool = gst_video_buffer_pool_new ();
  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
  gst_buffer_pool_config_set_allocator (config, allocator, params);
  if (!gst_buffer_pool_set_config (pool, config)) {
    gst_clear_object (&pool);
  }

  return pool;
}

/* configure the allocation query that was answered downstream, we can
 * configure some properties on it. Only called when not in
 * passthrough mode. */
static gboolean
gst_va_vpp_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstVaVpp *self = GST_VA_VPP (trans);
  GstAllocator *allocator = NULL, *other_allocator = NULL;
  GstAllocationParams params, other_params;
  GstBufferPool *pool = NULL, *other_pool = NULL;
  GstCaps *outcaps = NULL;
  GstStructure *config;
  GstVideoInfo vinfo;
  guint min, max, size = 0, usage_hint = VA_SURFACE_ATTRIB_USAGE_HINT_VPP_WRITE;
  gboolean update_pool, update_allocator, has_videometa, copy_frames;

  gst_query_parse_allocation (query, &outcaps, NULL);

  gst_allocation_params_init (&other_params);
  gst_allocation_params_init (&params);

  if (!gst_video_info_from_caps (&vinfo, outcaps)) {
    GST_ERROR_OBJECT (self, "Cannot parse caps %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &other_params);
    if (allocator && !(GST_IS_VA_DMABUF_ALLOCATOR (allocator)
            || GST_IS_VA_ALLOCATOR (allocator))) {
      /* save the allocator for the other pool */
      other_allocator = allocator;
      allocator = NULL;
    }
    update_allocator = TRUE;
  } else {
    update_allocator = FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    if (pool) {
      if (!GST_IS_VA_POOL (pool)) {
        GST_DEBUG_OBJECT (self,
            "may need other pool for copy frames %" GST_PTR_FORMAT, pool);
        other_pool = pool;
        pool = NULL;
      }
    }

    update_pool = TRUE;
  } else {
    size = GST_VIDEO_INFO_SIZE (&vinfo);
    min = 1;
    max = 0;
    update_pool = FALSE;
  }

  if (!allocator) {
    /* XXX(victor): VPP_WRITE uses a tiled drm modifier by iHD */
    if (gst_caps_is_dmabuf (outcaps) && GST_VIDEO_INFO_IS_RGB (&vinfo))
      usage_hint = VA_SURFACE_ATTRIB_USAGE_HINT_GENERIC;
    if (!(allocator = _create_allocator (self, outcaps)))
      return FALSE;
  }

  if (!pool)
    pool = gst_va_pool_new ();

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_allocator (config, allocator, &params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_set_va_allocation_params (config, usage_hint);
  gst_buffer_pool_set_config (pool, config);

  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);

  if (GST_IS_VA_DMABUF_ALLOCATOR (allocator)) {
    gst_va_dmabuf_allocator_get_format (allocator, &vinfo, NULL);
  } else if (GST_IS_VA_ALLOCATOR (allocator)) {
    gst_va_allocator_get_format (allocator, &vinfo, NULL);
  }
  self->srcpad_info = vinfo;

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  has_videometa = gst_query_find_allocation_meta (query,
      GST_VIDEO_META_API_TYPE, NULL);

  copy_frames = (!has_videometa && gst_va_pool_requires_video_meta (pool)
      && gst_caps_is_raw (outcaps));
  if (copy_frames) {
    if (other_pool) {
      gst_object_replace ((GstObject **) & self->other_pool,
          (GstObject *) other_pool);
    } else {
      self->other_pool =
          _create_other_pool (other_allocator, &other_params, outcaps, size);
    }
    GST_DEBUG_OBJECT (self, "Use the other pool for copy %" GST_PTR_FORMAT,
        self->other_pool);
  } else {
    gst_clear_object (&self->other_pool);
  }

  GST_DEBUG_OBJECT (self,
      "decided pool %" GST_PTR_FORMAT " with allocator %" GST_PTR_FORMAT,
      pool, allocator);

  gst_object_unref (allocator);
  gst_object_unref (pool);
  gst_clear_object (&other_allocator);
  gst_clear_object (&other_pool);

  /* removes allocation metas */
  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
gst_va_vpp_query (GstBaseTransform * trans, GstPadDirection direction,
    GstQuery * query)
{
  GstVaVpp *self = GST_VA_VPP (trans);
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      GstVaDisplay *display = NULL;

      gst_object_replace ((GstObject **) & display,
          (GstObject *) self->display);
      ret = gst_va_handle_context_query (GST_ELEMENT_CAST (self), query,
          display);
      gst_clear_object (&display);
      break;
    }
    default:
      ret = GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
          query);
      break;
  }

  return ret;
}

/* output buffers must be from our VA-based pool, they cannot be
 * system-allocated */
static gboolean
gst_va_vpp_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, gsize size,
    GstCaps * othercaps, gsize * othersize)
{
  return FALSE;
}

static gboolean
gst_va_vpp_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVaVpp *self = GST_VA_VPP (trans);
  GstVideoInfo in_info, out_info;
  GstCapsFeatures *infeat, *outfeat;

  /* input caps */
  if (!gst_video_info_from_caps (&in_info, incaps))
    goto invalid_caps;

  /* output caps */
  if (!gst_video_info_from_caps (&out_info, outcaps))
    goto invalid_caps;

  if (!gst_video_info_is_equal (&in_info, &out_info)) {
    if (GST_VIDEO_INFO_FORMAT (&in_info) != GST_VIDEO_INFO_FORMAT (&out_info))
      self->op_flags |= VPP_CONVERT_FORMAT;
    else
      self->op_flags &= ~VPP_CONVERT_FORMAT;

    if (GST_VIDEO_INFO_WIDTH (&in_info) != GST_VIDEO_INFO_WIDTH (&out_info)
        || GST_VIDEO_INFO_HEIGHT (&in_info) !=
        GST_VIDEO_INFO_HEIGHT (&out_info))
      self->op_flags |= VPP_CONVERT_SIZE;
    else
      self->op_flags &= ~VPP_CONVERT_SIZE;
  } else {
    self->op_flags &= ~VPP_CONVERT_FORMAT & ~VPP_CONVERT_SIZE;
  }

  infeat = gst_caps_get_features (incaps, 0);
  outfeat = gst_caps_get_features (outcaps, 0);
  if (!gst_caps_features_is_equal (infeat, outfeat))
    self->op_flags |= VPP_CONVERT_FEATURE;
  else
    self->op_flags &= ~VPP_CONVERT_FEATURE;

  if (self->sinkpad_pool) {
    gst_buffer_pool_set_active (self->sinkpad_pool, FALSE);
    gst_clear_object (&self->sinkpad_pool);
  }

  if (self->other_pool) {
    gst_buffer_pool_set_active (self->other_pool, FALSE);
    gst_clear_object (&self->other_pool);
  }

  gst_caps_replace (&self->incaps, incaps);
  gst_caps_replace (&self->outcaps, outcaps);

  self->in_info = in_info;
  self->out_info = out_info;

  self->negotiated =
      gst_va_filter_set_formats (self->filter, &self->in_info, &self->out_info);

  if (self->negotiated)
    gst_va_vpp_update_passthrough (self, FALSE);

  return self->negotiated;

  /* ERRORS */
invalid_caps:
  {
    GST_ERROR_OBJECT (self, "invalid caps");
    self->negotiated = FALSE;
    return FALSE;
  }
}

static inline gboolean
_get_filter_value (GstVaVpp * self, VAProcFilterType type, gfloat * value)
{
  gboolean ret = TRUE;

  GST_OBJECT_LOCK (self);
  switch (type) {
    case VAProcFilterNoiseReduction:
      *value = self->denoise;
      break;
    case VAProcFilterSharpening:
      *value = self->sharpen;
      break;
    case VAProcFilterSkinToneEnhancement:
      *value = self->skintone;
      break;
    default:
      ret = FALSE;
      break;
  }
  GST_OBJECT_UNLOCK (self);

  return ret;
}

static inline gboolean
_add_filter_buffer (GstVaVpp * self, VAProcFilterType type,
    const VAProcFilterCap * cap)
{
  VAProcFilterParameterBuffer param;
  gfloat value = 0;

  if (!_get_filter_value (self, type, &value))
    return FALSE;
  if (value == cap->range.default_value)
    return FALSE;

  /* *INDENT-OFF* */
  param = (VAProcFilterParameterBuffer) {
    .type = type,
    .value = value,
  };
  /* *INDENT-ON* */

  return
      gst_va_filter_add_filter_buffer (self->filter, &param, sizeof (param), 1);
}

static inline gboolean
_get_filter_cb_value (GstVaVpp * self, VAProcColorBalanceType type,
    gfloat * value)
{
  gboolean ret = TRUE;

  GST_OBJECT_LOCK (self);
  switch (type) {
    case VAProcColorBalanceHue:
      *value = self->hue;
      break;
    case VAProcColorBalanceSaturation:
      *value = self->saturation;
      break;
    case VAProcColorBalanceBrightness:
      *value = self->brightness;
      break;
    case VAProcColorBalanceContrast:
      *value = self->contrast;
      break;
    case VAProcColorBalanceAutoSaturation:
      *value = self->auto_saturation;
      break;
    case VAProcColorBalanceAutoBrightness:
      *value = self->auto_brightness;
      break;
    case VAProcColorBalanceAutoContrast:
      *value = self->auto_contrast;
      break;
    default:
      ret = FALSE;
      break;
  }
  GST_OBJECT_UNLOCK (self);

  return ret;
}

static inline gboolean
_add_filter_cb_buffer (GstVaVpp * self,
    const VAProcFilterCapColorBalance * caps, guint num_caps)
{
  VAProcFilterParameterBufferColorBalance param[VAProcColorBalanceCount] =
      { 0, };
  gfloat value;
  guint i, c = 0;

  value = 0;
  for (i = 0; i < num_caps && i < VAProcColorBalanceCount; i++) {
    if (!_get_filter_cb_value (self, caps[i].type, &value))
      continue;
    if (value == caps[i].range.default_value)
      continue;

    /* *INDENT-OFF* */
    param[c++] = (VAProcFilterParameterBufferColorBalance) {
      .type = VAProcFilterColorBalance,
      .attrib = caps[i].type,
      .value = value,
    };
    /* *INDENT-ON* */
  }

  if (c > 0) {
    return gst_va_filter_add_filter_buffer (self->filter, param,
        sizeof (*param), c);
  }
  return FALSE;
}

static void
_build_filters (GstVaVpp * self)
{
  static const VAProcFilterType filter_types[] = { VAProcFilterNoiseReduction,
    VAProcFilterSharpening, VAProcFilterSkinToneEnhancement,
    VAProcFilterColorBalance,
  };
  guint i, num_caps;
  gboolean apply = FALSE;

  for (i = 0; i < G_N_ELEMENTS (filter_types); i++) {
    const gpointer caps = gst_va_filter_get_filter_caps (self->filter,
        filter_types[i], &num_caps);
    if (!caps)
      continue;

    switch (filter_types[i]) {
      case VAProcFilterNoiseReduction:
        apply |= _add_filter_buffer (self, filter_types[i], caps);
        break;
      case VAProcFilterSharpening:
        apply |= _add_filter_buffer (self, filter_types[i], caps);
        break;
      case VAProcFilterSkinToneEnhancement:
        apply |= _add_filter_buffer (self, filter_types[i], caps);
        break;
      case VAProcFilterColorBalance:
        apply |= _add_filter_cb_buffer (self, caps, num_caps);
        break;
      default:
        break;
    }
  }

  GST_OBJECT_LOCK (self);
  if (apply)
    self->op_flags |= VPP_CONVERT_FILTERS;
  else
    self->op_flags &= ~VPP_CONVERT_FILTERS;
  GST_OBJECT_UNLOCK (self);
}

static void
gst_va_vpp_rebuild_filters (GstVaVpp * self)
{
  if (!g_atomic_int_get (&self->rebuild_filters))
    return;

  gst_va_filter_drop_filter_buffers (self->filter);
  _build_filters (self);
  g_atomic_int_set (&self->rebuild_filters, FALSE);
}

static void
gst_va_vpp_before_transform (GstBaseTransform * trans, GstBuffer * inbuf)
{
  GstVaVpp *self = GST_VA_VPP (trans);
  GstClockTime ts, stream_time;

  ts = GST_BUFFER_TIMESTAMP (inbuf);
  stream_time =
      gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME, ts);

  GST_TRACE_OBJECT (self, "sync to %" GST_TIME_FORMAT, GST_TIME_ARGS (ts));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT (self), stream_time);

  GST_OBJECT_LOCK (self);
  if (gst_buffer_get_video_crop_meta (inbuf)) {
    /* enable cropping if either already do operations on frame or
     * downstream doesn't support cropping */
    if (self->op_flags == 0 && self->forward_crop) {
      self->op_flags &= ~VPP_CONVERT_CROP;
    } else {
      self->op_flags |= VPP_CONVERT_CROP;
    }
  } else {
    self->op_flags &= ~VPP_CONVERT_CROP;
  }
  gst_va_filter_enable_cropping (self->filter,
      (self->op_flags & VPP_CONVERT_CROP));
  GST_OBJECT_UNLOCK (self);

  gst_va_vpp_rebuild_filters (self);
  gst_va_vpp_update_passthrough (self, TRUE);
}

static inline gsize
_get_plane_data_size (GstVideoInfo * info, guint plane)
{
  gint height, padded_height;

  height = GST_VIDEO_INFO_HEIGHT (info);
  padded_height =
      GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (info->finfo, plane, height);

  return GST_VIDEO_INFO_PLANE_STRIDE (info, plane) * padded_height;
}

static gboolean
_try_import_dmabuf_unlocked (GstVaVpp * self, GstBuffer * inbuf)
{
  GstVideoMeta *meta;
  GstVideoInfo in_info = self->in_info;
  GstMemory *mems[GST_VIDEO_MAX_PLANES];
  guint i, n_mem, n_planes;
  gsize offset[GST_VIDEO_MAX_PLANES];
  uintptr_t fd[GST_VIDEO_MAX_PLANES];

  n_planes = GST_VIDEO_INFO_N_PLANES (&in_info);
  n_mem = gst_buffer_n_memory (inbuf);
  meta = gst_buffer_get_video_meta (inbuf);

  /* This will eliminate most non-dmabuf out there */
  if (!gst_is_dmabuf_memory (gst_buffer_peek_memory (inbuf, 0)))
    return FALSE;

  /* We cannot have multiple dmabuf per plane */
  if (n_mem > n_planes)
    return FALSE;

  /* Update video info based on video meta */
  if (meta) {
    GST_VIDEO_INFO_WIDTH (&in_info) = meta->width;
    GST_VIDEO_INFO_HEIGHT (&in_info) = meta->height;

    for (i = 0; i < meta->n_planes; i++) {
      GST_VIDEO_INFO_PLANE_OFFSET (&in_info, i) = meta->offset[i];
      GST_VIDEO_INFO_PLANE_STRIDE (&in_info, i) = meta->stride[i];
    }
  }

  /* Find and validate all memories */
  for (i = 0; i < n_planes; i++) {
    guint plane_size;
    guint length;
    guint mem_idx;
    gsize mem_skip;

    plane_size = _get_plane_data_size (&in_info, i);

    if (!gst_buffer_find_memory (inbuf, in_info.offset[i], plane_size,
            &mem_idx, &length, &mem_skip))
      return FALSE;

    /* We can't have more then one dmabuf per plane */
    if (length != 1)
      return FALSE;

    mems[i] = gst_buffer_peek_memory (inbuf, mem_idx);

    /* And all memory found must be dmabuf */
    if (!gst_is_dmabuf_memory (mems[i]))
      return FALSE;

    offset[i] = mems[i]->offset + mem_skip;
    fd[i] = gst_dmabuf_memory_get_fd (mems[i]);
  }

  /* Now create a VASurfaceID for the buffer */
  return gst_va_dmabuf_memories_setup (self->display, &in_info, n_planes, mems,
      fd, offset, VA_SURFACE_ATTRIB_USAGE_HINT_VPP_READ);
}

static GstBufferPool *
_get_sinkpad_pool (GstVaVpp * self)
{
  GstAllocator *allocator;
  GstAllocationParams params;
  GstCaps *caps;
  GstVideoInfo alloc_info, in_info;
  guint size, usage_hint = VA_SURFACE_ATTRIB_USAGE_HINT_VPP_READ;

  if (self->sinkpad_pool)
    return self->sinkpad_pool;

  gst_allocation_params_init (&params);

  if (self->alloccaps) {
    caps = self->alloccaps;
    gst_video_info_from_caps (&in_info, caps);
  } else {
    caps = self->incaps;
    in_info = self->in_info;
  }

  size = GST_VIDEO_INFO_SIZE (&in_info);

  allocator = _create_allocator (self, caps);

  self->sinkpad_pool = _create_sinkpad_bufferpool (caps, size, 1, 0, usage_hint,
      allocator, &params);

  if (GST_IS_VA_DMABUF_ALLOCATOR (allocator)) {
    if (!gst_va_dmabuf_allocator_get_format (allocator, &alloc_info, NULL))
      alloc_info = in_info;
  } else if (GST_IS_VA_ALLOCATOR (allocator)) {
    if (!gst_va_allocator_get_format (allocator, &alloc_info, NULL))
      alloc_info = in_info;
  }

  gst_object_unref (allocator);

  if (self->sinkpad_pool) {
    self->sinkpad_info = alloc_info;
    gst_buffer_pool_set_active (self->sinkpad_pool, TRUE);
  }

  return self->sinkpad_pool;
}

static gboolean
_try_import_buffer (GstVaVpp * self, GstBuffer * inbuf)
{
  VASurfaceID surface;
  gboolean ret;

  surface = gst_va_buffer_get_surface (inbuf);
  if (surface != VA_INVALID_ID)
    return TRUE;

  g_rec_mutex_lock (&GST_VA_SHARED_LOCK);
  ret = _try_import_dmabuf_unlocked (self, inbuf);
  g_rec_mutex_unlock (&GST_VA_SHARED_LOCK);

  return ret;
}

static GstFlowReturn
gst_va_vpp_import_input_buffer (GstVaVpp * self, GstBuffer * inbuf,
    GstBuffer ** buf)
{
  GstBuffer *buffer = NULL;
  GstBufferPool *pool;
  GstFlowReturn ret;
  GstVideoFrame in_frame, out_frame;
  gboolean imported, copied;

  imported = _try_import_buffer (self, inbuf);
  if (imported) {
    *buf = gst_buffer_ref (inbuf);
    return GST_FLOW_OK;
  }

  /* input buffer doesn't come from a vapool, thus it is required to
   * have a pool, grab from it a new buffer and copy the input
   * buffer to the new one */
  if (!(pool = _get_sinkpad_pool (self)))
    return GST_FLOW_ERROR;

  ret = gst_buffer_pool_acquire_buffer (pool, &buffer, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  GST_LOG_OBJECT (self, "copying input frame");

  if (!gst_video_frame_map (&in_frame, &self->in_info, inbuf, GST_MAP_READ))
    goto invalid_buffer;

  if (!gst_video_frame_map (&out_frame, &self->sinkpad_info, buffer,
          GST_MAP_WRITE)) {
    gst_video_frame_unmap (&in_frame);
    goto invalid_buffer;
  }

  copied = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  if (!copied)
    goto invalid_buffer;

  /* strictly speaking this is not needed but let's play safe */
  if (!gst_buffer_copy_into (buffer, inbuf, GST_BUFFER_COPY_FLAGS |
          GST_BUFFER_COPY_TIMESTAMPS, 0, -1))
    return GST_FLOW_ERROR;

  *buf = buffer;

  return GST_FLOW_OK;

invalid_buffer:
  {
    GST_ELEMENT_WARNING (self, CORE, NOT_IMPLEMENTED, (NULL),
        ("invalid video buffer received"));
    if (buffer)
      gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_va_vpp_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVaVpp *self = GST_VA_VPP (trans);
  GstBuffer *buf = NULL;
  GstFlowReturn res = GST_FLOW_OK;
  GstVaSample src, dst;

  if (G_UNLIKELY (!self->negotiated))
    goto unknown_format;

  res = gst_va_vpp_import_input_buffer (self, inbuf, &buf);
  if (res != GST_FLOW_OK)
    return res;

  /* *INDENT-OFF* */
  src = (GstVaSample) {
    .buffer = buf,
  };

  dst = (GstVaSample) {
    .buffer = outbuf,
  };
  /* *INDENT-ON* */

  if (!gst_va_filter_convert_surface (self->filter, &src, &dst)) {
    gst_buffer_set_flags (outbuf, GST_BUFFER_FLAG_CORRUPTED);
  }

  gst_buffer_unref (buf);

  return res;

  /* ERRORS */
unknown_format:
  {
    GST_ELEMENT_ERROR (self, CORE, NOT_IMPLEMENTED, (NULL), ("unknown format"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static gboolean
gst_va_vpp_transform_meta (GstBaseTransform * trans, GstBuffer * inbuf,
    GstMeta * meta, GstBuffer * outbuf)
{
  GstVaVpp *self = GST_VA_VPP (trans);
  const GstMetaInfo *info = meta->info;
  const gchar *const *tags;

  tags = gst_meta_api_type_get_tags (info->api);

  if (!tags)
    return TRUE;

  /* don't copy colorspace/size/orientation specific metadata */
  if ((self->op_flags & VPP_CONVERT_FORMAT)
      && gst_meta_api_type_has_tag (info->api, META_TAG_COLORSPACE))
    return FALSE;
  else if ((self->op_flags & (VPP_CONVERT_SIZE | VPP_CONVERT_CROP))
      && gst_meta_api_type_has_tag (info->api, META_TAG_SIZE))
    return FALSE;
  else if ((self->op_flags & VPP_CONVERT_DIRECTION)
      && gst_meta_api_type_has_tag (info->api, META_TAG_ORIENTATION))
    return FALSE;
  else if (gst_meta_api_type_has_tag (info->api, META_TAG_VIDEO))
    return TRUE;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans, outbuf,
      meta, inbuf);
}

/* Remove all the info for the cases when we can actually convert:
 * Delete all the video "format", rangify the resolution size, also
 * remove "colorimetry", "chroma-site" and "pixel-aspect-ratio".  All
 * the missing caps features should be added based on the template,
 * and the caps features' order in @caps is kept */
static GstCaps *
gst_va_vpp_complete_caps_features (GstCaps * caps, GstCaps * tmpl_caps)
{
  GstCaps *ret, *full_caps;
  GstStructure *structure;
  GstCapsFeatures *features;
  gboolean has_sys_mem = FALSE, has_dma = FALSE, has_va = FALSE;
  gint i, n;

  full_caps = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure (caps, i);
    features = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0
        && gst_caps_is_subset_structure_full (full_caps, structure, features))
      continue;

    if (gst_caps_features_is_any (features))
      continue;

    if (gst_caps_features_is_equal (features,
            GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY)) {
      has_sys_mem = TRUE;
    } else {
      gboolean valid = FALSE;

      if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_DMABUF)) {
        has_dma = TRUE;
        valid = TRUE;
      }
      if (gst_caps_features_contains (features, "memory:VAMemory")) {
        has_va = TRUE;
        valid = TRUE;
      }
      /* Not contain our supported feature */
      if (!valid)
        continue;
    }

    structure = gst_structure_copy (structure);

    gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
    /* if pixel aspect ratio, make a range of it */
    if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
    }
    gst_structure_remove_fields (structure, "format", "colorimetry",
        "chroma-site", NULL);

    gst_caps_append_structure_full (full_caps, structure,
        gst_caps_features_copy (features));
  }

  /* Adding the missing features. */
  n = gst_caps_get_size (tmpl_caps);
  for (i = 0; i < n; i++) {
    structure = gst_caps_get_structure (tmpl_caps, i);
    features = gst_caps_get_features (tmpl_caps, i);

    if (gst_caps_features_contains (features, "memory:VAMemory") && !has_va)
      gst_caps_append_structure_full (full_caps, gst_structure_copy (structure),
          gst_caps_features_copy (features));

    if (gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_DMABUF) && !has_dma)
      gst_caps_append_structure_full (full_caps, gst_structure_copy (structure),
          gst_caps_features_copy (features));

    if (gst_caps_features_is_equal (features,
            GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY) && !has_sys_mem)
      gst_caps_append_structure_full (full_caps, gst_structure_copy (structure),
          gst_caps_features_copy (features));
  }

  ret = gst_caps_intersect_full (full_caps, tmpl_caps,
      GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (full_caps);

  return ret;
}

static GstCaps *
gst_va_vpp_transform_caps (GstBaseTransform * trans, GstPadDirection direction,
    GstCaps * caps, GstCaps * filter)
{
  GstVaVpp *self = GST_VA_VPP (trans);
  GstCaps *ret, *tmpl_caps;

  GST_DEBUG_OBJECT (self,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SINK) {
    tmpl_caps =
        gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SRC_PAD (trans));
  } else {
    tmpl_caps =
        gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SINK_PAD (trans));
  }

  ret = gst_va_vpp_complete_caps_features (caps, tmpl_caps);

  gst_caps_unref (tmpl_caps);

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = intersection;
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

/*
 * This is an incomplete matrix of in formats and a score for the preferred output
 * format.
 *
 *         out: RGB24   RGB16  ARGB  AYUV  YUV444  YUV422 YUV420 YUV411 YUV410  PAL  GRAY
 *  in
 * RGB24          0      2       1     2     2       3      4      5      6      7    8
 * RGB16          1      0       1     2     2       3      4      5      6      7    8
 * ARGB           2      3       0     1     4       5      6      7      8      9    10
 * AYUV           3      4       1     0     2       5      6      7      8      9    10
 * YUV444         2      4       3     1     0       5      6      7      8      9    10
 * YUV422         3      5       4     2     1       0      6      7      8      9    10
 * YUV420         4      6       5     3     2       1      0      7      8      9    10
 * YUV411         4      6       5     3     2       1      7      0      8      9    10
 * YUV410         6      8       7     5     4       3      2      1      0      9    10
 * PAL            1      3       2     6     4       6      7      8      9      0    10
 * GRAY           1      4       3     2     1       5      6      7      8      9    0
 *
 * PAL or GRAY are never preferred, if we can we would convert to PAL instead
 * of GRAY, though
 * less subsampling is preferred and if any, preferably horizontal
 * We would like to keep the alpha, even if we would need to to colorspace conversion
 * or lose depth.
 */
#define SCORE_FORMAT_CHANGE       1
#define SCORE_DEPTH_CHANGE        1
#define SCORE_ALPHA_CHANGE        1
#define SCORE_CHROMA_W_CHANGE     1
#define SCORE_CHROMA_H_CHANGE     1
#define SCORE_PALETTE_CHANGE      1

#define SCORE_COLORSPACE_LOSS     2     /* RGB <-> YUV */
#define SCORE_DEPTH_LOSS          4     /* change bit depth */
#define SCORE_ALPHA_LOSS          8     /* lose the alpha channel */
#define SCORE_CHROMA_W_LOSS      16     /* vertical subsample */
#define SCORE_CHROMA_H_LOSS      32     /* horizontal subsample */
#define SCORE_PALETTE_LOSS       64     /* convert to palette format */
#define SCORE_COLOR_LOSS        128     /* convert to GRAY */

#define COLORSPACE_MASK (GST_VIDEO_FORMAT_FLAG_YUV | \
                         GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_GRAY)
#define ALPHA_MASK      (GST_VIDEO_FORMAT_FLAG_ALPHA)
#define PALETTE_MASK    (GST_VIDEO_FORMAT_FLAG_PALETTE)

/* calculate how much loss a conversion would be */
static void
score_value (GstVaVpp * self, const GstVideoFormatInfo * in_info,
    const GValue * val, gint * min_loss, const GstVideoFormatInfo ** out_info)
{
  const gchar *fname;
  const GstVideoFormatInfo *t_info;
  GstVideoFormatFlags in_flags, t_flags;
  gint loss;

  fname = g_value_get_string (val);
  t_info = gst_video_format_get_info (gst_video_format_from_string (fname));
  if (!t_info || t_info->format == GST_VIDEO_FORMAT_UNKNOWN)
    return;

  /* accept input format immediately without loss */
  if (in_info == t_info) {
    *min_loss = 0;
    *out_info = t_info;
    return;
  }

  loss = SCORE_FORMAT_CHANGE;

  in_flags = GST_VIDEO_FORMAT_INFO_FLAGS (in_info);
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

  t_flags = GST_VIDEO_FORMAT_INFO_FLAGS (t_info);
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

  if ((t_flags & PALETTE_MASK) != (in_flags & PALETTE_MASK)) {
    loss += SCORE_PALETTE_CHANGE;
    if (t_flags & PALETTE_MASK)
      loss += SCORE_PALETTE_LOSS;
  }

  if ((t_flags & COLORSPACE_MASK) != (in_flags & COLORSPACE_MASK)) {
    loss += SCORE_COLORSPACE_LOSS;
    if (t_flags & GST_VIDEO_FORMAT_FLAG_GRAY)
      loss += SCORE_COLOR_LOSS;
  }

  if ((t_flags & ALPHA_MASK) != (in_flags & ALPHA_MASK)) {
    loss += SCORE_ALPHA_CHANGE;
    if (in_flags & ALPHA_MASK)
      loss += SCORE_ALPHA_LOSS;
  }

  if ((in_info->h_sub[1]) != (t_info->h_sub[1])) {
    loss += SCORE_CHROMA_H_CHANGE;
    if ((in_info->h_sub[1]) < (t_info->h_sub[1]))
      loss += SCORE_CHROMA_H_LOSS;
  }
  if ((in_info->w_sub[1]) != (t_info->w_sub[1])) {
    loss += SCORE_CHROMA_W_CHANGE;
    if ((in_info->w_sub[1]) < (t_info->w_sub[1]))
      loss += SCORE_CHROMA_W_LOSS;
  }

  if ((in_info->bits) != (t_info->bits)) {
    loss += SCORE_DEPTH_CHANGE;
    if ((in_info->bits) > (t_info->bits))
      loss += SCORE_DEPTH_LOSS;
  }

  GST_DEBUG_OBJECT (self, "score %s -> %s = %d",
      GST_VIDEO_FORMAT_INFO_NAME (in_info),
      GST_VIDEO_FORMAT_INFO_NAME (t_info), loss);

  if (loss < *min_loss) {
    GST_DEBUG_OBJECT (self, "found new best %d", loss);
    *out_info = t_info;
    *min_loss = loss;
  }
}

static void
gst_va_vpp_fixate_format (GstVaVpp * self, GstCaps * caps, GstCaps * result)
{
  GstStructure *ins, *outs;
  const gchar *in_format;
  const GstVideoFormatInfo *in_info, *out_info = NULL;
  gint min_loss = G_MAXINT;
  guint i, capslen;

  ins = gst_caps_get_structure (caps, 0);
  in_format = gst_structure_get_string (ins, "format");
  if (!in_format)
    return;

  GST_DEBUG_OBJECT (self, "source format %s", in_format);

  in_info =
      gst_video_format_get_info (gst_video_format_from_string (in_format));
  if (!in_info)
    return;

  outs = gst_caps_get_structure (result, 0);

  capslen = gst_caps_get_size (result);
  GST_DEBUG_OBJECT (self, "iterate %d structures", capslen);
  for (i = 0; i < capslen; i++) {
    GstStructure *tests;
    const GValue *format;

    tests = gst_caps_get_structure (result, i);
    format = gst_structure_get_value (tests, "format");
    gst_structure_remove_fields (tests, "height", "width", "pixel-aspect-ratio",
        "display-aspect-ratio", NULL);
    /* should not happen */
    if (format == NULL)
      continue;

    if (GST_VALUE_HOLDS_LIST (format)) {
      gint j, len;

      len = gst_value_list_get_size (format);
      GST_DEBUG_OBJECT (self, "have %d formats", len);
      for (j = 0; j < len; j++) {
        const GValue *val;

        val = gst_value_list_get_value (format, j);
        if (G_VALUE_HOLDS_STRING (val)) {
          score_value (self, in_info, val, &min_loss, &out_info);
          if (min_loss == 0)
            break;
        }
      }
    } else if (G_VALUE_HOLDS_STRING (format)) {
      score_value (self, in_info, format, &min_loss, &out_info);
    }
  }
  if (out_info)
    gst_structure_set (outs, "format", G_TYPE_STRING,
        GST_VIDEO_FORMAT_INFO_NAME (out_info), NULL);
}

static GstCaps *
gst_va_vpp_get_fixed_format (GstVaVpp * self, GstPadDirection direction,
    GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *result;

  result = gst_caps_intersect (othercaps, caps);
  if (gst_caps_is_empty (result)) {
    gst_caps_unref (result);
    result = gst_caps_copy (othercaps);
  }

  gst_va_vpp_fixate_format (self, caps, result);

  /* fixate remaining fields */
  result = gst_caps_fixate (result);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result)) {
      gst_caps_replace (&result, caps);
    }
  }

  return result;
}

static GstCaps *
gst_va_vpp_fixate_size (GstVaVpp * self, GstPadDirection direction,
    GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = { 0, };
  GValue tpar = { 0, };

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);
  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  /* If we're fixating from the sinkpad we always set the PAR and
   * assume that missing PAR on the sinkpad means 1/1 and
   * missing PAR on the srcpad means undefined
   */
  if (direction == GST_PAD_SINK) {
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION_RANGE);
      gst_value_set_fraction_range_full (&tpar, 1, G_MAXINT, G_MAXINT, 1);
      to_par = &tpar;
    }
  } else {
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&tpar, 1, 1);
      to_par = &tpar;

      gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
          NULL);
    }
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
  }

  /* we have both PAR but they might not be fixated */
  {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint w = 0, h = 0;
    gint from_dar_n, from_dar_d;
    gint num, den;

    /* from_par should be fixed */
    g_return_val_if_fail (gst_value_is_fixed (from_par), othercaps);

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    gst_structure_get_int (outs, "width", &w);
    gst_structure_get_int (outs, "height", &h);

    /* if video-orientation changes */
    switch (gst_va_filter_get_orientation (self->filter)) {
      case GST_VIDEO_ORIENTATION_90R:
      case GST_VIDEO_ORIENTATION_90L:
      case GST_VIDEO_ORIENTATION_UL_LR:
      case GST_VIDEO_ORIENTATION_UR_LL:
        if (direction == GST_PAD_SINK) {
          SWAP (from_w, from_h);
          SWAP (from_par_n, from_par_d);
        } else if (direction == GST_PAD_SRC) {
          SWAP (w, h);
          /* there's no need to swap 1/1 par */
        }
        break;
      default:
        break;
    }

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (w && h) {
      guint n, d;

      GST_DEBUG_OBJECT (self, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        if (gst_video_calculate_display_ratio (&n, &d, from_w, from_h,
                from_par_n, from_par_d, w, h)) {
          GST_DEBUG_OBJECT (self, "fixating to_par to %dx%d", n, d);
          if (gst_structure_has_field (outs, "pixel-aspect-ratio"))
            gst_structure_fixate_field_nearest_fraction (outs,
                "pixel-aspect-ratio", n, d);
          else if (n != d)
            gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                n, d, NULL);
        }
      }
      goto done;
    }

    /* Calculate input DAR */
    if (!gst_util_fraction_multiply (from_w, from_h, from_par_n, from_par_d,
            &from_dar_n, &from_dar_d)) {
      GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      goto done;
    }

    GST_DEBUG_OBJECT (self, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      GstStructure *tmp;
      gint set_w, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (self, "height is fixed (%d)", h);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the width that is nearest to the
       * width with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (self, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        w = (guint) gst_util_uint64_scale_int_round (h, num, den);
        gst_structure_fixate_field_nearest_int (outs, "width", w);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input width */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "width", G_TYPE_INT, set_w,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the width to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int_round (h, num, den);
      gst_structure_fixate_field_nearest_int (outs, "width", w);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (w) {
      GstStructure *tmp;
      gint set_h, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (self, "width is fixed (%d)", w);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the height that is nearest to the
       * height with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (self, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        h = (guint) gst_util_uint64_scale_int_round (w, den, num);
        gst_structure_fixate_field_nearest_int (outs, "height", h);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input height */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }
      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "height", G_TYPE_INT, set_h,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the height to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scale sized - integer overflow"));
        goto done;
      }

      h = (guint) gst_util_uint64_scale_int_round (w, den, num);
      gst_structure_fixate_field_nearest_int (outs, "height", h);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (gst_value_is_fixed (to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;

      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_n,
              to_par_d, &num, &den)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      /* Try to keep the input height (because of interlacing) */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      w = (guint) gst_util_uint64_scale_int_round (set_h, num, den);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &set_w);
      gst_structure_free (tmp);

      /* We kept the DAR and the height is nearest to the original height */
      if (set_w == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      f_h = set_h;
      f_w = set_w;

      /* If the former failed, try to keep the input width at least */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      h = (guint) gst_util_uint64_scale_int_round (set_w, den, num);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_free (tmp);

      /* We kept the DAR and the width is nearest to the original width */
      if (set_h == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      /* If all this failed, keep the dimensions with the DAR that was closest
       * to the correct DAR. This changes the DAR but there's not much else to
       * do here.
       */
      if (set_w * ABS (set_h - h) < ABS (f_w - w) * f_h) {
        f_h = set_h;
        f_w = set_w;
      }
      gst_structure_set (outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT,
          f_h, NULL);
      goto done;
    } else {
      GstStructure *tmp;
      gint set_h, set_w, set_par_n, set_par_d, tmp2;

      /* width, height and PAR are not fixed but passthrough is not possible */

      /* First try to keep the height and width as good as possible
       * and scale PAR */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);

        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* Otherwise try to scale width to keep the DAR with the set
       * PAR and height */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int_round (set_h, num, den);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, tmp2, "height",
            G_TYPE_INT, set_h, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* ... or try the same with the height */
      h = (guint) gst_util_uint64_scale_int_round (set_w, den, num);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, tmp2, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* If all fails we can't keep the DAR and take the nearest values
       * for everything from the first try */
      gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, NULL);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);
    }
  }

done:
  if (from_par == &fpar)
    g_value_unset (&fpar);
  if (to_par == &tpar)
    g_value_unset (&tpar);

  return othercaps;
}

static GstCaps *
gst_va_vpp_fixate_caps (GstBaseTransform * trans, GstPadDirection direction,
    GstCaps * caps, GstCaps * othercaps)
{
  GstVaVpp *self = GST_VA_VPP (trans);
  GstCaps *format;

  GST_DEBUG_OBJECT (self,
      "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %"
      GST_PTR_FORMAT, othercaps, caps);

  format = gst_va_vpp_get_fixed_format (self, direction, caps, othercaps);

  if (gst_caps_is_empty (format)) {
    GST_ERROR_OBJECT (self, "Could not convert formats");
    return format;
  }

  othercaps = gst_va_vpp_fixate_size (self, direction, caps, othercaps);
  if (gst_caps_get_size (othercaps) == 1) {
    gint i;
    const gchar *format_fields[] = { "format", "colorimetry", "chroma-site" };
    GstStructure *format_struct = gst_caps_get_structure (format, 0);
    GstStructure *fixated_struct;

    othercaps = gst_caps_make_writable (othercaps);
    fixated_struct = gst_caps_get_structure (othercaps, 0);

    for (i = 0; i < G_N_ELEMENTS (format_fields); i++) {
      if (gst_structure_has_field (format_struct, format_fields[i])) {
        gst_structure_set (fixated_struct, format_fields[i], G_TYPE_STRING,
            gst_structure_get_string (format_struct, format_fields[i]), NULL);
      } else {
        gst_structure_remove_field (fixated_struct, format_fields[i]);
      }
    }

    /* copy the framerate */
    {
      const GValue *framerate =
          gst_structure_get_value (fixated_struct, "framerate");
      if (framerate && !gst_value_is_fixed (framerate)) {
        GstStructure *st = gst_caps_get_structure (caps, 0);
        const GValue *fixated_framerate =
            gst_structure_get_value (st, "framerate");
        gst_structure_set_value (fixated_struct, "framerate",
            fixated_framerate);
      }
    }
  }
  gst_caps_unref (format);

  GST_DEBUG_OBJECT (self, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  return othercaps;
}

static void
_get_scale_factor (GstVaVpp * self, gdouble * w_factor, gdouble * h_factor)
{
  gdouble w = GST_VIDEO_INFO_WIDTH (&self->in_info);
  gdouble h = GST_VIDEO_INFO_HEIGHT (&self->in_info);

  switch (self->direction) {
    case GST_VIDEO_ORIENTATION_90R:
    case GST_VIDEO_ORIENTATION_90L:
    case GST_VIDEO_ORIENTATION_UR_LL:
    case GST_VIDEO_ORIENTATION_UL_LR:{
      gdouble tmp = h;
      h = w;
      w = tmp;
      break;
    }
    default:
      break;
  }

  /* TODO: add cropping factor */
  *w_factor = GST_VIDEO_INFO_WIDTH (&self->out_info);
  *w_factor /= w;

  *h_factor = GST_VIDEO_INFO_HEIGHT (&self->out_info);
  *h_factor /= h;
}

static gboolean
gst_va_vpp_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVaVpp *self = GST_VA_VPP (trans);
  GstStructure *structure;
  const GstVideoInfo *in_info = &self->in_info, *out_info = &self->out_info;
  gdouble new_x = 0, new_y = 0, x = 0, y = 0, w_factor = 1, h_factor = 1;
  gboolean ret;

  GST_TRACE_OBJECT (self, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      if (GST_VIDEO_INFO_WIDTH (in_info) != GST_VIDEO_INFO_WIDTH (out_info)
          || GST_VIDEO_INFO_HEIGHT (in_info) != GST_VIDEO_INFO_HEIGHT (out_info)
          || gst_va_filter_get_orientation (self->filter) !=
          GST_VIDEO_ORIENTATION_IDENTITY) {

        event =
            GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

        structure = (GstStructure *) gst_event_get_structure (event);
        if (!gst_structure_get_double (structure, "pointer_x", &x)
            || !gst_structure_get_double (structure, "pointer_y", &y))
          break;

        /* video-direction compensation */
        switch (self->direction) {
          case GST_VIDEO_ORIENTATION_90R:
            new_x = y;
            new_y = GST_VIDEO_INFO_WIDTH (in_info) - 1 - x;
            break;
          case GST_VIDEO_ORIENTATION_90L:
            new_x = GST_VIDEO_INFO_HEIGHT (in_info) - 1 - y;
            new_y = x;
            break;
          case GST_VIDEO_ORIENTATION_UR_LL:
            new_x = GST_VIDEO_INFO_HEIGHT (in_info) - 1 - y;
            new_y = GST_VIDEO_INFO_WIDTH (in_info) - 1 - x;
            break;
          case GST_VIDEO_ORIENTATION_UL_LR:
            new_x = y;
            new_y = x;
            break;
          case GST_VIDEO_ORIENTATION_180:
            /* FIXME: is this correct? */
            new_x = GST_VIDEO_INFO_WIDTH (in_info) - 1 - x;
            new_y = GST_VIDEO_INFO_HEIGHT (in_info) - 1 - y;
            break;
          case GST_VIDEO_ORIENTATION_HORIZ:
            new_x = GST_VIDEO_INFO_WIDTH (in_info) - 1 - x;
            new_y = y;
            break;
          case GST_VIDEO_ORIENTATION_VERT:
            new_x = x;
            new_y = GST_VIDEO_INFO_HEIGHT (in_info) - 1 - y;
            break;
          default:
            new_x = x;
            new_y = y;
            break;
        }

        /* scale compensation */
        _get_scale_factor (self, &w_factor, &h_factor);
        new_x *= w_factor;
        new_y *= h_factor;

        /* TODO: crop compensation */

        GST_TRACE_OBJECT (self, "from %fx%f to %fx%f", x, y, new_x, new_y);
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, new_x,
            "pointer_y", G_TYPE_DOUBLE, new_y, NULL);
      }
      break;
    default:
      break;
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);

  return ret;
}

static gboolean
gst_va_vpp_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVaVpp *self = GST_VA_VPP (trans);
  GstTagList *taglist;
  gchar *orientation;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      gst_event_parse_tag (event, &taglist);

      if (!gst_tag_list_get_string (taglist, "image-orientation", &orientation))
        break;

      if (self->direction != GST_VIDEO_ORIENTATION_AUTO)
        break;

      GST_DEBUG_OBJECT (self, "tag orientation %s", orientation);

      GST_OBJECT_LOCK (self);
      if (!g_strcmp0 ("rotate-0", orientation))
        self->tag_direction = GST_VIDEO_ORIENTATION_IDENTITY;
      else if (!g_strcmp0 ("rotate-90", orientation))
        self->tag_direction = GST_VIDEO_ORIENTATION_90R;
      else if (!g_strcmp0 ("rotate-180", orientation))
        self->tag_direction = GST_VIDEO_ORIENTATION_180;
      else if (!g_strcmp0 ("rotate-270", orientation))
        self->tag_direction = GST_VIDEO_ORIENTATION_90L;
      else if (!g_strcmp0 ("flip-rotate-0", orientation))
        self->tag_direction = GST_VIDEO_ORIENTATION_HORIZ;
      else if (!g_strcmp0 ("flip-rotate-90", orientation))
        self->tag_direction = GST_VIDEO_ORIENTATION_UL_LR;
      else if (!g_strcmp0 ("flip-rotate-180", orientation))
        self->tag_direction = GST_VIDEO_ORIENTATION_VERT;
      else if (!g_strcmp0 ("flip-rotate-270", orientation))
        self->tag_direction = GST_VIDEO_ORIENTATION_UR_LL;

      _update_properties_unlocked (self);
      GST_OBJECT_UNLOCK (self);

      /* no reconfig here because it's done in
       * _update_properties_unlocked */
      gst_va_vpp_update_passthrough (self, FALSE);

      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static GstFlowReturn
gst_va_vpp_generate_output (GstBaseTransform * trans, GstBuffer ** outbuf)
{
  GstVaVpp *self = GST_VA_VPP (trans);
  GstVideoFrame src_frame;
  GstVideoFrame dest_frame;
  GstBuffer *buffer = NULL;
  GstFlowReturn ret;

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->generate_output (trans,
      outbuf);

  if (ret != GST_FLOW_OK || *outbuf == NULL)
    return ret;

  if (!self->other_pool)
    return GST_FLOW_OK;

  /* Now need to copy the output buffer */
  ret = GST_FLOW_ERROR;

  if (!gst_buffer_pool_set_active (self->other_pool, TRUE)) {
    GST_WARNING_OBJECT (self, "failed to active the other pool %"
        GST_PTR_FORMAT, self->other_pool);
    goto out;
  }

  ret = gst_buffer_pool_acquire_buffer (self->other_pool, &buffer, NULL);
  if (ret != GST_FLOW_OK)
    goto out;

  if (!gst_video_frame_map (&src_frame, &self->srcpad_info, *outbuf,
          GST_MAP_READ))
    goto out;

  if (!gst_video_frame_map (&dest_frame, &self->out_info, buffer,
          GST_MAP_WRITE)) {
    gst_video_frame_unmap (&src_frame);
    goto out;
  }

  if (!gst_video_frame_copy (&dest_frame, &src_frame)) {
    gst_video_frame_unmap (&src_frame);
    gst_video_frame_unmap (&dest_frame);
    goto out;
  }

  gst_video_frame_unmap (&src_frame);
  gst_video_frame_unmap (&dest_frame);

  gst_buffer_replace (outbuf, buffer);
  ret = GST_FLOW_OK;

out:
  gst_clear_buffer (&buffer);
  return ret;
}

static void
gst_va_vpp_class_init (gpointer g_class, gpointer class_data)
{
  GstCaps *doc_caps, *caps = NULL;
  GstPadTemplate *sink_pad_templ, *src_pad_templ;
  GObjectClass *object_class = G_OBJECT_CLASS (g_class);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVaDisplay *display;
  GstVaFilter *filter;
  GstVaVppClass *klass = GST_VA_VPP_CLASS (g_class);
  struct CData *cdata = class_data;
  gchar *long_name;

  parent_class = g_type_class_peek_parent (g_class);

  klass->render_device_path = g_strdup (cdata->render_device_path);

  if (cdata->description) {
    long_name = g_strdup_printf ("VA-API Video Postprocessor in %s",
        cdata->description);
  } else {
    long_name = g_strdup ("VA-API Video Postprocessor");
  }

  gst_element_class_set_metadata (element_class, long_name,
      "Filter/Converter/Video/Scaler/Hardware",
      "VA-API based video postprocessor",
      "Víctor Jáquez <vjaquez@igalia.com>");

  display = gst_va_display_drm_new_from_path (klass->render_device_path);
  filter = gst_va_filter_new (display);

  if (gst_va_filter_open (filter))
    caps = gst_va_filter_get_caps (filter);
  else
    caps = gst_caps_from_string (caps_str);

  doc_caps = gst_caps_from_string (caps_str);

  sink_pad_templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      caps);
  gst_element_class_add_pad_template (element_class, sink_pad_templ);
  gst_pad_template_set_documentation_caps (sink_pad_templ,
      gst_caps_ref (doc_caps));

  src_pad_templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      caps);
  gst_element_class_add_pad_template (element_class, src_pad_templ);
  gst_pad_template_set_documentation_caps (src_pad_templ,
      gst_caps_ref (doc_caps));
  gst_caps_unref (doc_caps);

  gst_caps_unref (caps);

  object_class->dispose = gst_va_vpp_dispose;
  object_class->set_property = gst_va_vpp_set_property;
  object_class->get_property = gst_va_vpp_get_property;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_va_vpp_change_state);
  element_class->set_context = GST_DEBUG_FUNCPTR (gst_va_vpp_set_context);

  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_va_vpp_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_va_vpp_decide_allocation);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_va_vpp_query);
  trans_class->transform_caps = GST_DEBUG_FUNCPTR (gst_va_vpp_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_va_vpp_fixate_caps);
  trans_class->transform_size = GST_DEBUG_FUNCPTR (gst_va_vpp_transform_size);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_va_vpp_set_caps);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_va_vpp_before_transform);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_va_vpp_transform);
  trans_class->transform_meta = GST_DEBUG_FUNCPTR (gst_va_vpp_transform_meta);
  trans_class->src_event = GST_DEBUG_FUNCPTR (gst_va_vpp_src_event);
  trans_class->sink_event = GST_DEBUG_FUNCPTR (gst_va_vpp_sink_event);
  trans_class->generate_output = GST_DEBUG_FUNCPTR (gst_va_vpp_generate_output);

  trans_class->transform_ip_on_passthrough = FALSE;

  gst_va_filter_install_properties (filter, object_class);

  g_free (long_name);
  g_free (cdata->description);
  g_free (cdata->render_device_path);
  g_free (cdata);
  gst_object_unref (filter);
  gst_object_unref (display);
}

static inline void
_create_colorbalance_channel (GstVaVpp * self, const gchar * label)
{
  GstColorBalanceChannel *channel;

  channel = g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, NULL);
  channel->label = g_strdup_printf ("VA-%s", label);
  channel->min_value = -1000;
  channel->max_value = 1000;

  self->channels = g_list_append (self->channels, channel);
}

static void
gst_va_vpp_init (GTypeInstance * instance, gpointer g_class)
{
  GstVaVpp *self = GST_VA_VPP (instance);
  GParamSpec *pspec;

  self->direction = GST_VIDEO_ORIENTATION_IDENTITY;
  self->prev_direction = self->direction;
  self->tag_direction = GST_VIDEO_ORIENTATION_AUTO;

  pspec = g_object_class_find_property (g_class, "denoise");
  if (pspec)
    self->denoise = g_value_get_float (g_param_spec_get_default_value (pspec));

  pspec = g_object_class_find_property (g_class, "sharpen");
  if (pspec)
    self->sharpen = g_value_get_float (g_param_spec_get_default_value (pspec));

  pspec = g_object_class_find_property (g_class, "skin-tone");
  if (pspec) {
    const GValue *value = g_param_spec_get_default_value (pspec);
    if (G_VALUE_TYPE (value) == G_TYPE_BOOLEAN)
      self->skintone = g_value_get_boolean (value);
    else
      self->skintone = g_value_get_float (value);
  }

  /* color balance */
  pspec = g_object_class_find_property (g_class, "brightness");
  if (pspec) {
    self->brightness =
        g_value_get_float (g_param_spec_get_default_value (pspec));
    _create_colorbalance_channel (self, "BRIGHTNESS");
  }
  pspec = g_object_class_find_property (g_class, "contrast");
  if (pspec) {
    self->contrast = g_value_get_float (g_param_spec_get_default_value (pspec));
    _create_colorbalance_channel (self, "CONTRAST");
  }
  pspec = g_object_class_find_property (g_class, "hue");
  if (pspec) {
    self->hue = g_value_get_float (g_param_spec_get_default_value (pspec));
    _create_colorbalance_channel (self, "HUE");
  }
  pspec = g_object_class_find_property (g_class, "saturation");
  if (pspec) {
    self->saturation =
        g_value_get_float (g_param_spec_get_default_value (pspec));
    _create_colorbalance_channel (self, "SATURATION");
  }

  /* enable QoS */
  gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM (instance), TRUE);
}

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_va_vpp_debug, "vavpp", 0,
      "VA Video Postprocessor");

#define D(type) \
  G_PASTE (META_TAG_, type) = \
    g_quark_from_static_string (G_PASTE (G_PASTE (GST_META_TAG_VIDEO_, type), _STR))
  D (COLORSPACE);
  D (SIZE);
  D (ORIENTATION);
#undef D
  META_TAG_VIDEO = g_quark_from_static_string (GST_META_TAG_VIDEO_STR);

  return NULL;
}

gboolean
gst_va_vpp_register (GstPlugin * plugin, GstVaDevice * device, guint rank)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVaVppClass),
    .class_init = gst_va_vpp_class_init,
    .instance_size = sizeof (GstVaVpp),
    .instance_init = gst_va_vpp_init,
  };
  struct CData *cdata;
  gboolean ret;
  gchar *type_name, *feature_name;

  g_return_val_if_fail (GST_IS_PLUGIN (plugin), FALSE);
  g_return_val_if_fail (GST_IS_VA_DEVICE (device), FALSE);

  cdata = g_new (struct CData, 1);
  cdata->description = NULL;
  cdata->render_device_path = g_strdup (device->render_device_path);

  type_info.class_data = cdata;

  type_name = g_strdup ("GstVaPostProc");
  feature_name = g_strdup ("vapostproc");

  /* The first postprocessor to be registered should use a constant
   * name, like vapostproc, for any additional postprocessors, we
   * create unique names, using inserting the render device name. */
  if (g_type_from_name (type_name)) {
    gchar *basename = g_path_get_basename (device->render_device_path);
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstVa%sPostProc", basename);
    feature_name = g_strdup_printf ("va%spostproc", basename);
    cdata->description = basename;

    /* lower rank for non-first device */
    if (rank > 0)
      rank--;
  }

  g_once (&debug_once, _register_debug_category, NULL);

  type = g_type_register_static (GST_TYPE_BASE_TRANSFORM, type_name, &type_info,
      0);

  {
    GstVaFilter *filter = gst_va_filter_new (device->display);
    if (gst_va_filter_open (filter)
        && gst_va_filter_has_filter (filter, VAProcFilterColorBalance)) {
      const GInterfaceInfo info = { gst_va_vpp_colorbalance_init, NULL, NULL };
      g_type_add_interface_static (type, GST_TYPE_COLOR_BALANCE, &info);
    }
    gst_object_unref (filter);
  }

  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}

/* Color Balance interface */
static const GList *
gst_va_vpp_colorbalance_list_channels (GstColorBalance * balance)
{
  GstVaVpp *self = GST_VA_VPP (balance);

  return self->channels;
}

static gboolean
_set_cb_val (GstVaVpp * self, const gchar * name,
    GstColorBalanceChannel * channel, gint value, gfloat * cb)
{
  GObjectClass *klass = G_OBJECT_CLASS (GST_VA_VPP_GET_CLASS (self));
  GParamSpec *pspec;
  GParamSpecFloat *fpspec;
  gfloat new_value;
  gboolean changed;

  pspec = g_object_class_find_property (klass, name);
  if (!pspec)
    return FALSE;

  fpspec = G_PARAM_SPEC_FLOAT (pspec);
  new_value = (value - channel->min_value) * (fpspec->maximum - fpspec->minimum)
      / (channel->max_value - channel->min_value) + fpspec->minimum;

  GST_OBJECT_LOCK (self);
  changed = new_value != *cb;
  *cb = new_value;
  value = (*cb + fpspec->minimum) * (channel->max_value - channel->min_value)
      / (fpspec->maximum - fpspec->minimum) + channel->min_value;
  GST_OBJECT_UNLOCK (self);

  if (changed) {
    GST_INFO_OBJECT (self, "%s: %d / %f", channel->label, value, new_value);
    gst_color_balance_value_changed (GST_COLOR_BALANCE (self), channel, value);
    g_atomic_int_set (&self->rebuild_filters, TRUE);
  }

  return TRUE;
}

static void
gst_va_vpp_colorbalance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value)
{
  GstVaVpp *self = GST_VA_VPP (balance);

  if (g_str_has_suffix (channel->label, "HUE"))
    _set_cb_val (self, "hue", channel, value, &self->hue);
  else if (g_str_has_suffix (channel->label, "BRIGHTNESS"))
    _set_cb_val (self, "brightness", channel, value, &self->brightness);
  else if (g_str_has_suffix (channel->label, "CONTRAST"))
    _set_cb_val (self, "contrast", channel, value, &self->contrast);
  else if (g_str_has_suffix (channel->label, "SATURATION"))
    _set_cb_val (self, "saturation", channel, value, &self->saturation);
}

static gboolean
_get_cb_val (GstVaVpp * self, const gchar * name,
    GstColorBalanceChannel * channel, gfloat * cb, gint * val)
{
  GObjectClass *klass = G_OBJECT_CLASS (GST_VA_VPP_GET_CLASS (self));
  GParamSpec *pspec;
  GParamSpecFloat *fpspec;

  pspec = g_object_class_find_property (klass, name);
  if (!pspec)
    return FALSE;

  fpspec = G_PARAM_SPEC_FLOAT (pspec);

  GST_OBJECT_LOCK (self);
  *val = (*cb + fpspec->minimum) * (channel->max_value - channel->min_value)
      / (fpspec->maximum - fpspec->minimum) + channel->min_value;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static gint
gst_va_vpp_colorbalance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel)
{
  GstVaVpp *self = GST_VA_VPP (balance);
  gint value = 0;

  if (g_str_has_suffix (channel->label, "HUE"))
    _get_cb_val (self, "hue", channel, &self->hue, &value);
  else if (g_str_has_suffix (channel->label, "BRIGHTNESS"))
    _get_cb_val (self, "brightness", channel, &self->brightness, &value);
  else if (g_str_has_suffix (channel->label, "CONTRAST"))
    _get_cb_val (self, "contrast", channel, &self->contrast, &value);
  else if (g_str_has_suffix (channel->label, "SATURATION"))
    _get_cb_val (self, "saturation", channel, &self->saturation, &value);

  return value;
}

static GstColorBalanceType
gst_va_vpp_colorbalance_get_balance_type (GstColorBalance * balance)
{
  return GST_COLOR_BALANCE_HARDWARE;
}

static void
gst_va_vpp_colorbalance_init (gpointer iface, gpointer data)
{
  GstColorBalanceInterface *cbiface = iface;

  cbiface->list_channels = gst_va_vpp_colorbalance_list_channels;
  cbiface->set_value = gst_va_vpp_colorbalance_set_value;
  cbiface->get_value = gst_va_vpp_colorbalance_get_value;
  cbiface->get_balance_type = gst_va_vpp_colorbalance_get_balance_type;
}
