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

#include "gstd3d12.h"
#include "gstd3d12memory-private.h"
#include <directx/d3dx12.h>

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_staging_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_d3d12_staging_buffer_pool_debug

struct _GstD3D12StagingBufferPoolPrivate
{
  GstVideoInfo info;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout[GST_VIDEO_MAX_PLANES];
  gint stride[GST_VIDEO_MAX_PLANES];
  gsize offset[GST_VIDEO_MAX_PLANES];
  gsize total_mem_size;
  guint layout_count;
};

#define gst_d3d12_staging_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D12StagingBufferPool,
    gst_d3d12_staging_buffer_pool, GST_TYPE_BUFFER_POOL);

static const gchar **gst_d3d12_staging_buffer_pool_get_options (GstBufferPool *
    pool);
static gboolean gst_d3d12_staging_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config);
static GstFlowReturn gst_d3d12_staging_buffer_pool_alloc_buffer (GstBufferPool *
    pool, GstBuffer ** buffer, GstBufferPoolAcquireParams * params);

static void
gst_d3d12_staging_buffer_pool_class_init (GstD3D12StagingBufferPoolClass *
    klass)
{
  auto pool_class = GST_BUFFER_POOL_CLASS (klass);

  pool_class->get_options = gst_d3d12_staging_buffer_pool_get_options;
  pool_class->set_config = gst_d3d12_staging_buffer_pool_set_config;
  pool_class->alloc_buffer = gst_d3d12_staging_buffer_pool_alloc_buffer;

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_staging_buffer_pool_debug,
      "d3d12stagingbufferpool", 0, "d3d12stagingbufferpool");
}

static void
gst_d3d12_staging_buffer_pool_init (GstD3D12StagingBufferPool * self)
{
  self->priv = (GstD3D12StagingBufferPoolPrivate *)
      gst_d3d12_staging_buffer_pool_get_instance_private (self);
}

static const gchar **
gst_d3d12_staging_buffer_pool_get_options (GstBufferPool * pool)
{
  /* NOTE: d3d12 memory does not support alignment */
  static const gchar *options[] =
      { GST_BUFFER_POOL_OPTION_VIDEO_META, nullptr };

  return options;
}

static void
gst_d3d12_staging_buffer_pool_do_align (D3D12_RESOURCE_DESC & desc)
{
  UINT width_align =
      D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetWidthAlignment (desc.Format);
  UINT height_align =
      D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetHeightAlignment (desc.Format);

  if (width_align > 1)
    desc.Width = GST_ROUND_UP_N (desc.Width, (UINT64) width_align);

  if (height_align > 1)
    desc.Height = GST_ROUND_UP_N (desc.Height, height_align);
}

