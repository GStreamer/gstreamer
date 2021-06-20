/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#include <string.h>
#include "gstd3d11memory.h"
#include "gstd3d11device.h"
#include "gstd3d11utils.h"
#include "gstd3d11_private.h"

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_allocator_debug);
#define GST_CAT_DEFAULT gst_d3d11_allocator_debug

static GstAllocator *_d3d11_memory_allocator;

/* GstD3D11AllocationParams */
static void gst_d3d11_allocation_params_init (GType type);
G_DEFINE_BOXED_TYPE_WITH_CODE (GstD3D11AllocationParams,
    gst_d3d11_allocation_params,
    (GBoxedCopyFunc) gst_d3d11_allocation_params_copy,
    (GBoxedFreeFunc) gst_d3d11_allocation_params_free,
    gst_d3d11_allocation_params_init (g_define_type_id));

/**
 * gst_d3d11_allocation_params_new:
 * @device: a #GstD3D11Device
 * @info: a #GstVideoInfo
 * @flags: a #GstD3D11AllocationFlags
 * @bind_flags: D3D11_BIND_FLAG value used for creating Direct3D11 texture
 *
 * Create #GstD3D11AllocationParams object which is used by #GstD3D11BufferPool
 * and #GstD3D11Allocator in order to allocate new ID3D11Texture2D
 * object with given configuration
 *
 * Returns: a #GstD3D11AllocationParams or %NULL if @info is not supported
 *
 * Since: 1.20
 */
GstD3D11AllocationParams *
gst_d3d11_allocation_params_new (GstD3D11Device * device, GstVideoInfo * info,
    GstD3D11AllocationFlags flags, guint bind_flags)
{
  GstD3D11AllocationParams *ret;
  const GstD3D11Format *d3d11_format;
  guint i;

  g_return_val_if_fail (info != NULL, NULL);

  d3d11_format = gst_d3d11_device_format_from_gst (device,
      GST_VIDEO_INFO_FORMAT (info));
  if (!d3d11_format) {
    GST_WARNING ("Couldn't get d3d11 format");
    return NULL;
  }

  ret = g_new0 (GstD3D11AllocationParams, 1);

  ret->info = *info;
  ret->aligned_info = *info;
  ret->d3d11_format = d3d11_format;

  /* Usage Flag
   * https://docs.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_usage
   *
   * +----------------------------------------------------------+
   * | Resource Usage | Default | Dynamic | Immutable | Staging |
   * +----------------+---------+---------+-----------+---------+
   * | GPU-Read       | Yes     | Yes     | Yes       | Yes     |
   * | GPU-Write      | Yes     |         |           | Yes     |
   * | CPU-Read       |         |         |           | Yes     |
   * | CPU-Write      |         | Yes     |           | Yes     |
   * +----------------------------------------------------------+
   */

  /* If corresponding dxgi format is undefined, use resource format instead */
  if (d3d11_format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
      g_assert (d3d11_format->resource_format[i] != DXGI_FORMAT_UNKNOWN);

      ret->desc[i].Width = GST_VIDEO_INFO_COMP_WIDTH (info, i);
      ret->desc[i].Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);
      ret->desc[i].MipLevels = 1;
      ret->desc[i].ArraySize = 1;
      ret->desc[i].Format = d3d11_format->resource_format[i];
      ret->desc[i].SampleDesc.Count = 1;
      ret->desc[i].SampleDesc.Quality = 0;
      ret->desc[i].Usage = D3D11_USAGE_DEFAULT;
      ret->desc[i].BindFlags = bind_flags;
    }
  } else {
    ret->desc[0].Width = GST_VIDEO_INFO_WIDTH (info);
    ret->desc[0].Height = GST_VIDEO_INFO_HEIGHT (info);
    ret->desc[0].MipLevels = 1;
    ret->desc[0].ArraySize = 1;
    ret->desc[0].Format = d3d11_format->dxgi_format;
    ret->desc[0].SampleDesc.Count = 1;
    ret->desc[0].SampleDesc.Quality = 0;
    ret->desc[0].Usage = D3D11_USAGE_DEFAULT;
    ret->desc[0].BindFlags = bind_flags;
  }

  ret->flags = flags;

  return ret;
}

/**
 * gst_d3d11_allocation_params_alignment:
 * @params: a #GstD3D11AllocationParams
 * @align: a #GstVideoAlignment
 *
 * Adjust Width and Height fields of D3D11_TEXTURE2D_DESC with given
 * @align
 *
 * Returns: %TRUE if alignment could be applied
 *
 * Since: 1.20
 */
gboolean
gst_d3d11_allocation_params_alignment (GstD3D11AllocationParams * params,
    GstVideoAlignment * align)
{
  guint i;
  guint padding_width, padding_height;
  GstVideoInfo *info;
  GstVideoInfo new_info;

  g_return_val_if_fail (params != NULL, FALSE);
  g_return_val_if_fail (align != NULL, FALSE);

  /* d3d11 does not support stride align. Consider padding only */
  padding_width = align->padding_left + align->padding_right;
  padding_height = align->padding_top + align->padding_bottom;

  info = &params->info;

  if (!gst_video_info_set_format (&new_info, GST_VIDEO_INFO_FORMAT (info),
          GST_VIDEO_INFO_WIDTH (info) + padding_width,
          GST_VIDEO_INFO_HEIGHT (info) + padding_height)) {
    GST_WARNING ("Set format fail");
    return FALSE;
  }

  params->aligned_info = new_info;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    params->desc[i].Width = GST_VIDEO_INFO_COMP_WIDTH (&new_info, i);
    params->desc[i].Height = GST_VIDEO_INFO_COMP_HEIGHT (&new_info, i);
  }

  return TRUE;
}

/**
 * gst_d3d11_allocation_params_copy:
 * @src: a #GstD3D11AllocationParams
 *
 * Returns: a copy of @src
 *
 * Since: 1.20
 */
GstD3D11AllocationParams *
gst_d3d11_allocation_params_copy (GstD3D11AllocationParams * src)
{
  GstD3D11AllocationParams *dst;

  g_return_val_if_fail (src != NULL, NULL);

  dst = g_new0 (GstD3D11AllocationParams, 1);
  memcpy (dst, src, sizeof (GstD3D11AllocationParams));

  return dst;
}

/**
 * gst_d3d11_allocation_params_free:
 * @params: a #GstD3D11AllocationParams
 *
 * Free @params
 *
 * Since: 1.20
 */
void
gst_d3d11_allocation_params_free (GstD3D11AllocationParams * params)
{
  g_free (params);
}

static gint
gst_d3d11_allocation_params_compare (const GstD3D11AllocationParams * p1,
    const GstD3D11AllocationParams * p2)
{
  g_return_val_if_fail (p1 != NULL, -1);
  g_return_val_if_fail (p2 != NULL, -1);

  if (p1 == p2)
    return 0;

  return -1;
}

static void
gst_d3d11_allocation_params_init (GType type)
{
  static GstValueTable table = {
    0, (GstValueCompareFunc) gst_d3d11_allocation_params_compare,
    NULL, NULL
  };

  table.type = type;
  gst_value_register (&table);
}

/* GstD3D11Memory */
#define GST_D3D11_MEMORY_GET_LOCK(m) (&(GST_D3D11_MEMORY_CAST(m)->priv->lock))
#define GST_D3D11_MEMORY_LOCK(m) G_STMT_START { \
  GST_TRACE("Locking %p from thread %p", (m), g_thread_self()); \
  g_mutex_lock(GST_D3D11_MEMORY_GET_LOCK(m)); \
  GST_TRACE("Locked %p from thread %p", (m), g_thread_self()); \
} G_STMT_END

#define GST_D3D11_MEMORY_UNLOCK(m) G_STMT_START { \
  GST_TRACE("Unlocking %p from thread %p", (m), g_thread_self()); \
  g_mutex_unlock(GST_D3D11_MEMORY_GET_LOCK(m)); \
} G_STMT_END

struct _GstD3D11MemoryPrivate
{
  ID3D11Texture2D *texture;
  ID3D11Texture2D *staging;

  D3D11_TEXTURE2D_DESC desc;

  guint subresource_index;

  ID3D11ShaderResourceView *shader_resource_view[GST_VIDEO_MAX_PLANES];
  guint num_shader_resource_views;

  ID3D11RenderTargetView *render_target_view[GST_VIDEO_MAX_PLANES];
  guint num_render_target_views;

