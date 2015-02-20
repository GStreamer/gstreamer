/*
 *  gstvaapivideobufferpool.c - Gstreamer/VA video buffer pool
 *
 *  Copyright (C) 2013 Intel Corporation
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

#include "gst/vaapi/sysdeps.h"
#include "gstvaapivideobufferpool.h"
#include "gstvaapivideobuffer.h"
#include "gstvaapivideomemory.h"
#if GST_CHECK_VERSION(1,1,0) && (USE_GLX || USE_EGL)
#include "gstvaapivideometa_texture.h"
#endif

GST_DEBUG_CATEGORY_STATIC (gst_debug_vaapivideopool);
#define GST_CAT_DEFAULT gst_debug_vaapivideopool

G_DEFINE_TYPE (GstVaapiVideoBufferPool,
    gst_vaapi_video_buffer_pool, GST_TYPE_BUFFER_POOL);

enum
{
  PROP_0,

  PROP_DISPLAY,
};

struct _GstVaapiVideoBufferPoolPrivate
{
  GstVideoInfo video_info[2];
  guint video_info_index;
  GstAllocator *allocator;
  GstVideoInfo alloc_info;
  GstVaapiDisplay *display;
  guint has_video_meta:1;
  guint has_video_alignment:1;
  guint has_texture_upload_meta:1;
  guint use_dmabuf_memory:1;
};

#define GST_VAAPI_VIDEO_BUFFER_POOL_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_VAAPI_TYPE_VIDEO_BUFFER_POOL, \
      GstVaapiVideoBufferPoolPrivate))

static void
gst_vaapi_video_buffer_pool_finalize (GObject * object)
{
  GstVaapiVideoBufferPoolPrivate *const priv =
      GST_VAAPI_VIDEO_BUFFER_POOL (object)->priv;

  G_OBJECT_CLASS (gst_vaapi_video_buffer_pool_parent_class)->finalize (object);

  gst_vaapi_display_replace (&priv->display, NULL);
  g_clear_object (&priv->allocator);
}

static void
gst_vaapi_video_buffer_pool_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaapiVideoBufferPoolPrivate *const priv =
      GST_VAAPI_VIDEO_BUFFER_POOL (object)->priv;

  switch (prop_id) {
    case PROP_DISPLAY:
      priv->display = gst_vaapi_display_ref (g_value_get_pointer (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapi_video_buffer_pool_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaapiVideoBufferPoolPrivate *const priv =
      GST_VAAPI_VIDEO_BUFFER_POOL (object)->priv;

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_pointer (value, priv->display);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
fill_video_alignment (GstVaapiVideoBufferPool * pool, GstVideoAlignment * align)
{
  GstVideoInfo *const vip = &pool->priv->alloc_info;
  guint i;

  gst_video_alignment_reset (align);
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (vip); i++)
    align->stride_align[i] =
        (1U << g_bit_nth_lsf (GST_VIDEO_INFO_PLANE_STRIDE (vip, i), 0)) - 1;
}

static const gchar **
gst_vaapi_video_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *g_options[] = {
    GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_GL_TEXTURE_UPLOAD_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT,
    NULL,
  };

  return g_options;
}

static gboolean
gst_vaapi_video_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config)
{
  GstVaapiVideoBufferPoolPrivate *const priv =
      GST_VAAPI_VIDEO_BUFFER_POOL (pool)->priv;
  GstCaps *caps = NULL;
  GstVideoInfo *const cur_vip = &priv->video_info[priv->video_info_index];
  GstVideoInfo *const new_vip = &priv->video_info[!priv->video_info_index];
  GstVideoAlignment align;
  GstAllocator *allocator;
  gboolean changed_caps, use_dmabuf_memory;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL))
    goto error_invalid_config;
  if (!caps || !gst_video_info_from_caps (new_vip, caps))
    goto error_no_caps;

  use_dmabuf_memory = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_DMABUF_MEMORY);
  if (priv->use_dmabuf_memory != use_dmabuf_memory) {
    priv->use_dmabuf_memory = use_dmabuf_memory;
    g_clear_object (&priv->allocator);
  }

  changed_caps = !priv->allocator ||
      GST_VIDEO_INFO_FORMAT (cur_vip) != GST_VIDEO_INFO_FORMAT (new_vip) ||
      GST_VIDEO_INFO_WIDTH (cur_vip) != GST_VIDEO_INFO_WIDTH (new_vip) ||
      GST_VIDEO_INFO_HEIGHT (cur_vip) != GST_VIDEO_INFO_HEIGHT (new_vip);

  if (changed_caps) {
    const GstVideoInfo *alloc_vip;
    guint flags = 0;

    if (use_dmabuf_memory) {
      /* XXX: also needs fixed strides/offsets */
      flags |= GST_VAAPI_SURFACE_ALLOC_FLAG_LINEAR_STORAGE;
      allocator =
          gst_vaapi_dmabuf_allocator_new (priv->display, new_vip, flags);
    } else {
      allocator = gst_vaapi_video_allocator_new (priv->display, new_vip, 0);
    }
    if (!allocator)
      goto error_create_allocator;
    gst_object_replace ((GstObject **) & priv->allocator,
        GST_OBJECT_CAST (allocator));
    gst_object_unref (allocator);
    priv->video_info_index ^= 1;

    alloc_vip = gst_allocator_get_vaapi_video_info (allocator, NULL);
    if (!alloc_vip)
      goto error_create_allocator_info;
    priv->alloc_info = *alloc_vip;
  }

  if (!gst_buffer_pool_config_has_option (config,
          GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META))
    goto error_no_vaapi_video_meta_option;

  priv->has_video_meta = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  priv->has_video_alignment = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  if (priv->has_video_alignment) {
    fill_video_alignment (GST_VAAPI_VIDEO_BUFFER_POOL (pool), &align);
    gst_buffer_pool_config_set_video_alignment (config, &align);
  }

  priv->has_texture_upload_meta = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_GL_TEXTURE_UPLOAD_META);

  return
      GST_BUFFER_POOL_CLASS
      (gst_vaapi_video_buffer_pool_parent_class)->set_config (pool, config);

  /* ERRORS */
