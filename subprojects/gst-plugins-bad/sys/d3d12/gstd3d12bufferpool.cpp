/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12device.h"
#include "gstd3d12bufferpool.h"
#include "gstd3d12utils.h"

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_d3d12_buffer_pool_debug

struct _GstD3D12BufferPoolPrivate
{
  GstD3D12Allocator *alloc[GST_VIDEO_MAX_PLANES];

  GstD3D12AllocationParams *d3d12_params;

  gint stride[GST_VIDEO_MAX_PLANES];
  gsize offset[GST_VIDEO_MAX_PLANES];
};

#define gst_d3d12_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D12BufferPool,
    gst_d3d12_buffer_pool, GST_TYPE_BUFFER_POOL);

static void gst_d3d12_buffer_pool_dispose (GObject * object);
static const gchar **gst_d3d12_buffer_pool_get_options (GstBufferPool * pool);
static gboolean gst_d3d12_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config);
static GstFlowReturn gst_d3d12_buffer_pool_alloc_buffer (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params);
static gboolean gst_d3d12_buffer_pool_start (GstBufferPool * pool);
static gboolean gst_d3d12_buffer_pool_stop (GstBufferPool * pool);

static void
gst_d3d12_buffer_pool_class_init (GstD3D12BufferPoolClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *bufferpool_class = GST_BUFFER_POOL_CLASS (klass);

  gobject_class->dispose = gst_d3d12_buffer_pool_dispose;

  bufferpool_class->get_options = gst_d3d12_buffer_pool_get_options;
  bufferpool_class->set_config = gst_d3d12_buffer_pool_set_config;
  bufferpool_class->alloc_buffer = gst_d3d12_buffer_pool_alloc_buffer;
  bufferpool_class->start = gst_d3d12_buffer_pool_start;
  bufferpool_class->stop = gst_d3d12_buffer_pool_stop;

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_buffer_pool_debug, "d3d12bufferpool", 0,
      "d3d12bufferpool");
}

static void
gst_d3d12_buffer_pool_init (GstD3D12BufferPool * self)
{
  self->priv = (GstD3D12BufferPoolPrivate *)
      gst_d3d12_buffer_pool_get_instance_private (self);
}

static void
gst_d3d12_buffer_pool_clear_allocator (GstD3D12BufferPool * self)
{
  GstD3D12BufferPoolPrivate *priv = self->priv;
  guint i;

  for (i = 0; i < G_N_ELEMENTS (priv->alloc); i++) {
    if (priv->alloc[i]) {
      gst_d3d12_allocator_set_active (priv->alloc[i], FALSE);
      gst_clear_object (&priv->alloc[i]);
    }
  }
}