  ID3D11VideoDecoderOutputView *decoder_output_view;
  ID3D11VideoProcessorInputView *processor_input_view;
  ID3D11VideoProcessorOutputView *processor_output_view;

  D3D11_MAPPED_SUBRESOURCE map;


  GMutex lock;
  gint cpu_map_count;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstD3D11Memory, gst_d3d11_memory);

static inline D3D11_MAP
gst_d3d11_map_flags_to_d3d11 (GstMapFlags flags)
{
  if ((flags & GST_MAP_READWRITE) == GST_MAP_READWRITE)
    return D3D11_MAP_READ_WRITE;
  else if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE)
    return D3D11_MAP_WRITE;
  else if ((flags & GST_MAP_READ) == GST_MAP_READ)
    return D3D11_MAP_READ;
  else
    g_assert_not_reached ();

  return D3D11_MAP_READ;
}

static ID3D11Texture2D *
gst_d3d11_allocate_staging_texture (GstD3D11Device * device,
    const D3D11_TEXTURE2D_DESC * ref)
{
  D3D11_TEXTURE2D_DESC desc = { 0, };
  ID3D11Texture2D *texture = NULL;
  ID3D11Device *device_handle = gst_d3d11_device_get_device_handle (device);
  HRESULT hr;

  desc.Width = ref->Width;
  desc.Height = ref->Height;
  desc.MipLevels = 1;
  desc.Format = ref->Format;
  desc.SampleDesc.Count = 1;
  desc.ArraySize = 1;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = (D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE);

  hr = device_handle->CreateTexture2D (&desc, NULL, &texture);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (device, "Failed to create texture");
    return NULL;
  }

  return texture;
}

/* Must be called with d3d11 device lock */
static gboolean
gst_d3d11_memory_map_cpu_access (GstD3D11Memory * dmem, D3D11_MAP map_type)
{
  GstD3D11MemoryPrivate *priv = dmem->priv;
  HRESULT hr;
  ID3D11Resource *staging = (ID3D11Resource *) priv->staging;
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context_handle (dmem->device);

  hr = device_context->Map (staging, 0, map_type, 0, &priv->map);

  if (!gst_d3d11_result (hr, dmem->device)) {
    GST_ERROR_OBJECT (GST_MEMORY_CAST (dmem)->allocator,
        "Failed to map staging texture (0x%x)", (guint) hr);
    return FALSE;
  }

  return TRUE;
}

/* Must be called with d3d11 device lock */
static void
gst_d3d11_memory_upload (GstD3D11Memory * dmem)
{
  GstD3D11MemoryPrivate *priv = dmem->priv;
  ID3D11DeviceContext *device_context;

  if (!priv->staging || priv->staging == priv->texture ||
      !GST_MEMORY_FLAG_IS_SET (dmem, GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD))
    return;

  device_context = gst_d3d11_device_get_device_context_handle (dmem->device);
  device_context->CopySubresourceRegion (priv->texture, priv->subresource_index,
      0, 0, 0, priv->staging, 0, NULL);
}

/* Must be called with d3d11 device lock */
static void
gst_d3d11_memory_download (GstD3D11Memory * dmem)
{
  GstD3D11MemoryPrivate *priv = dmem->priv;
  ID3D11DeviceContext *device_context;

  if (!priv->staging || priv->staging == priv->texture ||
      !GST_MEMORY_FLAG_IS_SET (dmem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD))
    return;

  device_context = gst_d3d11_device_get_device_context_handle (dmem->device);
  device_context->CopySubresourceRegion (priv->staging, 0, 0, 0, 0,
      priv->texture, priv->subresource_index, NULL);
}

static gpointer
gst_d3d11_memory_map_full (GstMemory * mem, GstMapInfo * info, gsize maxsize)
{
  GstD3D11Memory *dmem = GST_D3D11_MEMORY_CAST (mem);
  GstD3D11MemoryPrivate *priv = dmem->priv;
  GstMapFlags flags = info->flags;
  gpointer ret = NULL;

  gst_d3d11_device_lock (dmem->device);
  GST_D3D11_MEMORY_LOCK (dmem);

  if ((flags & GST_MAP_D3D11) == GST_MAP_D3D11) {
    gst_d3d11_memory_upload (dmem);
    GST_MEMORY_FLAG_UNSET (dmem, GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD);

    if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (dmem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);

    g_assert (priv->texture != NULL);
    ret = priv->texture;
    goto out;
  }

  if (priv->cpu_map_count == 0) {
    D3D11_MAP map_type;

    /* Allocate staging texture for CPU access */
    if (!priv->staging) {
      priv->staging = gst_d3d11_allocate_staging_texture (dmem->device,
          &priv->desc);
      if (!priv->staging) {
        GST_ERROR_OBJECT (mem->allocator, "Couldn't create staging texture");
        goto out;
      }

      /* first memory, always need download to staging */
      GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
    }

    gst_d3d11_memory_download (dmem);
    map_type = gst_d3d11_map_flags_to_d3d11 (flags);

    if (!gst_d3d11_memory_map_cpu_access (dmem, map_type)) {
      GST_ERROR_OBJECT (mem->allocator, "Couldn't map staging texture");
      goto out;
    }
  }

  if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE) {
    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD);
  }

  GST_MEMORY_FLAG_UNSET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);

  priv->cpu_map_count++;
  ret = dmem->priv->map.pData;

out:
  GST_D3D11_MEMORY_UNLOCK (dmem);
  gst_d3d11_device_unlock (dmem->device);

  return ret;
}

/* Must be called with d3d11 device lock */
static void
gst_d3d11_memory_unmap_cpu_access (GstD3D11Memory * dmem)
{
  GstD3D11MemoryPrivate *priv = dmem->priv;
  ID3D11Resource *staging = (ID3D11Resource *) priv->staging;
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context_handle (dmem->device);

  device_context->Unmap (staging, 0);
}

static void
gst_d3d11_memory_unmap_full (GstMemory * mem, GstMapInfo * info)
{
  GstD3D11Memory *dmem = GST_D3D11_MEMORY_CAST (mem);
  GstD3D11MemoryPrivate *priv = dmem->priv;

  gst_d3d11_device_lock (dmem->device);
  GST_D3D11_MEMORY_LOCK (dmem);

  if ((info->flags & GST_MAP_D3D11) == GST_MAP_D3D11) {
    if ((info->flags & GST_MAP_WRITE) == GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);

    goto out;
  }

  if ((info->flags & GST_MAP_WRITE) == GST_MAP_WRITE)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD);

  priv->cpu_map_count--;
  if (priv->cpu_map_count > 0)
    goto out;

  gst_d3d11_memory_unmap_cpu_access (dmem);

out:
  GST_D3D11_MEMORY_UNLOCK (dmem);
  gst_d3d11_device_unlock (dmem->device);
}

static GstMemory *
gst_d3d11_memory_share (GstMemory * mem, gssize offset, gssize size)
{
  /* TODO: impl. */
  return NULL;
}

static gboolean
gst_d3d11_memory_update_size (GstMemory * mem)
{
  GstD3D11Memory *dmem = GST_D3D11_MEMORY_CAST (mem);
  GstD3D11MemoryPrivate *priv = dmem->priv;
  gsize offset[GST_VIDEO_MAX_PLANES];
  gint stride[GST_VIDEO_MAX_PLANES];
  gsize size;
  D3D11_TEXTURE2D_DESC *desc = &priv->desc;
  gboolean ret = FALSE;

  if (!priv->staging) {
    priv->staging = gst_d3d11_allocate_staging_texture (dmem->device,
        &priv->desc);
    if (!priv->staging) {
      GST_ERROR_OBJECT (mem->allocator, "Couldn't create staging texture");
      return FALSE;
    }

    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
  }

  gst_d3d11_device_lock (dmem->device);
  if (!gst_d3d11_memory_map_cpu_access (dmem, D3D11_MAP_READ_WRITE)) {
    GST_ERROR_OBJECT (mem->allocator, "Couldn't map staging texture");
    return FALSE;
  }

  gst_d3d11_memory_unmap_cpu_access (dmem);

  if (!gst_d3d11_dxgi_format_get_size (desc->Format, desc->Width, desc->Height,
          priv->map.RowPitch, offset, stride, &size)) {
    GST_ERROR_OBJECT (mem->allocator, "Couldn't calculate memory size");
    goto out;
  }

  mem->maxsize = mem->size = size;
  ret = TRUE;

out:
  gst_d3d11_device_unlock (dmem->device);
  return ret;
}

