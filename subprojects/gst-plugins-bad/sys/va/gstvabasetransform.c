/* GStreamer
 * Copyright (C) 2021 Igalia, S.L.
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvabasetransform.h"

#include <gst/va/gstva.h>

#include "gstvacaps.h"

#define GST_CAT_DEFAULT gst_va_base_transform_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_DEVICE_PATH = 1,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

struct _GstVaBaseTransformPrivate
{
  GstVideoInfo srcpad_info;

  GstBufferPool *other_pool;

  GstCaps *sinkpad_caps;
  GstVideoInfo sinkpad_info;
  GstBufferPool *sinkpad_pool;

  GstCaps *filter_caps;
};

/**
 * GstVaBaseTransform:
 *
 * A base class implementation for VA-API filters.
 *
 * Since: 1.20
 */
#define gst_va_base_transform_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstVaBaseTransform, gst_va_base_transform,
    GST_TYPE_BASE_TRANSFORM, G_ADD_PRIVATE (GstVaBaseTransform)
    GST_DEBUG_CATEGORY_INIT (gst_va_base_transform_debug,
        "vabasetransform", 0, "vabasetransform element");
    );

extern GRecMutex GST_VA_SHARED_LOCK;

static void
gst_va_base_transform_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaBaseTransform *self = GST_VA_BASE_TRANSFORM (object);

  switch (prop_id) {
    case PROP_DEVICE_PATH:{
      if (!(self->display && GST_IS_VA_DISPLAY_DRM (self->display))) {
        g_value_set_string (value, NULL);
        return;
      }
      g_object_get_property (G_OBJECT (self->display), "path", value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_va_base_transform_dispose (GObject * object)
{
  GstVaBaseTransform *self = GST_VA_BASE_TRANSFORM (object);

  if (self->priv->other_pool) {
    gst_buffer_pool_set_active (self->priv->other_pool, FALSE);
    gst_clear_object (&self->priv->other_pool);
  }

  gst_clear_caps (&self->out_caps);
  gst_clear_caps (&self->in_caps);

  gst_clear_caps (&self->priv->filter_caps);

  gst_clear_object (&self->filter);
  gst_clear_object (&self->display);

  if (self->priv->sinkpad_pool) {
    gst_buffer_pool_set_active (self->priv->sinkpad_pool, FALSE);
    gst_clear_object (&self->priv->sinkpad_pool);
  }

  gst_clear_caps (&self->priv->sinkpad_caps);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_va_base_transform_init (GstVaBaseTransform * self)
{
  gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM (self), TRUE);

  self->priv = gst_va_base_transform_get_instance_private (self);
}

static gboolean
gst_va_base_transform_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  GstVaBaseTransform *self = GST_VA_BASE_TRANSFORM (trans);
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

static gboolean
gst_va_base_transform_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVaBaseTransform *self = GST_VA_BASE_TRANSFORM (trans);
  GstVaBaseTransformClass *fclass;
  GstVideoInfo in_info, out_info;
  gboolean res;

  /* input caps */
  if (!gst_video_info_from_caps (&in_info, incaps))
    goto invalid_caps;

  /* output caps */
  if (!gst_video_info_from_caps (&out_info, outcaps))
    goto invalid_caps;

  fclass = GST_VA_BASE_TRANSFORM_GET_CLASS (self);
  if (fclass->set_info)
    res = fclass->set_info (self, incaps, &in_info, outcaps, &out_info);
  else
    res = TRUE;

  self->negotiated = res;

  if (res) {
    gst_caps_replace (&self->in_caps, incaps);
    gst_caps_replace (&self->out_caps, outcaps);

    self->in_info = in_info;
    self->out_info = out_info;
  }

  if (self->priv->sinkpad_pool) {
    gst_buffer_pool_set_active (self->priv->sinkpad_pool, FALSE);
    gst_clear_object (&self->priv->sinkpad_pool);
  }

  if (self->priv->other_pool) {
    gst_buffer_pool_set_active (self->priv->other_pool, FALSE);
    gst_clear_object (&self->priv->other_pool);
  }

  return res;

  /* ERRORS */
invalid_caps:
  {
    GST_ERROR_OBJECT (self, "invalid caps");
    self->negotiated = FALSE;
    return FALSE;
  }
}

/* Answer upstream allocation query. */
static gboolean
gst_va_base_transform_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstVaBaseTransform *self = GST_VA_BASE_TRANSFORM (trans);
  GstAllocator *allocator = NULL;
  GstAllocationParams params = { 0, };
  GstBufferPool *pool;
  GstCaps *caps;
  GstVideoInfo info;
  gboolean update_allocator = FALSE;
  guint size, usage_hint = VA_SURFACE_ATTRIB_USAGE_HINT_GENERIC;        /* it migth be
                                                                         * used by a va
                                                                         * decoder */

  gst_clear_caps (&self->priv->sinkpad_caps);

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  /* passthrough, we're done */
  if (!decide_query)
    return TRUE;

  if (gst_query_get_n_allocation_pools (query) > 0)
    return TRUE;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps)
    return FALSE;
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Cannot parse caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  size = GST_VIDEO_INFO_SIZE (&info);

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
    if (!(allocator = gst_va_base_transform_allocator_from_caps (self, caps)))
      return FALSE;
  }

  pool = gst_va_pool_new_with_config (caps, size, 1 + self->extra_min_buffers,
      0, usage_hint, GST_VA_FEATURE_AUTO, allocator, &params);
  if (!pool) {
    gst_object_unref (allocator);
    goto config_failed;
  }

  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);

  gst_query_add_allocation_pool (query, pool, size, 1 + self->extra_min_buffers,
      0);

  GST_DEBUG_OBJECT (self,
      "proposing %" GST_PTR_FORMAT " with allocator %" GST_PTR_FORMAT,
      pool, allocator);

  gst_object_unref (allocator);
  gst_object_unref (pool);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  self->priv->sinkpad_caps = gst_caps_ref (caps);

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
 * configure some properties on it. Only it's called when not in
 * passthrough mode. */
