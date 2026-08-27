/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include "gsthip.h"
#include "gsthip-private.h"

GST_DEBUG_CATEGORY_STATIC (gst_hip_host_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_hip_host_buffer_pool_debug

struct _GstHipHostBufferPoolPrivate
{
  GstVideoInfo info;
  GstHipHostPoolAllocator *alloc;
};

#define gst_hip_host_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstHipHostBufferPool,
    gst_hip_host_buffer_pool, GST_TYPE_BUFFER_POOL);

static void gst_hip_host_buffer_pool_finalize (GObject * object);
static const gchar **gst_hip_host_buffer_pool_get_options (GstBufferPool *
    pool);
static gboolean gst_hip_host_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config);
static gboolean gst_hip_host_buffer_pool_start (GstBufferPool * pool);
static gboolean gst_hip_host_buffer_pool_stop (GstBufferPool * pool);
static GstFlowReturn gst_hip_host_buffer_pool_alloc_buffer (GstBufferPool *
    pool, GstBuffer ** buffer, GstBufferPoolAcquireParams * params);

static void
gst_hip_host_buffer_pool_class_init (GstHipHostBufferPoolClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto pool_class = GST_BUFFER_POOL_CLASS (klass);

  object_class->finalize = gst_hip_host_buffer_pool_finalize;

  pool_class->get_options = gst_hip_host_buffer_pool_get_options;
  pool_class->set_config = gst_hip_host_buffer_pool_set_config;
  pool_class->start = gst_hip_host_buffer_pool_start;
  pool_class->stop = gst_hip_host_buffer_pool_stop;
  pool_class->alloc_buffer = gst_hip_host_buffer_pool_alloc_buffer;

  GST_DEBUG_CATEGORY_INIT (gst_hip_host_buffer_pool_debug,
      "hiphostbufferpool", 0, "hiphostbufferpool");
}

static void
gst_hip_host_buffer_pool_init (GstHipHostBufferPool * self)
{
  self->priv = (GstHipHostBufferPoolPrivate *)
      gst_hip_host_buffer_pool_get_instance_private (self);
}

static void
gst_hip_host_buffer_pool_finalize (GObject * object)
{
  auto self = GST_HIP_HOST_BUFFER_POOL (object);
  auto priv = self->priv;

  if (priv->alloc) {
    gst_hip_host_allocator_set_active (GST_HIP_HOST_ALLOCATOR (priv->alloc),
        FALSE);
    gst_object_unref (priv->alloc);
  }

  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static const gchar **
gst_hip_host_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    nullptr
  };

  return options;
}

static gboolean
gst_hip_host_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config)
{
  auto self = GST_HIP_HOST_BUFFER_POOL (pool);
  auto priv = self->priv;
  GstCaps *caps = nullptr;
  guint min_buffers, max_buffers;
  guint size;
  GstVideoInfo info, aligned_info;
  gboolean dummy;

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers)) {
    GST_WARNING_OBJECT (self, "invalid config");
    return FALSE;
  }

  if (!caps) {
    GST_WARNING_OBJECT (self, "Empty caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (priv->alloc) {
    gst_hip_host_allocator_set_active (GST_HIP_HOST_ALLOCATOR (priv->alloc),
        FALSE);
    gst_clear_object (&priv->alloc);
  }

  if (!gst_hip_device_align_video_info_for_texture (self->device,
          &info, &aligned_info, &dummy)) {
    GST_ERROR_OBJECT (self, "Failed to align video info");
    return FALSE;
  }

  priv->alloc = gst_hip_host_pool_allocator_new (self->device,
      aligned_info.size,
      gst_buffer_pool_config_get_hip_host_alloc_flags (config));

  if (!priv->alloc) {
    GST_ERROR_OBJECT (self, "Couldn't create allocator");
    return FALSE;
  }

  priv->info = aligned_info;

  gst_buffer_pool_config_set_params (config, caps, aligned_info.size,
      min_buffers, max_buffers);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static gboolean
gst_hip_host_buffer_pool_start (GstBufferPool * pool)
{
  auto self = GST_HIP_HOST_BUFFER_POOL (pool);
  auto priv = self->priv;

  if (!gst_hip_host_allocator_set_active (GST_HIP_HOST_ALLOCATOR (priv->alloc),
          TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't activate allocator");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_hip_host_buffer_pool_stop (GstBufferPool * pool)
{
  auto self = GST_HIP_HOST_BUFFER_POOL (pool);
  auto priv = self->priv;

  if (priv->alloc) {
    gst_hip_host_allocator_set_active (GST_HIP_HOST_ALLOCATOR (priv->alloc),
        FALSE);
  }

  return GST_BUFFER_POOL_CLASS (parent_class)->stop (pool);
}

static GstFlowReturn
gst_hip_host_buffer_pool_alloc_buffer (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  auto self = GST_HIP_HOST_BUFFER_POOL (pool);
  auto priv = self->priv;
  auto info = &priv->info;
  GstMemory *mem;

  auto ret = gst_hip_host_pool_allocator_acquire_memory (priv->alloc, &mem);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Couldn't acquire memory");
    return ret;
  }

  auto buf = gst_buffer_new ();
  gst_buffer_append_memory (buf, mem);

  gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
      info->offset, info->stride);

  *buffer = buf;

  return GST_FLOW_OK;
}

/**
 * gst_hip_host_buffer_pool_new:
 * @device: a #GstHipDevice to use
 *
 * Returns: (transfer full): a #GstBufferPool that allocates buffers with
 * #GstHipHostMemory
 *
 * Since: 1.30
 */
GstBufferPool *
gst_hip_host_buffer_pool_new (GstHipDevice * device)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), nullptr);

  auto self = (GstHipHostBufferPool *)
      g_object_new (GST_TYPE_HIP_HOST_BUFFER_POOL, nullptr);
  gst_object_ref_sink (self);

  self->device = (GstHipDevice *) gst_object_ref (device);

  return GST_BUFFER_POOL_CAST (self);
}

/**
 * gst_buffer_pool_config_set_hip_host_alloc_flags:
 * @config: a buffer pool config
 * @flags: flags value for hipHostMalloc
 *
 * Sets flags value for hipHostMalloc
 *
 * Since: 1.30
 */
void
gst_buffer_pool_config_set_hip_host_alloc_flags (GstStructure * config,
    guint flags)
{
  g_return_if_fail (config);

  gst_structure_set (config,
      "hip-host-alloc-flags", G_TYPE_UINT, flags, nullptr);
}

/**
 * gst_buffer_pool_config_get_hip_host_alloc_flags:
 * @config: a buffer pool config
 *
 * Returns: Requested flags value for hipHostMalloc
 *
 * Since: 1.30
 */
guint
gst_buffer_pool_config_get_hip_host_alloc_flags (GstStructure * config)
{
  g_return_val_if_fail (config, 0);

  guint flags = 0;
  if (gst_structure_get_uint (config, "hip-host-alloc-flags", &flags))
    return flags;

  return 0;
}