/**
 * gst_is_d3d11_memory:
 * @mem: a #GstMemory
 *
 * Returns: whether @mem is a #GstD3D11Memory
 *
 * Since: 1.20
 */
gboolean
gst_is_d3d11_memory (GstMemory * mem)
{
  return mem != NULL && mem->allocator != NULL &&
      (GST_IS_D3D11_ALLOCATOR (mem->allocator) ||
      GST_IS_D3D11_POOL_ALLOCATOR (mem->allocator));
}

/**
 * gst_d3d11_memory_init_once:
 *
 * Initializes the Direct3D11 Texture allocator. It is safe to call
 * this function multiple times. This must be called before any other
 * GstD3D11Memory operation.
 *
 * Since: 1.20
 */
void
gst_d3d11_memory_init_once (void)
{
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {

    GST_DEBUG_CATEGORY_INIT (gst_d3d11_allocator_debug, "d3d11allocator", 0,
        "Direct3D11 Texture Allocator");

    _d3d11_memory_allocator =
        (GstAllocator *) g_object_new (GST_TYPE_D3D11_ALLOCATOR, NULL);
    gst_object_ref_sink (_d3d11_memory_allocator);

    gst_allocator_register (GST_D3D11_MEMORY_NAME, _d3d11_memory_allocator);
    g_once_init_leave (&_init, 1);
  }
}

/**
 * gst_d3d11_memory_get_texture_handle:
 * @mem: a #GstD3D11Memory
 *
 * Returns: (transfer none): a ID3D11Texture2D handle. Caller must not release
 * returned handle.
 *
 * Since: 1.20
 */
ID3D11Texture2D *
gst_d3d11_memory_get_texture_handle (GstD3D11Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), NULL);

  return mem->priv->texture;
}

/**
 * gst_d3d11_memory_get_subresource_index:
 * @mem: a #GstD3D11Memory
 *
 * Returns: subresource index corresponding to @mem.
 *
 * Since: 1.20
 */
guint
gst_d3d11_memory_get_subresource_index (GstD3D11Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), 0);

  return mem->priv->subresource_index;
}

/**
 * gst_d3d11_memory_get_texture_desc:
 * @mem: a #GstD3D11Memory
 * @desc: (out): a D3D11_TEXTURE2D_DESC
 *
 * Fill @desc with D3D11_TEXTURE2D_DESC for ID3D11Texture2D
 *
 * Returns: %TRUE if successeed
 *
 * Since: 1.20
 */
gboolean
gst_d3d11_memory_get_texture_desc (GstD3D11Memory * mem,
    D3D11_TEXTURE2D_DESC * desc)
{
  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), FALSE);
  g_return_val_if_fail (desc != NULL, FALSE);

  *desc = mem->priv->desc;

  return TRUE;
}

gboolean
gst_d3d11_memory_get_texture_stride (GstD3D11Memory * mem, guint * stride)
{
  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), FALSE);
  g_return_val_if_fail (stride != NULL, FALSE);

  *stride = mem->priv->map.RowPitch;

  return TRUE;
}

static gboolean
create_shader_resource_views (GstD3D11Memory * mem)
{
  GstD3D11MemoryPrivate *priv = mem->priv;
  guint i;
  HRESULT hr;
  guint num_views = 0;
  ID3D11Device *device_handle;
  D3D11_SHADER_RESOURCE_VIEW_DESC resource_desc;
  DXGI_FORMAT formats[GST_VIDEO_MAX_PLANES] = { DXGI_FORMAT_UNKNOWN, };

  memset (&resource_desc, 0, sizeof (D3D11_SHADER_RESOURCE_VIEW_DESC));

  device_handle = gst_d3d11_device_get_device_handle (mem->device);

  switch (priv->desc.Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
      num_views = 1;
      formats[0] = priv->desc.Format;
      break;
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_YUY2:
      num_views = 1;
      formats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
      break;
    case DXGI_FORMAT_NV12:
      num_views = 2;
      formats[0] = DXGI_FORMAT_R8_UNORM;
      formats[1] = DXGI_FORMAT_R8G8_UNORM;
      break;
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
      num_views = 2;
      formats[0] = DXGI_FORMAT_R16_UNORM;
      formats[1] = DXGI_FORMAT_R16G16_UNORM;
      break;
    case DXGI_FORMAT_Y210:
      num_views = 1;
      formats[0] = DXGI_FORMAT_R16G16B16A16_UNORM;
      break;
    case DXGI_FORMAT_Y410:
      num_views = 1;
      formats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  if ((priv->desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) ==
      D3D11_BIND_SHADER_RESOURCE) {
    resource_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    resource_desc.Texture2D.MipLevels = 1;

    for (i = 0; i < num_views; i++) {
      resource_desc.Format = formats[i];
      hr = device_handle->CreateShaderResourceView (priv->texture,
          &resource_desc, &priv->shader_resource_view[i]);

      if (!gst_d3d11_result (hr, mem->device)) {
        GST_ERROR_OBJECT (GST_MEMORY_CAST (mem)->allocator,
            "Failed to create %dth resource view (0x%x)", i, (guint) hr);
        goto error;
      }
    }

    priv->num_shader_resource_views = num_views;

    return TRUE;
  }

  return FALSE;

error:
  for (i = 0; i < num_views; i++)
    GST_D3D11_CLEAR_COM (priv->shader_resource_view[i]);

  priv->num_shader_resource_views = 0;

  return FALSE;
}

static gboolean
gst_d3d11_memory_ensure_shader_resource_view (GstD3D11Memory * mem)
{
  GstD3D11MemoryPrivate *priv = mem->priv;
  gboolean ret = FALSE;

  if (!(priv->desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
    GST_LOG_OBJECT (GST_MEMORY_CAST (mem)->allocator,
        "Need BindFlags, current flag 0x%x", priv->desc.BindFlags);
    return FALSE;
  }

  GST_D3D11_MEMORY_LOCK (mem);
  if (priv->num_shader_resource_views) {
    ret = TRUE;
    goto done;
  }

  ret = create_shader_resource_views (mem);

done:
  GST_D3D11_MEMORY_UNLOCK (mem);

  return ret;
}

/**
 * gst_d3d11_memory_get_shader_resource_view_size:
 * @mem: a #GstD3D11Memory
 *
 * Returns: the number of ID3D11ShaderResourceView that can be used
 * for processing GPU operation with @mem
 *
 * Since: 1.20
 */
guint
gst_d3d11_memory_get_shader_resource_view_size (GstD3D11Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), 0);

  if (!gst_d3d11_memory_ensure_shader_resource_view (mem))
    return 0;

  return mem->priv->num_shader_resource_views;
}

/**
 * gst_d3d11_memory_get_shader_resource_view:
 * @mem: a #GstD3D11Memory
 * @index: the index of the ID3D11ShaderResourceView
 *
 * Returns: (transfer none) (nullable): a pointer to the
 * ID3D11ShaderResourceView or %NULL if ID3D11ShaderResourceView is unavailable
 * for @index
 *
 * Since: 1.20
 */
ID3D11ShaderResourceView *
gst_d3d11_memory_get_shader_resource_view (GstD3D11Memory * mem, guint index)
{
  GstD3D11MemoryPrivate *priv;

  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), NULL);

  if (!gst_d3d11_memory_ensure_shader_resource_view (mem))
    return NULL;

  priv = mem->priv;

  if (index >= priv->num_shader_resource_views) {
    GST_ERROR ("Invalid SRV index %d", index);
    return NULL;
  }

  return priv->shader_resource_view[index];
}

