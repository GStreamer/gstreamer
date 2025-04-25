/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

GST_DEBUG_CATEGORY_STATIC (gst_hip_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_hip_buffer_pool_debug

struct _GstHipBufferPoolPrivate
{
  GstVideoInfo info;
  GstHipPoolAllocator *alloc = nullptr;
};

#define gst_hip_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstHipBufferPool, gst_hip_buffer_pool, GST_TYPE_BUFFER_POOL);

static void gst_hip_buffer_pool_finalize (GObject * object);
static const gchar **gst_hip_buffer_pool_get_options (GstBufferPool * pool);
static gboolean gst_hip_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config);
static gboolean gst_hip_buffer_pool_start (GstBufferPool * pool);
static gboolean gst_hip_buffer_pool_stop (GstBufferPool * pool);
static GstFlowReturn gst_hip_buffer_pool_alloc (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params);

static void
gst_hip_buffer_pool_class_init (GstHipBufferPoolClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto pool_class = GST_BUFFER_POOL_CLASS (klass);

  object_class->finalize = gst_hip_buffer_pool_finalize;

  pool_class->get_options = gst_hip_buffer_pool_get_options;
  pool_class->set_config = gst_hip_buffer_pool_set_config;
  pool_class->start = gst_hip_buffer_pool_start;
  pool_class->stop = gst_hip_buffer_pool_stop;
  pool_class->alloc_buffer = gst_hip_buffer_pool_alloc;

  GST_DEBUG_CATEGORY_INIT (gst_hip_buffer_pool_debug, "hipbufferpool", 0,
      "hipbufferpool");
}

static void
gst_hip_buffer_pool_init (GstHipBufferPool * self)
{
  self->priv = new GstHipBufferPoolPrivate ();
}

static void
gst_hip_buffer_pool_finalize (GObject * object)
{
  auto self = GST_HIP_BUFFER_POOL (object);
  auto priv = self->priv;

  if (priv->alloc) {
    gst_hip_allocator_set_active (GST_HIP_ALLOCATOR (priv->alloc), FALSE);
    gst_clear_object (&priv->alloc);
  }

  gst_clear_object (&self->device);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static const gchar **
gst_hip_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, nullptr
  };

  return options;
}

static gboolean
gst_hip_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  auto self = GST_HIP_BUFFER_POOL (pool);
  auto priv = self->priv;
  GstCaps *caps = nullptr;
  guint size, min_buffers, max_buffers;
  GstVideoInfo info;
  GstMemory *mem = nullptr;

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers)) {
    GST_WARNING_OBJECT (self, "invalid config");
    return FALSE;
  }

  if (!caps) {
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to convert caps to video-info");
    return FALSE;
  }

  if (priv->alloc) {
    gst_hip_allocator_set_active (GST_HIP_ALLOCATOR (priv->alloc), FALSE);
    gst_clear_object (&priv->alloc);
  }

  priv->alloc = gst_hip_pool_allocator_new (self->device, &info, nullptr);

  if (!priv->alloc) {
    GST_ERROR_OBJECT (self, "Couldn't create allocator");
    return FALSE;
  }

  if (!gst_hip_allocator_set_active (GST_HIP_ALLOCATOR (priv->alloc), TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't set active");
    return FALSE;
  }

  gst_hip_pool_allocator_acquire_memory (priv->alloc, &mem);
  gst_hip_allocator_set_active (GST_HIP_ALLOCATOR (priv->alloc), FALSE);
  if (!mem) {
    GST_WARNING_OBJECT (self, "Failed to allocate memory");
    return FALSE;
  }

  auto hmem = GST_HIP_MEMORY_CAST (mem);

  gst_buffer_pool_config_set_params (config, caps,
      GST_VIDEO_INFO_SIZE (&hmem->info), min_buffers, max_buffers);

  priv->info = info;

  gst_memory_unref (mem);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static GstFlowReturn
gst_hip_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  auto self = GST_HIP_BUFFER_POOL (pool);
  auto priv = self->priv;
  GstVideoInfo *info = &priv->info;
  GstMemory *mem;
  GstFlowReturn ret;

  ret = gst_hip_pool_allocator_acquire_memory (priv->alloc, &mem);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Couldn't acquire memory");
    return ret;
  }

  auto buf = gst_buffer_new ();
  gst_buffer_append_memory (buf, mem);

  auto hmem = GST_HIP_MEMORY_CAST (mem);
  gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
      hmem->info.offset, hmem->info.stride);

  *buffer = buf;

  return GST_FLOW_OK;
}

static gboolean
gst_hip_buffer_pool_start (GstBufferPool * pool)
{
  auto self = GST_HIP_BUFFER_POOL (pool);
  auto priv = self->priv;

  if (!gst_hip_allocator_set_active (GST_HIP_ALLOCATOR (priv->alloc), TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't activate allocator");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_hip_buffer_pool_stop (GstBufferPool * pool)
{
  auto self = GST_HIP_BUFFER_POOL (pool);
  auto priv = self->priv;

  if (priv->alloc)
    gst_hip_allocator_set_active (GST_HIP_ALLOCATOR (priv->alloc), FALSE);

  return GST_BUFFER_POOL_CLASS (parent_class)->stop (pool);
}

GstBufferPool *
gst_hip_buffer_pool_new (GstHipDevice * device)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), nullptr);

  auto self = (GstHipBufferPool *)
      g_object_new (GST_TYPE_HIP_BUFFER_POOL, nullptr);
  gst_object_ref_sink (self);

  self->device = (GstHipDevice *) gst_object_ref (device);

  return GST_BUFFER_POOL_CAST (self);
}