static void
gst_d3d12_buffer_pool_dispose (GObject * object)
{
  GstD3D12BufferPool *self = GST_D3D12_BUFFER_POOL (object);
  GstD3D12BufferPoolPrivate *priv = self->priv;

  g_clear_pointer (&priv->d3d12_params, gst_d3d12_allocation_params_free);
  gst_clear_object (&self->device);
  gst_d3d12_buffer_pool_clear_allocator (self);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static const gchar **
gst_d3d12_buffer_pool_get_options (GstBufferPool * pool)
{
  /* NOTE: d3d12 memory does not support alignment */
  static const gchar *options[] =
      { GST_BUFFER_POOL_OPTION_VIDEO_META, nullptr };

  return options;
}

static gboolean
gst_d3d12_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstD3D12BufferPool *self = GST_D3D12_BUFFER_POOL (pool);
  GstD3D12BufferPoolPrivate *priv = self->priv;
  GstVideoInfo info;
  GstCaps *caps = nullptr;
  guint min_buffers, max_buffers;
  gboolean ret = TRUE;
  guint align = 0;
  D3D12_RESOURCE_DESC *desc;
  D3D12_HEAP_PROPERTIES heap_props =
      CD3D12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
  guint plane_index = 0;
  gsize mem_size = 0;

  if (!gst_buffer_pool_config_get_params (config, &caps, nullptr, &min_buffers,
          &max_buffers)) {
    GST_WARNING_OBJECT (self, "invalid config");
    return FALSE;
  }

  if (!caps) {
    GST_WARNING_OBJECT (self, "Empty caps");
    return FALSE;
  }

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "%dx%d, caps %" GST_PTR_FORMAT, info.width, info.height,
      caps);

  gst_d3d12_buffer_pool_clear_allocator (self);

  memset (priv->stride, 0, sizeof (priv->stride));
  memset (priv->offset, 0, sizeof (priv->offset));

  g_clear_pointer (&priv->d3d12_params, gst_d3d12_allocation_params_free);
  priv->d3d12_params =
      gst_buffer_pool_config_get_d3d12_allocation_params (config);
  if (!priv->d3d12_params) {
    priv->d3d12_params =
        gst_d3d12_allocation_params_new (self->device,
        &info, GST_D3D12_ALLOCATION_FLAG_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
  }

  desc = priv->d3d12_params->desc;
  switch (desc[0].Format) {
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
      align = 2;
      break;
    default:
      break;
  }

  /* resolution of semi-planar formats must be multiple of 2 */
  if (align != 0 && (desc[0].Width % align || desc[0].Height % align)) {
    gint width, height;
    GstVideoAlignment video_align;

    GST_WARNING_OBJECT (self, "Resolution %" G_GUINT64_FORMAT
        "x%d is not mutiple of %d, fixing", desc[0].Width, desc[0].Height,
        align);

    width = GST_ROUND_UP_N ((guint) desc[0].Width, align);
    height = GST_ROUND_UP_N ((guint) desc[0].Height, align);

    gst_video_alignment_reset (&video_align);
    video_align.padding_right = width - desc[0].Width;
    video_align.padding_bottom = height - desc[0].Height;

    gst_d3d12_allocation_params_alignment (priv->d3d12_params, &video_align);
  }

  if ((priv->d3d12_params->flags & GST_D3D12_ALLOCATION_FLAG_TEXTURE_ARRAY)) {
    guint max_array_size = 0;

    for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      if (desc[i].Format == DXGI_FORMAT_UNKNOWN)
        break;

      if (desc[i].DepthOrArraySize > max_array_size)
        max_array_size = desc[i].DepthOrArraySize;
    }

    if (max_buffers == 0 || max_buffers > max_array_size) {
      GST_WARNING_OBJECT (self,
          "Array pool is requested but allowed pool size %d > ArraySize %d",
          max_buffers, max_array_size);
      max_buffers = max_array_size;
    }
  }

  for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    GstD3D12Allocator *alloc;
    GstD3D12PoolAllocator *pool_alloc;
    GstFlowReturn flow_ret;
    GstMemory *mem = nullptr;
    GstD3D12Memory *dmem;
    gint stride = 0;
    gsize offset = 0;
    guint plane_count = 0;

    if (desc[i].Format == DXGI_FORMAT_UNKNOWN)
      break;

    alloc =
        (GstD3D12Allocator *) gst_d3d12_pool_allocator_new (self->device,
        &heap_props, D3D12_HEAP_FLAG_NONE, &desc[i],
        D3D12_RESOURCE_STATE_COMMON, nullptr);
    if (!gst_d3d12_allocator_set_active (alloc, TRUE)) {
      GST_ERROR_OBJECT (self, "Failed to activate allocator");
      gst_object_unref (alloc);
      return FALSE;
    }

    pool_alloc = GST_D3D12_POOL_ALLOCATOR (alloc);
    flow_ret = gst_d3d12_pool_allocator_acquire_memory (pool_alloc, &mem);
    if (flow_ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "Failed to allocate initial memory");
      gst_d3d12_allocator_set_active (alloc, FALSE);
      gst_object_unref (alloc);
      return FALSE;
    }

    dmem = GST_D3D12_MEMORY_CAST (mem);

    plane_count = gst_d3d12_memory_get_plane_count (dmem);
    for (guint j = 0; j < plane_count; j++) {
      if (!gst_d3d12_memory_get_plane_size (dmem, j,
              nullptr, nullptr, &stride, &offset)) {
        GST_ERROR_OBJECT (self, "Failed to calculate stride");

        gst_d3d12_allocator_set_active (alloc, FALSE);
        gst_object_unref (alloc);
        gst_memory_unref (mem);

        return FALSE;
      }

      g_assert (plane_index < GST_VIDEO_MAX_PLANES);
      priv->stride[plane_index] = stride;
      priv->offset[plane_index] = offset;
      plane_index++;
    }

    priv->alloc[i] = alloc;
    mem_size += mem->size;
    gst_memory_unref (mem);
  }

  gst_buffer_pool_config_set_params (config,
      caps, mem_size, min_buffers, max_buffers);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config) && ret;
}