error_invalid_config:
  {
    GST_ERROR ("invalid config");
    return FALSE;
  }
error_no_caps:
  {
    GST_ERROR ("no valid caps in config");
    return FALSE;
  }
error_create_allocator:
  {
    GST_ERROR ("failed to create GstVaapiVideoAllocator object");
    return FALSE;
  }
error_create_allocator_info:
  {
    GST_ERROR ("failed to create GstVaapiVideoAllocator `video-info'");
    return FALSE;
  }
error_no_vaapi_video_meta_option:
  {
    GST_ERROR ("no GstVaapiVideoMeta option");
    return FALSE;
  }
}

static GstFlowReturn
gst_vaapi_video_buffer_pool_alloc_buffer (GstBufferPool * pool,
    GstBuffer ** out_buffer_ptr, GstBufferPoolAcquireParams * params)
{
  GstVaapiVideoBufferPoolPrivate *const priv =
      GST_VAAPI_VIDEO_BUFFER_POOL (pool)->priv;
  GstVaapiVideoMeta *meta;
  GstMemory *mem;
  GstBuffer *buffer;

  const gboolean alloc_vaapi_video_meta = !params ||
      !(params->flags & GST_VAAPI_VIDEO_BUFFER_POOL_ACQUIRE_FLAG_NO_ALLOC);

  if (!priv->allocator)
    goto error_no_allocator;

  if (alloc_vaapi_video_meta) {
    meta = gst_vaapi_video_meta_new (priv->display);
    if (!meta)
      goto error_create_meta;

    buffer = gst_vaapi_video_buffer_new (meta);
  } else {
    meta = NULL;
    buffer = gst_vaapi_video_buffer_new_empty ();
  }
  if (!buffer)
    goto error_create_buffer;

  if (priv->use_dmabuf_memory)
    mem = gst_vaapi_dmabuf_memory_new (priv->allocator, meta);
  else
    mem = gst_vaapi_video_memory_new (priv->allocator, meta);
  if (!mem)
    goto error_create_memory;
  gst_vaapi_video_meta_replace (&meta, NULL);
  gst_buffer_append_memory (buffer, mem);

  if (priv->has_video_meta) {
    GstVideoInfo *const vip = &priv->alloc_info;
    GstVideoMeta *vmeta;

    vmeta = gst_buffer_add_video_meta_full (buffer, 0,
        GST_VIDEO_INFO_FORMAT (vip), GST_VIDEO_INFO_WIDTH (vip),
        GST_VIDEO_INFO_HEIGHT (vip), GST_VIDEO_INFO_N_PLANES (vip),
        &GST_VIDEO_INFO_PLANE_OFFSET (vip, 0),
        &GST_VIDEO_INFO_PLANE_STRIDE (vip, 0));

    if (GST_VAAPI_IS_VIDEO_MEMORY (mem)) {
      vmeta->map = gst_video_meta_map_vaapi_memory;
      vmeta->unmap = gst_video_meta_unmap_vaapi_memory;
    }
  }

#if GST_CHECK_VERSION(1,1,0) && (USE_GLX || USE_EGL)
  if (priv->has_texture_upload_meta)
    gst_buffer_add_texture_upload_meta (buffer);
#endif

  *out_buffer_ptr = buffer;
  return GST_FLOW_OK;

  /* ERRORS */
error_no_allocator:
  {
    GST_ERROR ("no GstAllocator in buffer pool");
    return GST_FLOW_ERROR;
  }
error_create_meta:
  {
    GST_ERROR ("failed to allocate vaapi video meta");
    return GST_FLOW_ERROR;
  }
error_create_buffer:
  {
    GST_ERROR ("failed to create video buffer");
    gst_vaapi_video_meta_unref (meta);
    return GST_FLOW_ERROR;
  }
error_create_memory:
  {
    GST_ERROR ("failed to create video memory");
    gst_buffer_unref (buffer);
    gst_vaapi_video_meta_unref (meta);
    return GST_FLOW_ERROR;
  }
}