static gboolean
create_render_target_views (GstD3D11Memory * mem)
{
  GstD3D11MemoryPrivate *priv = mem->priv;
  guint i;
  HRESULT hr;
  guint num_views = 0;
  ID3D11Device *device_handle;
  D3D11_RENDER_TARGET_VIEW_DESC render_desc;
  DXGI_FORMAT formats[GST_VIDEO_MAX_PLANES] = { DXGI_FORMAT_UNKNOWN, };

  memset (&render_desc, 0, sizeof (D3D11_RENDER_TARGET_VIEW_DESC));

  device_handle = gst_d3d11_device_get_device_handle (mem->device);

  switch (priv->desc.Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
      num_views = 1;
      formats[0] = priv->desc.Format;
      break;
    case DXGI_FORMAT_AYUV:
      num_views = 1;
      formats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
      break;
    case DXGI_FORMAT_NV12:
      num_views = 2;
      formats[0] = DXGI_FORMAT_R8_UNORM;
      formats[1] = DXGI_FORMAT_R8G8_UNORM;
      break;
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
      num_views = 2;
      formats[0] = DXGI_FORMAT_R16_UNORM;
      formats[1] = DXGI_FORMAT_R16G16_UNORM;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  if ((priv->desc.BindFlags & D3D11_BIND_RENDER_TARGET) ==
      D3D11_BIND_RENDER_TARGET) {
    render_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    render_desc.Texture2D.MipSlice = 0;

    for (i = 0; i < num_views; i++) {
      render_desc.Format = formats[i];

      hr = device_handle->CreateRenderTargetView (priv->texture, &render_desc,
          &priv->render_target_view[i]);
      if (!gst_d3d11_result (hr, mem->device)) {
        GST_ERROR_OBJECT (GST_MEMORY_CAST (mem)->allocator,
            "Failed to create %dth render target view (0x%x)", i, (guint) hr);
        goto error;
      }
    }

    priv->num_render_target_views = num_views;

    return TRUE;
  }

  return FALSE;

error:
  for (i = 0; i < num_views; i++)
    GST_D3D11_CLEAR_COM (priv->render_target_view[i]);

  priv->num_render_target_views = 0;

  return FALSE;
}

static gboolean
gst_d3d11_memory_ensure_render_target_view (GstD3D11Memory * mem)
{
  GstD3D11MemoryPrivate *priv = mem->priv;
  gboolean ret = FALSE;

  if (!(priv->desc.BindFlags & D3D11_BIND_RENDER_TARGET)) {
    GST_WARNING_OBJECT (GST_MEMORY_CAST (mem)->allocator,
        "Need BindFlags, current flag 0x%x", priv->desc.BindFlags);
    return FALSE;
  }

  GST_D3D11_MEMORY_LOCK (mem);
  if (priv->num_render_target_views) {
    ret = TRUE;
    goto done;
  }

  ret = create_render_target_views (mem);

done:
  GST_D3D11_MEMORY_UNLOCK (mem);

  return ret;
}

/**
 * gst_d3d11_memory_get_render_target_view_size:
 * @mem: a #GstD3D11Memory
 *
 * Returns: the number of ID3D11RenderTargetView that can be used
 * for processing GPU operation with @mem
 *
 * Since: 1.20
 */
guint
gst_d3d11_memory_get_render_target_view_size (GstD3D11Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), 0);

  if (!gst_d3d11_memory_ensure_render_target_view (mem))
    return 0;

  return mem->priv->num_render_target_views;
}

/**
 * gst_d3d11_memory_get_render_target_view:
 * @mem: a #GstD3D11Memory
 * @index: the index of the ID3D11RenderTargetView
 *
 * Returns: (transfer none) (nullable): a pointer to the
 * ID3D11RenderTargetView or %NULL if ID3D11RenderTargetView is unavailable
 * for @index
 *
 * Since: 1.20
 */
ID3D11RenderTargetView *
gst_d3d11_memory_get_render_target_view (GstD3D11Memory * mem, guint index)
{
  GstD3D11MemoryPrivate *priv;

  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), NULL);

  if (!gst_d3d11_memory_ensure_render_target_view (mem))
    return NULL;

  priv = mem->priv;

  if (index >= priv->num_render_target_views) {
    GST_ERROR ("Invalid RTV index %d", index);
    return NULL;
  }

  return priv->render_target_view[index];
}

static gboolean
gst_d3d11_memory_ensure_decoder_output_view (GstD3D11Memory * mem,
    ID3D11VideoDevice * video_device, GUID * decoder_profile)
{
  GstD3D11MemoryPrivate *dmem_priv = mem->priv;
  GstD3D11Allocator *allocator;
  D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC desc;
  HRESULT hr;
  gboolean ret = FALSE;

  allocator = GST_D3D11_ALLOCATOR (GST_MEMORY_CAST (mem)->allocator);

  if (!(dmem_priv->desc.BindFlags & D3D11_BIND_DECODER)) {
    GST_LOG_OBJECT (allocator,
        "Need BindFlags, current flag 0x%x", dmem_priv->desc.BindFlags);
    return FALSE;
  }

  GST_D3D11_MEMORY_LOCK (mem);
  if (dmem_priv->decoder_output_view) {
    dmem_priv->decoder_output_view->GetDesc (&desc);
    if (IsEqualGUID (desc.DecodeProfile, *decoder_profile)) {
      goto succeeded;
    } else {
      /* Shouldn't happen, but try again anyway */
      GST_WARNING_OBJECT (allocator,
          "Existing view has different decoder profile");
      GST_D3D11_CLEAR_COM (dmem_priv->decoder_output_view);
    }
  }

  if (dmem_priv->decoder_output_view)
    goto succeeded;

  desc.DecodeProfile = *decoder_profile;
  desc.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;
  desc.Texture2D.ArraySlice = dmem_priv->subresource_index;

  hr = video_device->CreateVideoDecoderOutputView (dmem_priv->texture, &desc,
      &dmem_priv->decoder_output_view);
  if (!gst_d3d11_result (hr, mem->device)) {
    GST_ERROR_OBJECT (allocator,
        "Could not create decoder output view, hr: 0x%x", (guint) hr);
    goto done;
  }

succeeded:
  ret = TRUE;

done:
  GST_D3D11_MEMORY_UNLOCK (mem);

  return ret;
}

/**
 * gst_d3d11_memory_get_decoder_output_view:
 * @mem: a #GstD3D11Memory
 *
 * Returns: (transfer none) (nullable): a pointer to the
 * ID3D11VideoDecoderOutputView or %NULL if ID3D11VideoDecoderOutputView is
 * unavailable
 *
 * Since: 1.20
 */
ID3D11VideoDecoderOutputView *
gst_d3d11_memory_get_decoder_output_view (GstD3D11Memory * mem,
    ID3D11VideoDevice * video_device, GUID * decoder_profile)
{
  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), NULL);
  g_return_val_if_fail (video_device != NULL, NULL);
  g_return_val_if_fail (decoder_profile != NULL, NULL);

  if (!gst_d3d11_memory_ensure_decoder_output_view (mem,
          video_device, decoder_profile))
    return NULL;

  return mem->priv->decoder_output_view;
}

static gboolean
check_bind_flags_for_processor_input_view (guint bind_flags)
{
  static const guint compatible_flags = (D3D11_BIND_DECODER |
      D3D11_BIND_VIDEO_ENCODER | D3D11_BIND_RENDER_TARGET |
      D3D11_BIND_UNORDERED_ACCESS);

  if (bind_flags == 0)
    return TRUE;

  if ((bind_flags & compatible_flags) != 0)
    return TRUE;

  return FALSE;
}

static gboolean
gst_d3d11_memory_ensure_processor_input_view (GstD3D11Memory * mem,
    ID3D11VideoDevice * video_device,
    ID3D11VideoProcessorEnumerator * enumerator)
{
  GstD3D11MemoryPrivate *dmem_priv = mem->priv;
  GstD3D11Allocator *allocator;
  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC desc;
  HRESULT hr;
  gboolean ret = FALSE;

  allocator = GST_D3D11_ALLOCATOR (GST_MEMORY_CAST (mem)->allocator);

  if (!check_bind_flags_for_processor_input_view (dmem_priv->desc.BindFlags)) {
    GST_LOG_OBJECT (allocator,
        "Need BindFlags, current flag 0x%x", dmem_priv->desc.BindFlags);
    return FALSE;
  }

  GST_D3D11_MEMORY_LOCK (mem);
  if (dmem_priv->processor_input_view)
    goto succeeded;

  desc.FourCC = 0;
  desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  desc.Texture2D.MipSlice = 0;
  desc.Texture2D.ArraySlice = dmem_priv->subresource_index;

  hr = video_device->CreateVideoProcessorInputView (dmem_priv->texture,
      enumerator, &desc, &dmem_priv->processor_input_view);
  if (!gst_d3d11_result (hr, mem->device)) {
    GST_ERROR_OBJECT (allocator,
        "Could not create processor input view, hr: 0x%x", (guint) hr);
    goto done;
  }

succeeded:
  ret = TRUE;

done:
  GST_D3D11_MEMORY_UNLOCK (mem);

  return ret;
}