static gboolean
gst_va_base_transform_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstVaBaseTransform *self = GST_VA_BASE_TRANSFORM (trans);
  GstAllocator *allocator = NULL, *other_allocator = NULL;
  GstAllocationParams params, other_params;
  GstBufferPool *pool = NULL, *other_pool = NULL;
  GstCaps *outcaps = NULL;
  GstStructure *config;
  GstVideoInfo vinfo;
  guint min, max, size = 0, usage_hint = VA_SURFACE_ATTRIB_USAGE_HINT_VPP_WRITE;
  gboolean update_pool, update_allocator, has_videometa, copy_frames;
  gboolean dont_use_other_pool = FALSE;

  gst_query_parse_allocation (query, &outcaps, NULL);

  gst_allocation_params_init (&other_params);
  gst_allocation_params_init (&params);

  if (!gst_video_info_from_caps (&vinfo, outcaps)) {
    GST_ERROR_OBJECT (self, "Cannot parse caps %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  if (gst_query_get_n_allocation_params (query) > 0) {
    GstVaDisplay *display;

    gst_query_parse_nth_allocation_param (query, 0, &allocator, &other_params);
    display = gst_va_allocator_peek_display (allocator);
    if (!display) {
      /* save the allocator for the other pool */
      other_allocator = allocator;
      allocator = NULL;
    } else if (display != self->display) {
      /* The allocator and pool belong to other display, we should not use. */
      gst_clear_object (&allocator);
      dont_use_other_pool = TRUE;
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
      } else if (dont_use_other_pool) {
        gst_clear_object (&pool);
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
    /* XXX(victor): USAGE_HINT_VPP_WRITE creates tiled dmabuf frames
     * in iHD */
    if (gst_caps_is_dmabuf (outcaps) && GST_VIDEO_INFO_IS_RGB (&vinfo))
      usage_hint = VA_SURFACE_ATTRIB_USAGE_HINT_GENERIC;
    if (!(allocator =
            gst_va_base_transform_allocator_from_caps (self, outcaps)))
      return FALSE;
  }

  if (!pool)
    pool = gst_va_pool_new ();

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_allocator (config, allocator, &params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_set_va_allocation_params (config, usage_hint,
      GST_VA_FEATURE_AUTO);
  if (!gst_buffer_pool_set_config (pool, config)) {
    gst_object_unref (allocator);
    gst_object_unref (pool);
    return FALSE;
  }

  if (GST_IS_VA_DMABUF_ALLOCATOR (allocator)) {
    gst_va_dmabuf_allocator_get_format (allocator, &self->priv->srcpad_info,
        NULL);
  } else if (GST_IS_VA_ALLOCATOR (allocator)) {
    gst_va_allocator_get_format (allocator, &self->priv->srcpad_info, NULL,
        NULL);
  }

  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);

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
      gst_object_replace ((GstObject **) & self->priv->other_pool,
          (GstObject *) other_pool);
    } else {
      self->priv->other_pool =
          _create_other_pool (other_allocator, &other_params, outcaps, size);
    }
    GST_DEBUG_OBJECT (self, "Use the other pool for copy %" GST_PTR_FORMAT,
        self->priv->other_pool);
  } else {
    gst_clear_object (&self->priv->other_pool);
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

/* output buffers must be from our VA-based pool, they cannot be
 * system-allocated */
static gboolean
gst_va_base_transform_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, gsize size,
    GstCaps * othercaps, gsize * othersize)
{
  return FALSE;
}

