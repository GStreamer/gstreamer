/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d11stagingbufferpool.h"
#include "gstd3d11memory.h"
#include "gstd3d11device.h"
#include "gstd3d11utils.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_staging_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_d3d11_staging_buffer_pool_debug

struct _GstD3D11StagingBufferPoolPrivate
{
  GstVideoInfo info;

  D3D11_TEXTURE2D_DESC desc[GST_VIDEO_MAX_PLANES];
  GstD3D11Allocator *alloc[GST_VIDEO_MAX_PLANES];
  gint stride[GST_VIDEO_MAX_PLANES];
  gsize offset[GST_VIDEO_MAX_PLANES];
};

#define gst_d3d11_staging_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D11StagingBufferPool,
    gst_d3d11_staging_buffer_pool, GST_TYPE_BUFFER_POOL);

static void gst_d3d11_staging_buffer_pool_dispose (GObject * object);
static const gchar **gst_d3d11_staging_buffer_pool_get_options (GstBufferPool *
    pool);
static gboolean gst_d3d11_staging_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config);
static GstFlowReturn gst_d3d11_staging_buffer_pool_alloc_buffer (GstBufferPool *
    pool, GstBuffer ** buffer, GstBufferPoolAcquireParams * params);
static gboolean gst_d3d11_staging_buffer_pool_start (GstBufferPool * pool);
static gboolean gst_d3d11_staging_buffer_pool_stop (GstBufferPool * pool);

static void
gst_d3d11_staging_buffer_pool_class_init (GstD3D11StagingBufferPoolClass *
    klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *bufferpool_class = GST_BUFFER_POOL_CLASS (klass);

  gobject_class->dispose = gst_d3d11_staging_buffer_pool_dispose;

  bufferpool_class->get_options = gst_d3d11_staging_buffer_pool_get_options;
  bufferpool_class->set_config = gst_d3d11_staging_buffer_pool_set_config;
  bufferpool_class->alloc_buffer = gst_d3d11_staging_buffer_pool_alloc_buffer;
  bufferpool_class->start = gst_d3d11_staging_buffer_pool_start;
  bufferpool_class->stop = gst_d3d11_staging_buffer_pool_stop;

  GST_DEBUG_CATEGORY_INIT (gst_d3d11_staging_buffer_pool_debug,
      "d3d11stagingbufferpool", 0, "d3d11stagingbufferpool object");
}

static void
gst_d3d11_staging_buffer_pool_init (GstD3D11StagingBufferPool * self)
{
  self->priv = (GstD3D11StagingBufferPoolPrivate *)
      gst_d3d11_staging_buffer_pool_get_instance_private (self);
}

static void
gst_d3d11_staging_buffer_pool_clear_allocator (GstD3D11StagingBufferPool * self)
{
  GstD3D11StagingBufferPoolPrivate *priv = self->priv;

  for (guint i = 0; i < G_N_ELEMENTS (priv->alloc); i++) {
    if (priv->alloc[i]) {
      gst_d3d11_allocator_set_active (priv->alloc[i], FALSE);
      gst_clear_object (&priv->alloc[i]);
    }
  }
}