/**
 * gst_d3d11_memory_get_processor_input_view:
 * @mem: a #GstD3D11Memory
 * @video_device: a #ID3D11VideoDevice
 * @enumerator: a #ID3D11VideoProcessorEnumerator
 *
 * Returns: (transfer none) (nullable): a pointer to the
 * ID3D11VideoProcessorInputView or %NULL if ID3D11VideoProcessorInputView is
 * unavailable
 *
 * Since: 1.20
 */
ID3D11VideoProcessorInputView *
gst_d3d11_memory_get_processor_input_view (GstD3D11Memory * mem,
    ID3D11VideoDevice * video_device,
    ID3D11VideoProcessorEnumerator * enumerator)
{
  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), NULL);
  g_return_val_if_fail (video_device != NULL, NULL);
  g_return_val_if_fail (enumerator != NULL, NULL);

  if (!gst_d3d11_memory_ensure_processor_input_view (mem, video_device,
          enumerator))
    return NULL;

  return mem->priv->processor_input_view;
}

static gboolean
gst_d3d11_memory_ensure_processor_output_view (GstD3D11Memory * mem,
    ID3D11VideoDevice * video_device,
    ID3D11VideoProcessorEnumerator * enumerator)
{
  GstD3D11MemoryPrivate *priv = mem->priv;
  GstD3D11Allocator *allocator;
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC desc;
  HRESULT hr;
  gboolean ret;

  memset (&desc, 0, sizeof (D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC));

  allocator = GST_D3D11_ALLOCATOR (GST_MEMORY_CAST (mem)->allocator);

  if (!(priv->desc.BindFlags & D3D11_BIND_RENDER_TARGET)) {
    GST_LOG_OBJECT (allocator,
        "Need BindFlags, current flag 0x%x", priv->desc.BindFlags);
    return FALSE;
  }

  /* FIXME: texture array should be supported at some point */
  if (priv->subresource_index != 0) {
    GST_FIXME_OBJECT (allocator,
        "Texture array is not suppoted for processor output view");
    return FALSE;
  }

  GST_D3D11_MEMORY_LOCK (mem);
  if (priv->processor_output_view)
    goto succeeded;

  desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  desc.Texture2D.MipSlice = 0;

  hr = video_device->CreateVideoProcessorOutputView (priv->texture,
      enumerator, &desc, &priv->processor_output_view);
  if (!gst_d3d11_result (hr, mem->device)) {
    GST_ERROR_OBJECT (allocator,
        "Could not create processor input view, hr: 0x%x", (guint) hr);
    goto done;
  }

succeeded:
  ret = TRUE;

done:
  GST_D3D11_MEMORY_UNLOCK (mem);

  return ret;
}

/**
 * gst_d3d11_memory_get_processor_output_view:
 * @mem: a #GstD3D11Memory
 * @video_device: a #ID3D11VideoDevice
 * @enumerator: a #ID3D11VideoProcessorEnumerator
 *
 * Returns: (transfer none) (nullable): a pointer to the
 * ID3D11VideoProcessorOutputView or %NULL if ID3D11VideoProcessorOutputView is
 * unavailable
 *
 * Since: 1.20
 */
ID3D11VideoProcessorOutputView *
gst_d3d11_memory_get_processor_output_view (GstD3D11Memory * mem,
    ID3D11VideoDevice * video_device,
    ID3D11VideoProcessorEnumerator * enumerator)
{
  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), NULL);
  g_return_val_if_fail (video_device != NULL, NULL);
  g_return_val_if_fail (enumerator != NULL, NULL);

  if (!gst_d3d11_memory_ensure_processor_output_view (mem, video_device,
          enumerator))
    return NULL;

  return mem->priv->processor_output_view;
}

/* GstD3D11Allocator */
struct _GstD3D11AllocatorPrivate
{
  GstMemoryCopyFunction fallback_copy;
};

#define gst_d3d11_allocator_parent_class alloc_parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D11Allocator,
    gst_d3d11_allocator, GST_TYPE_ALLOCATOR);

static GstMemory *gst_d3d11_allocator_dummy_alloc (GstAllocator * allocator,
    gsize size, GstAllocationParams * params);
static GstMemory *gst_d3d11_allocator_alloc_internal (GstD3D11Allocator * self,
    GstD3D11Device * device, const D3D11_TEXTURE2D_DESC * desc);
static void gst_d3d11_allocator_free (GstAllocator * allocator,
    GstMemory * mem);

static void
gst_d3d11_allocator_class_init (GstD3D11AllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  allocator_class->alloc = gst_d3d11_allocator_dummy_alloc;
  allocator_class->free = gst_d3d11_allocator_free;
}

static GstMemory *
gst_d3d11_memory_copy (GstMemory * mem, gssize offset, gssize size)
{
  GstD3D11Allocator *alloc = GST_D3D11_ALLOCATOR (mem->allocator);
  GstD3D11AllocatorPrivate *priv = alloc->priv;
  GstD3D11Memory *dmem = GST_D3D11_MEMORY_CAST (mem);
  GstD3D11Memory *copy_dmem;
  GstD3D11Device *device = dmem->device;
  ID3D11Device *device_handle = gst_d3d11_device_get_device_handle (device);
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context_handle (device);
  D3D11_TEXTURE2D_DESC dst_desc = { 0, };
  D3D11_TEXTURE2D_DESC src_desc = { 0, };
  GstMemory *copy = NULL;
  GstMapInfo info;
  HRESULT hr;
  UINT bind_flags = 0;
  UINT supported_flags = 0;

  /* non-zero offset or different size is not supported */
  if (offset != 0 || (size != -1 && (gsize) size != mem->size)) {
    GST_DEBUG_OBJECT (alloc, "Different size/offset, try fallback copy");
    return priv->fallback_copy (mem, offset, size);
  }

  gst_d3d11_device_lock (device);
  if (!gst_memory_map (mem, &info,
          (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
    gst_d3d11_device_unlock (device);

    GST_WARNING_OBJECT (alloc, "Failed to map memory, try fallback copy");

    return priv->fallback_copy (mem, offset, size);
  }

  dmem->priv->texture->GetDesc (&src_desc);
  dst_desc.Width = src_desc.Width;
  dst_desc.Height = src_desc.Height;
  dst_desc.MipLevels = 1;
  dst_desc.Format = src_desc.Format;
  dst_desc.SampleDesc.Count = 1;
  dst_desc.ArraySize = 1;
  dst_desc.Usage = D3D11_USAGE_DEFAULT;

  /* If supported, use bind flags for SRV/RTV */
  hr = device_handle->CheckFormatSupport (src_desc.Format, &supported_flags);
  if (gst_d3d11_result (hr, device)) {
    if ((supported_flags & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) ==
        D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) {
      bind_flags |= D3D11_BIND_SHADER_RESOURCE;
    }

    if ((supported_flags & D3D11_FORMAT_SUPPORT_RENDER_TARGET) ==
        D3D11_FORMAT_SUPPORT_RENDER_TARGET) {
      bind_flags |= D3D11_BIND_RENDER_TARGET;
    }
  }

  copy = gst_d3d11_allocator_alloc_internal (alloc, device, &dst_desc);
  if (!copy) {
    gst_memory_unmap (mem, &info);
    gst_d3d11_device_unlock (device);

    GST_WARNING_OBJECT (alloc,
        "Failed to allocate new d3d11 map memory, try fallback copy");

    return priv->fallback_copy (mem, offset, size);
  }

  copy_dmem = GST_D3D11_MEMORY_CAST (copy);
  device_context->CopySubresourceRegion (copy_dmem->priv->texture, 0, 0, 0, 0,
      dmem->priv->texture, dmem->priv->subresource_index, NULL);
  copy->maxsize = copy->size = mem->maxsize;
  gst_memory_unmap (mem, &info);
  gst_d3d11_device_unlock (device);

  /* newly allocated memory holds valid image data. We need download this
   * pixel data into staging memory for CPU access */
  GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);

  return copy;
}

static void
gst_d3d11_allocator_init (GstD3D11Allocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);
  GstD3D11AllocatorPrivate *priv;

  priv = allocator->priv = (GstD3D11AllocatorPrivate *)
      gst_d3d11_allocator_get_instance_private (allocator);

  alloc->mem_type = GST_D3D11_MEMORY_NAME;
  alloc->mem_map_full = gst_d3d11_memory_map_full;
  alloc->mem_unmap_full = gst_d3d11_memory_unmap_full;
  alloc->mem_share = gst_d3d11_memory_share;

  /* Store pointer to default mem_copy method for fallback copy */
  priv->fallback_copy = alloc->mem_copy;
  alloc->mem_copy = gst_d3d11_memory_copy;

  GST_OBJECT_FLAG_SET (alloc, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static GstMemory *
gst_d3d11_allocator_dummy_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_return_val_if_reached (NULL);
}

static void
gst_d3d11_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstD3D11Memory *dmem = GST_D3D11_MEMORY_CAST (mem);
  GstD3D11MemoryPrivate *dmem_priv = dmem->priv;
  gint i;

  GST_LOG_OBJECT (allocator, "Free memory %p", mem);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    GST_D3D11_CLEAR_COM (dmem_priv->render_target_view[i]);
    GST_D3D11_CLEAR_COM (dmem_priv->shader_resource_view[i]);
  }

  GST_D3D11_CLEAR_COM (dmem_priv->decoder_output_view);
  GST_D3D11_CLEAR_COM (dmem_priv->processor_input_view);
  GST_D3D11_CLEAR_COM (dmem_priv->processor_output_view);
  GST_D3D11_CLEAR_COM (dmem_priv->texture);
  GST_D3D11_CLEAR_COM (dmem_priv->staging);

  gst_clear_object (&dmem->device);
  g_mutex_clear (&dmem_priv->lock);
  g_free (dmem->priv);
  g_free (dmem);
}