static void
gst_vaapi_video_buffer_pool_reset_buffer (GstBufferPool * pool,
    GstBuffer * buffer)
{
  GstMemory *const mem = gst_buffer_peek_memory (buffer, 0);

  /* Release the underlying surface proxy */
  if (GST_VAAPI_IS_VIDEO_MEMORY (mem))
    gst_vaapi_video_memory_reset_surface (GST_VAAPI_VIDEO_MEMORY_CAST (mem));

  GST_BUFFER_POOL_CLASS (gst_vaapi_video_buffer_pool_parent_class)->reset_buffer
      (pool, buffer);
}

static void
gst_vaapi_video_buffer_pool_class_init (GstVaapiVideoBufferPoolClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *const pool_class = GST_BUFFER_POOL_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_debug_vaapivideopool,
      "vaapivideopool", 0, "VA-API video pool");

  g_type_class_add_private (klass, sizeof (GstVaapiVideoBufferPoolPrivate));

  object_class->finalize = gst_vaapi_video_buffer_pool_finalize;
  object_class->set_property = gst_vaapi_video_buffer_pool_set_property;
  object_class->get_property = gst_vaapi_video_buffer_pool_get_property;
  pool_class->get_options = gst_vaapi_video_buffer_pool_get_options;
  pool_class->set_config = gst_vaapi_video_buffer_pool_set_config;
  pool_class->alloc_buffer = gst_vaapi_video_buffer_pool_alloc_buffer;
  pool_class->reset_buffer = gst_vaapi_video_buffer_pool_reset_buffer;

  /**
   * GstVaapiVideoBufferPool:display:
   *
   * The #GstVaapiDisplay this object is bound to.
   */
  g_object_class_install_property
      (object_class,
      PROP_DISPLAY,
      g_param_spec_pointer ("display",
          "Display",
          "The GstVaapiDisplay to use for this video pool",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_video_buffer_pool_init (GstVaapiVideoBufferPool * pool)
{
  GstVaapiVideoBufferPoolPrivate *const priv =
      GST_VAAPI_VIDEO_BUFFER_POOL_GET_PRIVATE (pool);

  pool->priv = priv;

  gst_video_info_init (&priv->video_info[0]);
  gst_video_info_init (&priv->video_info[1]);
  gst_video_info_init (&priv->alloc_info);
}

GstBufferPool *
gst_vaapi_video_buffer_pool_new (GstVaapiDisplay * display)
{
  return g_object_new (GST_VAAPI_TYPE_VIDEO_BUFFER_POOL,
      "display", display, NULL);
}