static GstFlowReturn
gst_va_base_transform_generate_output (GstBaseTransform * trans,
    GstBuffer ** outbuf)
{
  GstVaBaseTransform *self = GST_VA_BASE_TRANSFORM (trans);
  GstVideoFrame src_frame;
  GstVideoFrame dest_frame;
  GstBuffer *buffer = NULL;
  GstFlowReturn ret;

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->generate_output (trans,
      outbuf);

  if (ret != GST_FLOW_OK || *outbuf == NULL)
    return ret;

  if (!self->priv->other_pool)
    return GST_FLOW_OK;

  /* Now need to copy the output buffer */
  ret = GST_FLOW_ERROR;

  if (!gst_buffer_pool_set_active (self->priv->other_pool, TRUE)) {
    GST_WARNING_OBJECT (self, "failed to active the other pool %"
        GST_PTR_FORMAT, self->priv->other_pool);
    goto out;
  }

  ret = gst_buffer_pool_acquire_buffer (self->priv->other_pool, &buffer, NULL);
  if (ret != GST_FLOW_OK)
    goto out;

  if (!gst_video_frame_map (&src_frame, &self->priv->srcpad_info, *outbuf,
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

static GstStateChangeReturn
gst_va_base_transform_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVaBaseTransform *self = GST_VA_BASE_TRANSFORM (element);
  GstVaBaseTransformClass *klass = GST_VA_BASE_TRANSFORM_GET_CLASS (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_va_ensure_element_data (element, klass->render_device_path,
              &self->display))
        goto open_failed;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEVICE_PATH]);
      gst_clear_caps (&self->priv->filter_caps);
      gst_clear_object (&self->filter);
      self->filter = gst_va_filter_new (self->display);
      if (!gst_va_filter_open (self->filter))
        goto open_failed;
      if (klass->update_properties)
        klass->update_properties (self);
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
      gst_clear_caps (&self->priv->filter_caps);
      gst_clear_object (&self->filter);
      gst_clear_object (&self->display);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEVICE_PATH]);
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
gst_va_base_transform_set_context (GstElement * element, GstContext * context)
{
  GstVaDisplay *old_display, *new_display;
  GstVaBaseTransform *self = GST_VA_BASE_TRANSFORM (element);
  GstVaBaseTransformClass *klass = GST_VA_BASE_TRANSFORM_GET_CLASS (self);
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

static void
gst_va_base_transform_class_init (GstVaBaseTransformClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *trans_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->dispose = gst_va_base_transform_dispose;
  gobject_class->get_property = gst_va_base_transform_get_property;

  trans_class->query = GST_DEBUG_FUNCPTR (gst_va_base_transform_query);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_va_base_transform_set_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_va_base_transform_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_va_base_transform_decide_allocation);
  trans_class->transform_size =
      GST_DEBUG_FUNCPTR (gst_va_base_transform_transform_size);
  trans_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_va_base_transform_generate_output);

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_va_base_transform_set_context);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_va_base_transform_change_state);

  /**
   * GstVaBaseTransform:device-path:
   *
   * It shows the DRM device path used for the VA operation, if any.
   *
   * Since: 1.22
   */
  properties[PROP_DEVICE_PATH] = g_param_spec_string ("device-path",
      "Device Path", "DRM device path", NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, properties);

  gst_type_mark_as_plugin_api (GST_TYPE_VA_BASE_TRANSFORM, 0);
}

GstAllocator *
gst_va_base_transform_allocator_from_caps (GstVaBaseTransform * self,
    GstCaps * caps)
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

static inline gsize
_get_plane_data_size (GstVideoInfo * info, guint plane)
{
  gint comp[GST_VIDEO_MAX_COMPONENTS];
  gint height, padded_height;

  gst_video_format_info_component (info->finfo, plane, comp);

  height = GST_VIDEO_INFO_HEIGHT (info);
  padded_height =
      GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (info->finfo, comp[0], height);

  return GST_VIDEO_INFO_PLANE_STRIDE (info, plane) * padded_height;
}

static gboolean
_try_import_dmabuf_unlocked (GstVaBaseTransform * self, GstBuffer * inbuf)
{
  GstVaBaseTransform *btrans = GST_VA_BASE_TRANSFORM (self);
  GstVideoMeta *meta;
  GstVideoInfo in_info = btrans->in_info;
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
  return gst_va_dmabuf_memories_setup (btrans->display, &in_info, n_planes,
      mems, fd, offset, VA_SURFACE_ATTRIB_USAGE_HINT_VPP_READ);
}