static GstMemory *
gst_d3d11_allocator_alloc_wrapped (GstD3D11Allocator * self,
    GstD3D11Device * device, const D3D11_TEXTURE2D_DESC * desc,
    ID3D11Texture2D * texture)
{
  GstD3D11Memory *mem;

  mem = g_new0 (GstD3D11Memory, 1);
  mem->priv = g_new0 (GstD3D11MemoryPrivate, 1);

  gst_memory_init (GST_MEMORY_CAST (mem),
      (GstMemoryFlags) 0, GST_ALLOCATOR_CAST (self), NULL, 0, 0, 0, 0);
  g_mutex_init (&mem->priv->lock);
  mem->priv->texture = texture;
  mem->priv->desc = *desc;
  mem->device = (GstD3D11Device *) gst_object_ref (device);

  /* This is staging texture as well */
  if (desc->Usage == D3D11_USAGE_STAGING) {
    mem->priv->staging = texture;
    texture->AddRef ();
  }

  return GST_MEMORY_CAST (mem);
}

static GstMemory *
gst_d3d11_allocator_alloc_internal (GstD3D11Allocator * self,
    GstD3D11Device * device, const D3D11_TEXTURE2D_DESC * desc)
{
  ID3D11Texture2D *texture = NULL;
  ID3D11Device *device_handle;
  HRESULT hr;

  device_handle = gst_d3d11_device_get_device_handle (device);

  hr = device_handle->CreateTexture2D (desc, NULL, &texture);
  if (!gst_d3d11_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't create texture");
    return NULL;
  }

  return gst_d3d11_allocator_alloc_wrapped (self, device, desc, texture);
}

/**
 * gst_d3d11_allocator_alloc:
 * @allocator: a #GstD3D11Allocator
 * @device: a #GstD3D11Device
 * @desc: a D3D11_TEXTURE2D_DESC struct
 *
 * Returns: a newly allocated #GstD3D11Memory with given parameters.
 *
 * Since: 1.20
 */
GstMemory *
gst_d3d11_allocator_alloc (GstD3D11Allocator * allocator,
    GstD3D11Device * device, const D3D11_TEXTURE2D_DESC * desc)
{
  GstMemory *mem;

  g_return_val_if_fail (GST_IS_D3D11_ALLOCATOR (allocator), NULL);
  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);
  g_return_val_if_fail (desc != NULL, NULL);

  mem = gst_d3d11_allocator_alloc_internal (allocator, device, desc);
  if (!mem)
    return NULL;

  if (!gst_d3d11_memory_update_size (mem)) {
    GST_ERROR_OBJECT (allocator, "Failed to calculate size");
    gst_memory_unref (mem);
    return NULL;
  }

  return mem;
}

gboolean
gst_d3d11_allocator_set_active (GstD3D11Allocator * allocator, gboolean active)
{
  GstD3D11AllocatorClass *klass;

  g_return_val_if_fail (GST_IS_D3D11_ALLOCATOR (allocator), FALSE);

  klass = GST_D3D11_ALLOCATOR_GET_CLASS (allocator);
  if (klass->set_actvie)
    return klass->set_actvie (allocator, active);

  return TRUE;
}

/* GstD3D11PoolAllocator */
#define GST_D3D11_POOL_ALLOCATOR_LOCK(alloc)   (g_rec_mutex_lock(&alloc->priv->lock))
#define GST_D3D11_POOL_ALLOCATOR_UNLOCK(alloc) (g_rec_mutex_unlock(&alloc->priv->lock))
#define GST_D3D11_POOL_ALLOCATOR_IS_FLUSHING(alloc)  (g_atomic_int_get (&alloc->priv->flushing))

struct _GstD3D11PoolAllocatorPrivate
{
  /* parent texture when array typed memory is used */
  ID3D11Texture2D *texture;
  D3D11_TEXTURE2D_DESC desc;

  /* All below member variables are analogous to that of GstBufferPool */
  GstAtomicQueue *queue;
  GstPoll *poll;

  /* This lock will protect all below variables apart from atomic ones
   * (identical to GstBufferPool::priv::rec_lock) */
  GRecMutex lock;
  gboolean started;
  gboolean active;

  /* atomic */
  gint outstanding;
  guint max_mems;
  guint cur_mems;
  gboolean flushing;

  /* Calculated memory size, based on Direct3D11 staging texture map.
   * Note that, we cannot know the actually staging texture memory size prior
   * to map the staging texture because driver will likely require padding */
  gsize mem_size;
};

static void gst_d3d11_pool_allocator_dispose (GObject * object);
static void gst_d3d11_pool_allocator_finalize (GObject * object);

static gboolean
gst_d3d11_pool_allocator_set_active (GstD3D11Allocator * allocator,
    gboolean active);

static gboolean gst_d3d11_pool_allocator_start (GstD3D11PoolAllocator * self);
static gboolean gst_d3d11_pool_allocator_stop (GstD3D11PoolAllocator * self);
static gboolean gst_d3d11_memory_release (GstMiniObject * mini_object);

#define gst_d3d11_pool_allocator_parent_class pool_alloc_parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D11PoolAllocator,
    gst_d3d11_pool_allocator, GST_TYPE_D3D11_ALLOCATOR);

static void
gst_d3d11_pool_allocator_class_init (GstD3D11PoolAllocatorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstD3D11AllocatorClass *d3d11alloc_class = GST_D3D11_ALLOCATOR_CLASS (klass);

  gobject_class->dispose = gst_d3d11_pool_allocator_dispose;
  gobject_class->finalize = gst_d3d11_pool_allocator_finalize;

  d3d11alloc_class->set_actvie = gst_d3d11_pool_allocator_set_active;
}

static void
gst_d3d11_pool_allocator_init (GstD3D11PoolAllocator * allocator)
{
  GstD3D11PoolAllocatorPrivate *priv;

  priv = allocator->priv = (GstD3D11PoolAllocatorPrivate *)
      gst_d3d11_pool_allocator_get_instance_private (allocator);
  g_rec_mutex_init (&priv->lock);

  priv->poll = gst_poll_new_timer ();
  priv->queue = gst_atomic_queue_new (16);
  priv->flushing = 1;
  priv->active = FALSE;
  priv->started = FALSE;

  /* 1 control write for flushing - the flush token */
  gst_poll_write_control (priv->poll);
  /* 1 control write for marking that we are not waiting for poll - the wait token */
  gst_poll_write_control (priv->poll);
}

static void
gst_d3d11_pool_allocator_dispose (GObject * object)
{
  GstD3D11PoolAllocator *self = GST_D3D11_POOL_ALLOCATOR (object);

  gst_clear_object (&self->device);

  G_OBJECT_CLASS (pool_alloc_parent_class)->dispose (object);
}

static void
gst_d3d11_pool_allocator_finalize (GObject * object)
{
  GstD3D11PoolAllocator *self = GST_D3D11_POOL_ALLOCATOR (object);
  GstD3D11PoolAllocatorPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "Finalize");

  gst_d3d11_pool_allocator_stop (self);
  gst_atomic_queue_unref (priv->queue);
  gst_poll_free (priv->poll);
  g_rec_mutex_clear (&priv->lock);

  GST_D3D11_CLEAR_COM (priv->texture);

  G_OBJECT_CLASS (pool_alloc_parent_class)->finalize (object);
}