static gboolean
gst_d3d12_staging_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config)
{
  auto self = GST_D3D12_STAGING_BUFFER_POOL (pool);
  auto priv = self->priv;
  GstCaps *caps = nullptr;
  guint min_buffers, max_buffers;

  if (!gst_buffer_pool_config_get_params (config, &caps, nullptr, &min_buffers,
          &max_buffers)) {
    GST_WARNING_OBJECT (self, "invalid config");
    return FALSE;
  }

  if (!caps) {
    GST_WARNING_OBJECT (self, "Empty caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&priv->info, caps)) {
    GST_WARNING_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "%dx%d, caps %" GST_PTR_FORMAT, priv->info.width,
      priv->info.height, caps);

  GstD3D12Format d3d12_format;
  auto format = GST_VIDEO_INFO_FORMAT (&priv->info);
  if (!gst_d3d12_device_get_format (self->device, format, &d3d12_format)) {
    GST_ERROR_OBJECT (self, "%s is not supported",
        gst_video_format_to_string (format));
    return FALSE;
  }

  memset (priv->stride, 0, sizeof (priv->stride));
  memset (priv->offset, 0, sizeof (priv->offset));
  memset (priv->layout, 0, sizeof (priv->layout));
  priv->layout_count = 0;
  priv->total_mem_size = 0;

  auto device = gst_d3d12_device_get_device_handle (self->device);

  if (d3d12_format.dxgi_format != DXGI_FORMAT_UNKNOWN) {
    auto desc = CD3DX12_RESOURCE_DESC::Tex2D (d3d12_format.dxgi_format,
        priv->info.width, priv->info.height, 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_NONE);

    gst_d3d12_staging_buffer_pool_do_align (desc);

    auto num_planes = D3D12GetFormatPlaneCount (device,
        d3d12_format.dxgi_format);

    UINT64 mem_size;
    device->GetCopyableFootprints (&desc, 0, num_planes, 0,
        priv->layout, nullptr, nullptr, &mem_size);
    for (guint i = 0; i < num_planes; i++) {
      priv->stride[i] = priv->layout[i].Footprint.RowPitch;
      priv->offset[i] = (gsize) priv->layout[i].Offset;
    }

    priv->layout_count = num_planes;
    priv->total_mem_size = mem_size;
  } else {
    auto finfo = priv->info.finfo;
    UINT64 base_offset = 0;

    for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      if (d3d12_format.resource_format[i] == DXGI_FORMAT_UNKNOWN)
        break;

      gint comp[GST_VIDEO_MAX_COMPONENTS];
      gst_video_format_info_component (finfo, i, comp);

      guint width = GST_VIDEO_INFO_COMP_WIDTH (&priv->info, comp[0]);
      guint height = GST_VIDEO_INFO_COMP_HEIGHT (&priv->info, comp[0]);
      width = MAX (width, 1);
      height = MAX (height, 1);

      auto desc = CD3DX12_RESOURCE_DESC::Tex2D (d3d12_format.resource_format[i],
          width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_NONE);

      gst_d3d12_staging_buffer_pool_do_align (desc);

      UINT64 mem_size;
      device->GetCopyableFootprints (&desc, 0, 1, base_offset,
          &priv->layout[i], nullptr, nullptr, &mem_size);

      priv->stride[i] = priv->layout[i].Footprint.RowPitch;
      priv->offset[i] = (gsize) priv->layout[i].Offset;

      base_offset += mem_size;

      priv->layout_count++;
      base_offset = GST_ROUND_UP_N (base_offset,
          (UINT64) D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    }

    priv->total_mem_size = (gsize) base_offset;
  }

  gst_buffer_pool_config_set_params (config,
      caps, priv->total_mem_size, min_buffers, max_buffers);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static GstFlowReturn
gst_d3d12_staging_buffer_pool_alloc_buffer (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  auto self = GST_D3D12_STAGING_BUFFER_POOL (pool);
  auto priv = self->priv;
  auto info = &priv->info;

  auto mem = gst_d3d12_staging_allocator_alloc (nullptr, self->device,
      priv->layout_count, priv->layout, priv->total_mem_size);
  if (!mem) {
    GST_ERROR_OBJECT (self, "Couldn't allocate memory");
    return GST_FLOW_ERROR;
  }

  auto buf = gst_buffer_new ();
  gst_buffer_append_memory (buf, mem);

  gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
      priv->offset, priv->stride);

  *buffer = buf;

  return GST_FLOW_OK;
}

/**
 * gst_d3d12_staging_buffer_pool_new:
 * @device: a #GstD3D12Device to use
 *
 * Returns: (transfer full): a #GstBufferPool that allocates buffers with
 * #GstD3D12StagingMemory
 *
 * Since: 1.28
 */
GstBufferPool *
gst_d3d12_staging_buffer_pool_new (GstD3D12Device * device)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);

  auto self = (GstD3D12StagingBufferPool *)
      g_object_new (GST_TYPE_D3D12_STAGING_BUFFER_POOL, nullptr);
  gst_object_ref_sink (self);

  self->device = (GstD3D12Device *) gst_object_ref (device);

  return GST_BUFFER_POOL_CAST (self);
}