static GstFlowReturn
gst_d3d12_buffer_pool_fill_buffer (GstD3D12BufferPool * self, GstBuffer * buf)
{
  GstD3D12BufferPoolPrivate *priv = self->priv;
  GstFlowReturn ret = GST_FLOW_OK;
  guint i;

  for (i = 0; i < G_N_ELEMENTS (priv->alloc); i++) {
    GstMemory *mem = nullptr;
    GstD3D12PoolAllocator *alloc = GST_D3D12_POOL_ALLOCATOR (priv->alloc[i]);

    if (!alloc)
      break;

    ret = gst_d3d12_pool_allocator_acquire_memory (alloc, &mem);
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
gst_d3d12_buffer_pool_alloc_buffer (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstD3D12BufferPool *self = GST_D3D12_BUFFER_POOL (pool);
  GstD3D12BufferPoolPrivate *priv = self->priv;
  GstD3D12AllocationParams *d3d12_params = priv->d3d12_params;
  GstVideoInfo *info = &d3d12_params->info;
  GstBuffer *buf;
  GstFlowReturn ret = GST_FLOW_OK;

  buf = gst_buffer_new ();
  ret = gst_d3d12_buffer_pool_fill_buffer (self, buf);
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
gst_d3d12_buffer_pool_start (GstBufferPool * pool)
{
  GstD3D12BufferPool *self = GST_D3D12_BUFFER_POOL (pool);
  GstD3D12BufferPoolPrivate *priv = self->priv;
  guint i;
  gboolean ret;

  GST_DEBUG_OBJECT (self, "Start");

  for (i = 0; i < G_N_ELEMENTS (priv->alloc); i++) {
    GstD3D12Allocator *alloc = priv->alloc[i];

    if (!alloc)
      break;

    if (!gst_d3d12_allocator_set_active (alloc, TRUE)) {
      GST_ERROR_OBJECT (self, "Failed to activate allocator");
      return FALSE;
    }
  }

  ret = GST_BUFFER_POOL_CLASS (parent_class)->start (pool);
  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to start");

    for (i = 0; i < G_N_ELEMENTS (priv->alloc); i++) {
      GstD3D12Allocator *alloc = priv->alloc[i];

      if (!alloc)
        break;

      gst_d3d12_allocator_set_active (alloc, FALSE);
    }

    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d12_buffer_pool_stop (GstBufferPool * pool)
{
  GstD3D12BufferPool *self = GST_D3D12_BUFFER_POOL (pool);
  GstD3D12BufferPoolPrivate *priv = self->priv;
  guint i;

  GST_DEBUG_OBJECT (self, "Stop");

  for (i = 0; i < G_N_ELEMENTS (priv->alloc); i++) {
    GstD3D12Allocator *alloc = priv->alloc[i];

    if (!alloc)
      break;

    if (!gst_d3d12_allocator_set_active (alloc, FALSE)) {
      GST_ERROR_OBJECT (self, "Failed to deactivate allocator");
      return FALSE;
    }
  }

  return GST_BUFFER_POOL_CLASS (parent_class)->stop (pool);
}

GstBufferPool *
gst_d3d12_buffer_pool_new (GstD3D12Device * device)
{
  GstD3D12BufferPool *self;

  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  self = (GstD3D12BufferPool *)
      g_object_new (GST_TYPE_D3D12_BUFFER_POOL, nullptr);
  gst_object_ref_sink (self);

  self->device = (GstD3D12Device *) gst_object_ref (device);

  return GST_BUFFER_POOL_CAST (self);
}

GstD3D12AllocationParams *
gst_buffer_pool_config_get_d3d12_allocation_params (GstStructure * config)
{
  GstD3D12AllocationParams *ret = nullptr;

  if (!gst_structure_get (config, "d3d12-allocation-params",
          GST_TYPE_D3D12_ALLOCATION_PARAMS, &ret, nullptr)) {
    return nullptr;
  }

  return ret;
}

void
gst_buffer_pool_config_set_d3d12_allocation_params (GstStructure * config,
    GstD3D12AllocationParams * params)
{
  g_return_if_fail (config);
  g_return_if_fail (params);

  gst_structure_set (config, "d3d12-allocation-params",
      GST_TYPE_D3D12_ALLOCATION_PARAMS, params, nullptr);
}