static gboolean
gst_d3d11_pool_allocator_start (GstD3D11PoolAllocator * self)
{
  GstD3D11PoolAllocatorPrivate *priv = self->priv;
  ID3D11Device *device_handle;
  HRESULT hr;
  guint i;

  if (priv->started)
    return TRUE;

  /* Nothing to do */
  if (priv->desc.ArraySize == 1) {
    priv->started = TRUE;
    return TRUE;
  }

  device_handle = gst_d3d11_device_get_device_handle (self->device);

  if (!priv->texture) {
    hr = device_handle->CreateTexture2D (&priv->desc, NULL, &priv->texture);
    if (!gst_d3d11_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Failed to allocate texture");
      return FALSE;
    }
  }

  /* Pre-allocate memory objects */
  for (i = 0; i < priv->desc.ArraySize; i++) {
    GstMemory *mem;

    priv->texture->AddRef ();
    mem =
        gst_d3d11_allocator_alloc_wrapped (GST_D3D11_ALLOCATOR_CAST
        (_d3d11_memory_allocator), self->device, &priv->desc, priv->texture);

    if (i == 0) {
      if (!gst_d3d11_memory_update_size (mem)) {
        GST_ERROR_OBJECT (self, "Failed to calculate memory size");
        gst_memory_unref (mem);
        return FALSE;
      }

      priv->mem_size = mem->size;
    } else {
      mem->size = mem->maxsize = priv->mem_size;
    }

    GST_D3D11_MEMORY_CAST (mem)->priv->subresource_index = i;

    g_atomic_int_add (&priv->cur_mems, 1);
    gst_atomic_queue_push (priv->queue, mem);
    gst_poll_write_control (priv->poll);
  }

  priv->started = TRUE;

  return TRUE;
}

static void
gst_d3d11_pool_allocator_do_set_flushing (GstD3D11PoolAllocator * self,
    gboolean flushing)
{
  GstD3D11PoolAllocatorPrivate *priv = self->priv;

  if (GST_D3D11_POOL_ALLOCATOR_IS_FLUSHING (self) == flushing)
    return;

  if (flushing) {
    g_atomic_int_set (&priv->flushing, 1);
    /* Write the flush token to wake up any waiters */
    gst_poll_write_control (priv->poll);
  } else {
    while (!gst_poll_read_control (priv->poll)) {
      if (errno == EWOULDBLOCK) {
        /* This should not really happen unless flushing and unflushing
         * happens on different threads. Let's wait a bit to get back flush
         * token from the thread that was setting it to flushing */
        g_thread_yield ();
        continue;
      } else {
        /* Critical error but GstPoll already complained */
        break;
      }
    }

    g_atomic_int_set (&priv->flushing, 0);
  }
}

static gboolean
gst_d3d11_pool_allocator_set_active (GstD3D11Allocator * allocator,
    gboolean active)
{
  GstD3D11PoolAllocator *self = GST_D3D11_POOL_ALLOCATOR (allocator);
  GstD3D11PoolAllocatorPrivate *priv = self->priv;

  GST_LOG_OBJECT (self, "active %d", active);

  GST_D3D11_POOL_ALLOCATOR_LOCK (self);
  /* just return if we are already in the right state */
  if (priv->active == active)
    goto was_ok;

  if (active) {
    if (!gst_d3d11_pool_allocator_start (self))
      goto start_failed;

    /* flush_stop may release memory objects, setting to active to avoid running
     * do_stop while activating the pool */
    priv->active = TRUE;

    gst_d3d11_pool_allocator_do_set_flushing (self, FALSE);
  } else {
    gint outstanding;

    /* set to flushing first */
    gst_d3d11_pool_allocator_do_set_flushing (self, TRUE);

    /* when all memory objects are in the pool, free them. Else they will be
     * freed when they are released */
    outstanding = g_atomic_int_get (&priv->outstanding);
    GST_LOG_OBJECT (self, "outstanding memories %d, (in queue %d)",
        outstanding, gst_atomic_queue_length (priv->queue));
    if (outstanding == 0) {
      if (!gst_d3d11_pool_allocator_stop (self))
        goto stop_failed;
    }

    priv->active = FALSE;
  }

  GST_D3D11_POOL_ALLOCATOR_UNLOCK (self);

  return TRUE;

was_ok:
  {
    GST_DEBUG_OBJECT (self, "allocator was in the right state");
    GST_D3D11_POOL_ALLOCATOR_UNLOCK (self);
    return TRUE;
  }
start_failed:
  {
    GST_ERROR_OBJECT (self, "start failed");
    GST_D3D11_POOL_ALLOCATOR_UNLOCK (self);
    return FALSE;
  }
stop_failed:
  {
    GST_ERROR_OBJECT (self, "stop failed");
    GST_D3D11_POOL_ALLOCATOR_UNLOCK (self);
    return FALSE;
  }
}

static void
gst_d3d11_pool_allocator_free_memory (GstD3D11PoolAllocator * self,
    GstMemory * mem)
{
  GstD3D11PoolAllocatorPrivate *priv = self->priv;

  g_atomic_int_add (&priv->cur_mems, -1);
  GST_LOG_OBJECT (self, "freeing memory %p (%u left)", mem, priv->cur_mems);

  GST_MINI_OBJECT_CAST (mem)->dispose = NULL;
  gst_memory_unref (mem);
}

/* must be called with the lock */
static gboolean
gst_d3d11_pool_allocator_clear_queue (GstD3D11PoolAllocator * self)
{
  GstD3D11PoolAllocatorPrivate *priv = self->priv;
  GstMemory *memory;

  GST_LOG_OBJECT (self, "Clearing queue");

  /* clear the pool */
  while ((memory = (GstMemory *) gst_atomic_queue_pop (priv->queue))) {
    while (!gst_poll_read_control (priv->poll)) {
      if (errno == EWOULDBLOCK) {
        /* We put the memory into the queue but did not finish writing control
         * yet, let's wait a bit and retry */
        g_thread_yield ();
        continue;
      } else {
        /* Critical error but GstPoll already complained */
        break;
      }
    }
    gst_d3d11_pool_allocator_free_memory (self, memory);
  }

  GST_LOG_OBJECT (self, "Clear done");

  return priv->cur_mems == 0;
}

/* must be called with the lock */
static gboolean
gst_d3d11_pool_allocator_stop (GstD3D11PoolAllocator * self)
{
  GstD3D11PoolAllocatorPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  if (priv->started) {
    if (!gst_d3d11_pool_allocator_clear_queue (self))
      return FALSE;

    priv->started = FALSE;
  } else {
    GST_DEBUG_OBJECT (self, "Wasn't started");
  }

  return TRUE;
}

static inline void
dec_outstanding (GstD3D11PoolAllocator * self)
{
  if (g_atomic_int_dec_and_test (&self->priv->outstanding)) {
    /* all memory objects are returned to the pool, see if we need to free them */
    if (GST_D3D11_POOL_ALLOCATOR_IS_FLUSHING (self)) {
      /* take the lock so that set_active is not run concurrently */
      GST_D3D11_POOL_ALLOCATOR_LOCK (self);
      /* now that we have the lock, check if we have been de-activated with
       * outstanding buffers */
      if (!self->priv->active)
        gst_d3d11_pool_allocator_stop (self);

      GST_D3D11_POOL_ALLOCATOR_UNLOCK (self);
    }
  }
}

static void
gst_d3d11_pool_allocator_release_memory (GstD3D11PoolAllocator * self,
    GstMemory * mem)
{
  GST_LOG_OBJECT (self, "Released memory %p", mem);

  GST_MINI_OBJECT_CAST (mem)->dispose = NULL;
  mem->allocator = (GstAllocator *) gst_object_ref (_d3d11_memory_allocator);
  gst_object_unref (self);

  /* keep it around in our queue */
  gst_atomic_queue_push (self->priv->queue, mem);
  gst_poll_write_control (self->priv->poll);
  dec_outstanding (self);
}