static GstBufferPool *
_get_sinkpad_pool (GstVaBaseTransform * self)
{
  GstAllocator *allocator;
  GstAllocationParams params = { 0, };
  GstCaps *caps;
  GstVideoInfo in_info;
  guint size, usage_hint = VA_SURFACE_ATTRIB_USAGE_HINT_VPP_READ;

  if (self->priv->sinkpad_pool)
    return self->priv->sinkpad_pool;

  gst_allocation_params_init (&params);

  if (self->priv->sinkpad_caps) {
    caps = self->priv->sinkpad_caps;
    if (!gst_video_info_from_caps (&in_info, caps)) {
      GST_ERROR_OBJECT (self, "Cannot parse caps %" GST_PTR_FORMAT, caps);
      return NULL;
    }
  } else {
    caps = self->in_caps;
    in_info = self->in_info;
  }

  size = GST_VIDEO_INFO_SIZE (&in_info);

  allocator = gst_va_base_transform_allocator_from_caps (self, caps);
  self->priv->sinkpad_pool = gst_va_pool_new_with_config (caps, size, 1, 0,
      usage_hint, GST_VA_FEATURE_AUTO, allocator, &params);
  if (!self->priv->sinkpad_pool) {
    gst_object_unref (allocator);
    return NULL;
  }

  if (GST_IS_VA_DMABUF_ALLOCATOR (allocator)) {
    gst_va_dmabuf_allocator_get_format (allocator, &self->priv->sinkpad_info,
        NULL);
  } else if (GST_IS_VA_ALLOCATOR (allocator)) {
    gst_va_allocator_get_format (allocator, &self->priv->sinkpad_info, NULL,
        NULL);
  }

  gst_object_unref (allocator);

  if (!gst_buffer_pool_set_active (self->priv->sinkpad_pool, TRUE)) {
    GST_WARNING_OBJECT (self, "failed to active the sinkpad pool %"
        GST_PTR_FORMAT, self->priv->sinkpad_pool);
    return NULL;
  }

  return self->priv->sinkpad_pool;
}

static gboolean
_try_import_buffer (GstVaBaseTransform * self, GstBuffer * inbuf)
{
  VASurfaceID surface;
  gboolean ret;

  surface = gst_va_buffer_get_surface (inbuf);
  if (surface != VA_INVALID_ID &&
      (gst_va_buffer_peek_display (inbuf) == self->display))
    return TRUE;

  g_rec_mutex_lock (&GST_VA_SHARED_LOCK);
  ret = _try_import_dmabuf_unlocked (self, inbuf);
  g_rec_mutex_unlock (&GST_VA_SHARED_LOCK);

  return ret;
}

GstFlowReturn
gst_va_base_transform_import_buffer (GstVaBaseTransform * self,
    GstBuffer * inbuf, GstBuffer ** buf)
{
  GstBuffer *buffer = NULL;
  GstBufferPool *pool;
  GstFlowReturn ret;
  GstVideoFrame in_frame, out_frame;
  gboolean imported, copied;

  g_return_val_if_fail (GST_IS_VA_BASE_TRANSFORM (self), GST_FLOW_ERROR);

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

  if (!gst_video_frame_map (&out_frame, &self->priv->sinkpad_info, buffer,
          GST_MAP_WRITE)) {
    gst_video_frame_unmap (&in_frame);
    goto invalid_buffer;
  }

  copied = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  if (!copied)
    goto invalid_buffer;

  /* copy metadata, default implemenation of baseclass will copy everything
   * what we need */
  GST_BASE_TRANSFORM_CLASS (parent_class)->copy_metadata
      (GST_BASE_TRANSFORM_CAST (self), inbuf, buffer);

  *buf = buffer;

  return GST_FLOW_OK;

invalid_buffer:
  {
    GST_ELEMENT_WARNING (self, STREAM, FORMAT, (NULL),
        ("invalid video buffer received"));
    if (buffer)
      gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
}

GstCaps *
gst_va_base_transform_get_filter_caps (GstVaBaseTransform * self)
{
  g_return_val_if_fail (GST_IS_VA_BASE_TRANSFORM (self), NULL);

  GST_OBJECT_LOCK (self);
  if (self->priv->filter_caps) {
    GST_OBJECT_UNLOCK (self);
    return self->priv->filter_caps;
  }
  GST_OBJECT_UNLOCK (self);

  if (!self->filter)
    return NULL;

  GST_OBJECT_LOCK (self);
  self->priv->filter_caps = gst_va_filter_get_caps (self->filter);
  GST_OBJECT_UNLOCK (self);
  return self->priv->filter_caps;
}