static void
gst_d3d11_staging_buffer_pool_dispose (GObject * object)
{
  GstD3D11StagingBufferPool *self = GST_D3D11_STAGING_BUFFER_POOL (object);

  gst_clear_object (&self->device);
  gst_d3d11_staging_buffer_pool_clear_allocator (self);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static const gchar **
gst_d3d11_staging_buffer_pool_get_options (GstBufferPool * pool)
{
  /* NOTE: d3d11 memory does not support alignment */
  static const gchar *options[] =
      { GST_BUFFER_POOL_OPTION_VIDEO_META, nullptr };

  return options;
}

static gboolean
gst_d3d11_staging_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config)
{
  GstD3D11StagingBufferPool *self = GST_D3D11_STAGING_BUFFER_POOL (pool);
  GstD3D11StagingBufferPoolPrivate *priv = self->priv;
  GstVideoInfo info;
  GstCaps *caps = nullptr;
  guint min_buffers, max_buffers;
  D3D11_TEXTURE2D_DESC *desc;
  const GstD3D11Format *format;
  gsize offset = 0;

  if (!gst_buffer_pool_config_get_params (config, &caps, nullptr, &min_buffers,
          &max_buffers))
    goto wrong_config;

  if (caps == nullptr)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps))
    goto wrong_caps;

  format = gst_d3d11_device_format_from_gst (self->device,
      GST_VIDEO_INFO_FORMAT (&info));
  if (!format)
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, info.width, info.height,
      caps);

  gst_d3d11_staging_buffer_pool_clear_allocator (self);

  memset (priv->stride, 0, sizeof (priv->stride));
  memset (priv->offset, 0, sizeof (priv->offset));
  memset (priv->desc, 0, sizeof (priv->desc));

  if (format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++) {
      desc = &priv->desc[i];

      desc->Width = GST_VIDEO_INFO_COMP_WIDTH (&info, i);
      desc->Height = GST_VIDEO_INFO_COMP_HEIGHT (&info, i);
      desc->MipLevels = 1;
      desc->ArraySize = 1;
      desc->Format = format->resource_format[i];
      desc->SampleDesc.Count = 1;
      desc->Usage = D3D11_USAGE_STAGING;
      desc->CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    }
  } else {
    guint width, height;

    desc = &priv->desc[0];

    width = GST_VIDEO_INFO_WIDTH (&info);
    height = GST_VIDEO_INFO_HEIGHT (&info);

    /* resolution of semi-planar formats must be multiple of 2 */
    switch (format->dxgi_format) {
      case DXGI_FORMAT_NV12:
      case DXGI_FORMAT_P010:
      case DXGI_FORMAT_P016:
        width = GST_ROUND_UP_2 (width);
        height = GST_ROUND_UP_2 (height);
        break;
      default:
        break;
    }

    desc->Width = width;
    desc->Height = height;
    desc->MipLevels = 1;
    desc->ArraySize = 1;
    desc->Format = format->dxgi_format;
    desc->SampleDesc.Count = 1;
    desc->Usage = D3D11_USAGE_STAGING;
    desc->CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
  }

  offset = 0;
  for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    GstD3D11Allocator *alloc;
    GstD3D11PoolAllocator *pool_alloc;
    GstFlowReturn flow_ret;
    GstMemory *mem = nullptr;
    guint stride = 0;

    if (priv->desc[i].Format == DXGI_FORMAT_UNKNOWN)
      break;

    alloc =
        (GstD3D11Allocator *) gst_d3d11_pool_allocator_new (self->device,
        &priv->desc[i]);
    if (!gst_d3d11_allocator_set_active (alloc, TRUE)) {
      GST_ERROR_OBJECT (self, "Failed to activate allocator");
      gst_object_unref (alloc);
      return FALSE;
    }

    pool_alloc = GST_D3D11_POOL_ALLOCATOR (alloc);
    flow_ret = gst_d3d11_pool_allocator_acquire_memory (pool_alloc, &mem);
    if (flow_ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "Failed to allocate initial memory");
      gst_d3d11_allocator_set_active (alloc, FALSE);
      gst_object_unref (alloc);
      return FALSE;
    }

    if (!gst_d3d11_memory_get_resource_stride (GST_D3D11_MEMORY_CAST (mem),
            &stride) || stride < priv->desc[i].Width) {
      GST_ERROR_OBJECT (self, "Failed to calculate stride");

      gst_d3d11_allocator_set_active (alloc, FALSE);
      gst_object_unref (alloc);
      gst_memory_unref (mem);

      return FALSE;
    }

    priv->stride[i] = stride;
    priv->offset[i] = offset;
    offset += mem->size;

    priv->alloc[i] = alloc;

    gst_memory_unref (mem);
  }

  /* single texture semi-planar formats */
  if (format->dxgi_format != DXGI_FORMAT_UNKNOWN &&
      GST_VIDEO_INFO_N_PLANES (&info) == 2) {
    priv->stride[1] = priv->stride[0];
    priv->offset[1] = priv->stride[0] * priv->desc[0].Height;
  }

  gst_buffer_pool_config_set_params (config,
      caps, offset, min_buffers, max_buffers);

  priv->info = info;

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);

  /* ERRORS */
wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }
wrong_caps:
  {
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

static GstFlowReturn
gst_d3d11_staging_buffer_pool_fill_buffer (GstD3D11StagingBufferPool * self,
    GstBuffer * buf)
{
  GstD3D11StagingBufferPoolPrivate *priv = self->priv;
  GstFlowReturn ret = GST_FLOW_OK;

  for (guint i = 0; i < G_N_ELEMENTS (priv->alloc); i++) {
    GstMemory *mem = nullptr;
    GstD3D11PoolAllocator *alloc = GST_D3D11_POOL_ALLOCATOR (priv->alloc[i]);

    if (!alloc)
      break;

    ret = gst_d3d11_pool_allocator_acquire_memory (alloc, &mem);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (self, "Failed to acquire memory, ret %s",
          gst_flow_get_name (ret));
      return ret;
    }

    gst_buffer_append_memory (buf, mem);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_staging_buffer_pool_alloc_buffer (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstD3D11StagingBufferPool *self = GST_D3D11_STAGING_BUFFER_POOL (pool);
  GstD3D11StagingBufferPoolPrivate *priv = self->priv;
  GstVideoInfo *info = &priv->info;
  GstBuffer *buf;
  GstFlowReturn ret = GST_FLOW_OK;

  buf = gst_buffer_new ();
  ret = gst_d3d11_staging_buffer_pool_fill_buffer (self, buf);
  if (ret != GST_FLOW_OK) {
    gst_buffer_unref (buf);
    return ret;
  }

  gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
      priv->offset, priv->stride);

  *buffer = buf;

  return GST_FLOW_OK;
}

static gboolean
gst_d3d11_staging_buffer_pool_start (GstBufferPool * pool)
{
  GstD3D11StagingBufferPool *self = GST_D3D11_STAGING_BUFFER_POOL (pool);
  GstD3D11StagingBufferPoolPrivate *priv = self->priv;
  gboolean ret;

  GST_DEBUG_OBJECT (self, "Start");

  for (guint i = 0; i < G_N_ELEMENTS (priv->alloc); i++) {
    GstD3D11Allocator *alloc = priv->alloc[i];

    if (!alloc)
      break;

    if (!gst_d3d11_allocator_set_active (alloc, TRUE)) {
      GST_ERROR_OBJECT (self, "Failed to activate allocator");
      return FALSE;
    }
  }

  ret = GST_BUFFER_POOL_CLASS (parent_class)->start (pool);
  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to start");

    for (guint i = 0; i < G_N_ELEMENTS (priv->alloc); i++) {
      GstD3D11Allocator *alloc = priv->alloc[i];

      if (!alloc)
        break;

      gst_d3d11_allocator_set_active (alloc, FALSE);
    }

    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_staging_buffer_pool_stop (GstBufferPool * pool)
{
  GstD3D11StagingBufferPool *self = GST_D3D11_STAGING_BUFFER_POOL (pool);
  GstD3D11StagingBufferPoolPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  for (guint i = 0; i < G_N_ELEMENTS (priv->alloc); i++) {
    GstD3D11Allocator *alloc = priv->alloc[i];

    if (!alloc)
      break;

    if (!gst_d3d11_allocator_set_active (alloc, FALSE)) {
      GST_ERROR_OBJECT (self, "Failed to deactivate allocator");
      return FALSE;
    }
  }

  return GST_BUFFER_POOL_CLASS (parent_class)->stop (pool);
}

/**
 * gst_d3d11_staging_buffer_pool_new:
 * @device: a #GstD3D11Device to use
 *
 * Returns: a #GstBufferPool that allocates buffers with #GstD3D11Memory
 * which hold Direct3D11 staging texture allocated with D3D11_USAGE_STAGING
 * flag, instead of D3D11_USAGE_DEFAULT. The staging texture can be used for
 * optimized resource upload/download.
 *
 * Since: 1.22
 */
GstBufferPool *
gst_d3d11_staging_buffer_pool_new (GstD3D11Device * device)
{
  GstD3D11StagingBufferPool *pool;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), nullptr);

  pool = (GstD3D11StagingBufferPool *)
      g_object_new (GST_TYPE_D3D11_STAGING_BUFFER_POOL, nullptr);
  gst_object_ref_sink (pool);

  pool->device = (GstD3D11Device *) gst_object_ref (device);

  return GST_BUFFER_POOL_CAST (pool);
}