static gboolean
gst_d3d11_memory_release (GstMiniObject * mini_object)
{
  GstMemory *mem = GST_MEMORY_CAST (mini_object);
  GstD3D11PoolAllocator *alloc;

  g_assert (mem->allocator != NULL);

  if (!GST_IS_D3D11_POOL_ALLOCATOR (mem->allocator)) {
    GST_LOG_OBJECT (mem->allocator, "Not our memory, free");
    return TRUE;
  }

  alloc = GST_D3D11_POOL_ALLOCATOR (mem->allocator);
  /* if flushing, free this memory */
  if (GST_D3D11_POOL_ALLOCATOR_IS_FLUSHING (alloc)) {
    GST_LOG_OBJECT (alloc, "allocator is flushing, free %p", mem);
    return TRUE;
  }

  /* return the memory to the allocator */
  gst_memory_ref (mem);
  gst_d3d11_pool_allocator_release_memory (alloc, mem);

  return FALSE;
}

static GstFlowReturn
gst_d3d11_pool_allocator_alloc (GstD3D11PoolAllocator * self, GstMemory ** mem)
{
  GstD3D11PoolAllocatorPrivate *priv = self->priv;
  GstMemory *new_mem;

  /* we allcates texture array during start */
  if (priv->desc.ArraySize > 1)
    return GST_FLOW_EOS;

  /* increment the allocation counter */
  g_atomic_int_add (&priv->cur_mems, 1);
  new_mem =
      gst_d3d11_allocator_alloc_internal (GST_D3D11_ALLOCATOR_CAST
      (_d3d11_memory_allocator), self->device, &priv->desc);
  if (!new_mem) {
    GST_ERROR_OBJECT (self, "Failed to allocate new memory");
    g_atomic_int_add (&priv->cur_mems, -1);
    return GST_FLOW_ERROR;
  }

  if (!priv->mem_size) {
    if (!gst_d3d11_memory_update_size (new_mem)) {
      GST_ERROR_OBJECT (self, "Failed to calculate size");
      gst_memory_unref (new_mem);
      g_atomic_int_add (&priv->cur_mems, -1);

      return GST_FLOW_ERROR;
    }

    priv->mem_size = new_mem->size;
  }

  new_mem->size = new_mem->maxsize = priv->mem_size;

  *mem = new_mem;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_pool_allocator_acquire_memory_internal (GstD3D11PoolAllocator * self,
    GstMemory ** memory)
{
  GstFlowReturn result;
  GstD3D11PoolAllocatorPrivate *priv = self->priv;

  while (TRUE) {
    if (G_UNLIKELY (GST_D3D11_POOL_ALLOCATOR_IS_FLUSHING (self)))
      goto flushing;

    /* try to get a memory from the queue */
    *memory = (GstMemory *) gst_atomic_queue_pop (priv->queue);
    if (G_LIKELY (*memory)) {
      while (!gst_poll_read_control (priv->poll)) {
        if (errno == EWOULDBLOCK) {
          /* We put the memory into the queue but did not finish writing control
           * yet, let's wait a bit and retry */
          g_thread_yield ();
          continue;
        } else {
          /* Critical error but GstPoll already complained */
          break;
        }
      }
      result = GST_FLOW_OK;
      GST_LOG_OBJECT (self, "acquired memory %p", *memory);
      break;
    }

    /* no memory, try to allocate some more */
    GST_LOG_OBJECT (self, "no memory, trying to allocate");
    result = gst_d3d11_pool_allocator_alloc (self, memory);
    if (G_LIKELY (result == GST_FLOW_OK))
      /* we have a memory, return it */
      break;

    if (G_UNLIKELY (result != GST_FLOW_EOS))
      /* something went wrong, return error */
      break;

    /* now we release the control socket, we wait for a memory release or
     * flushing */
    if (!gst_poll_read_control (priv->poll)) {
      if (errno == EWOULDBLOCK) {
        /* This means that we have two threads trying to allocate memory
         * already, and the other one already got the wait token. This
         * means that we only have to wait for the poll now and not write the
         * token afterwards: we will be woken up once the other thread is
         * woken up and that one will write the wait token it removed */
        GST_LOG_OBJECT (self, "waiting for free memory or flushing");
        gst_poll_wait (priv->poll, GST_CLOCK_TIME_NONE);
      } else {
        /* This is a critical error, GstPoll already gave a warning */
        result = GST_FLOW_ERROR;
        break;
      }
    } else {
      /* We're the first thread waiting, we got the wait token and have to
       * write it again later
       * OR
       * We're a second thread and just consumed the flush token and block all
       * other threads, in which case we must not wait and give it back
       * immediately */
      if (!GST_D3D11_POOL_ALLOCATOR_IS_FLUSHING (self)) {
        GST_LOG_OBJECT (self, "waiting for free memory or flushing");
        gst_poll_wait (priv->poll, GST_CLOCK_TIME_NONE);
      }
      gst_poll_write_control (priv->poll);
    }
  }

  return result;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (self, "we are flushing");
    return GST_FLOW_FLUSHING;
  }
}

/**
 * gst_d3d11_pool_allocator_new:
 * @device: a #GstD3D11Device
 * @desc: a D3D11_TEXTURE2D_DESC for texture allocation
 *
 * Creates a new #GstD3D11PoolAllocator instance.
 *
 * Returns: (transfer full): a new #GstD3D11PoolAllocator instance
 */
GstD3D11PoolAllocator *
gst_d3d11_pool_allocator_new (GstD3D11Device * device,
    const D3D11_TEXTURE2D_DESC * desc)
{
  GstD3D11PoolAllocator *self;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);
  g_return_val_if_fail (desc != NULL, NULL);

  gst_d3d11_memory_init_once ();

  self = (GstD3D11PoolAllocator *)
      g_object_new (GST_TYPE_D3D11_POOL_ALLOCATOR, NULL);
  gst_object_ref_sink (self);

  self->device = (GstD3D11Device *) gst_object_ref (device);
  self->priv->desc = *desc;

  return self;
}

/**
 * gst_d3d11_pool_allocator_acquire_memory:
 * @allocator: a #GstD3D11PoolAllocator
 * @memory: (transfer full): a #GstMemory
 *
 * Acquires a #GstMemory from @allocator. @memory should point to a memory
 * location that can hold a pointer to the new #GstMemory.
 *
 * Returns: a #GstFlowReturn such as %GST_FLOW_FLUSHING when the allocator is
 * inactive.
 */
GstFlowReturn
gst_d3d11_pool_allocator_acquire_memory (GstD3D11PoolAllocator * allocator,
    GstMemory ** memory)
{
  GstD3D11PoolAllocatorPrivate *priv;
  GstFlowReturn result;

  g_return_val_if_fail (GST_IS_D3D11_POOL_ALLOCATOR (allocator),
      GST_FLOW_ERROR);
  g_return_val_if_fail (memory != NULL, GST_FLOW_ERROR);

  priv = allocator->priv;

  /* assume we'll have one more outstanding buffer we need to do that so
   * that concurrent set_active doesn't clear the buffers */
  g_atomic_int_inc (&priv->outstanding);
  result = gst_d3d11_pool_allocator_acquire_memory_internal (allocator, memory);

  if (result == GST_FLOW_OK) {
    GstMemory *mem = *memory;
    /* Replace default allocator with ours */
    gst_object_unref (mem->allocator);
    mem->allocator = (GstAllocator *) gst_object_ref (allocator);
    GST_MINI_OBJECT_CAST (mem)->dispose = gst_d3d11_memory_release;
  } else {
    dec_outstanding (allocator);
  }

  return result;
}

/**
 * gst_d3d11_pool_allocator_get_pool_size:
 * @allocator: a #GstD3D11PoolAllocator
 * @max_size: (out) (optional): the max size of pool
 * @outstanding_size: (out) (optional): the number of outstanding memory
 *
 * Returns: %TRUE if the size of memory pool is known
 *
 * Since: 1.20
 */
gboolean
gst_d3d11_pool_allocator_get_pool_size (GstD3D11PoolAllocator * allocator,
    guint * max_size, guint * outstanding_size)
{
  GstD3D11PoolAllocatorPrivate *priv;

  g_return_val_if_fail (GST_IS_D3D11_POOL_ALLOCATOR (allocator), FALSE);

  priv = allocator->priv;

  if (max_size) {
    if (priv->desc.ArraySize > 1) {
      *max_size = priv->desc.ArraySize;
    } else {
      /* For non-texture-array memory, we don't have any limit yet */
      *max_size = 0;
    }
  }

  if (outstanding_size)
    *outstanding_size = g_atomic_int_get (&priv->outstanding);

  return TRUE;
}
