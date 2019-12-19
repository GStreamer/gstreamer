/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_allocator_debug);
#define GST_CAT_DEFAULT gst_d3d11_allocator_debug

GstD3D11AllocationParams *
gst_d3d11_allocation_params_new (GstVideoInfo * info,
    GstD3D11AllocationFlags flags, D3D11_USAGE usage, gint bind_flags)
{
  GstD3D11AllocationParams *ret;
  const GstD3D11Format *d3d11_format;
  gint i;
  gint cpu_access_flags;

  g_return_val_if_fail (info != NULL, NULL);

  d3d11_format = gst_d3d11_format_from_gst (GST_VIDEO_INFO_FORMAT (info));
  if (!d3d11_format) {
    GST_WARNING ("Couldn't get d3d11 format");
    return NULL;
  }

  if (usage == D3D11_USAGE_DEFAULT) {
    cpu_access_flags = 0;
  } else if (usage == D3D11_USAGE_DYNAMIC) {
    cpu_access_flags = D3D11_CPU_ACCESS_WRITE;
  } else {
    GST_FIXME ("Neither default nor dynamic usage");
    return NULL;
  }

  ret = g_new0 (GstD3D11AllocationParams, 1);

  ret->info = *info;
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
  if (d3d11_format->dxgi_format == DXGI_FORMAT_UNKNOWN ||
      (flags & GST_D3D11_ALLOCATION_FLAG_USE_RESOURCE_FORMAT) ==
      GST_D3D11_ALLOCATION_FLAG_USE_RESOURCE_FORMAT) {
    flags |= GST_D3D11_ALLOCATION_FLAG_USE_RESOURCE_FORMAT;

    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
      g_assert (d3d11_format->resource_format[i] != DXGI_FORMAT_UNKNOWN);

      ret->desc[i].Width = GST_VIDEO_INFO_COMP_WIDTH (info, i);
      ret->desc[i].Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);
      ret->desc[i].MipLevels = 1;
      ret->desc[i].ArraySize = 1;
      ret->desc[i].Format = d3d11_format->resource_format[i];
      ret->desc[i].SampleDesc.Count = 1;
      ret->desc[i].SampleDesc.Quality = 0;
      ret->desc[i].Usage = usage;
      ret->desc[i].BindFlags = bind_flags;
      ret->desc[i].CPUAccessFlags = cpu_access_flags;
    }
  } else {
    g_assert (d3d11_format->dxgi_format != DXGI_FORMAT_UNKNOWN);

    ret->desc[0].Width = GST_VIDEO_INFO_WIDTH (info);
    ret->desc[0].Height = GST_VIDEO_INFO_HEIGHT (info);
    ret->desc[0].MipLevels = 1;
    ret->desc[0].ArraySize = 1;
    ret->desc[0].Format = d3d11_format->dxgi_format;
    ret->desc[0].SampleDesc.Count = 1;
    ret->desc[0].SampleDesc.Quality = 0;
    ret->desc[0].Usage = usage;
    ret->desc[0].BindFlags = bind_flags;
    ret->desc[0].CPUAccessFlags = cpu_access_flags;
  }

  ret->flags = flags;

  return ret;
}

GstD3D11AllocationParams *
gst_d3d11_allocation_params_copy (GstD3D11AllocationParams * src)
{
  GstD3D11AllocationParams *dst;

  g_return_val_if_fail (src != NULL, NULL);

  dst = g_new0 (GstD3D11AllocationParams, 1);
  memcpy (dst, src, sizeof (GstD3D11AllocationParams));

  return dst;
}

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
_init_alloc_params (GType type)
{
  static GstValueTable table = {
    0, (GstValueCompareFunc) gst_d3d11_allocation_params_compare,
    NULL, NULL
  };

  table.type = type;
  gst_value_register (&table);
}

G_DEFINE_BOXED_TYPE_WITH_CODE (GstD3D11AllocationParams,
    gst_d3d11_allocation_params,
    (GBoxedCopyFunc) gst_d3d11_allocation_params_copy,
    (GBoxedFreeFunc) gst_d3d11_allocation_params_free,
    _init_alloc_params (g_define_type_id));

#define gst_d3d11_allocator_parent_class parent_class
G_DEFINE_TYPE (GstD3D11Allocator, gst_d3d11_allocator, GST_TYPE_ALLOCATOR);

static inline D3D11_MAP
gst_map_flags_to_d3d11 (GstMapFlags flags)
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
create_staging_texture (GstD3D11Device * device,
    const D3D11_TEXTURE2D_DESC * ref)
{
  D3D11_TEXTURE2D_DESC desc = { 0, };

  desc.Width = ref->Width;
  desc.Height = ref->Height;
  desc.MipLevels = 1;
  desc.Format = ref->Format;
  desc.SampleDesc.Count = 1;
  desc.ArraySize = 1;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = (D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE);

  return gst_d3d11_device_create_texture (device, &desc, NULL);
}

static gboolean
map_cpu_access_data (GstD3D11Memory * dmem, D3D11_MAP map_type)
{
  HRESULT hr;
  gboolean ret = TRUE;
  ID3D11Resource *texture = (ID3D11Resource *) dmem->texture;
  ID3D11Resource *staging = (ID3D11Resource *) dmem->staging;
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context_handle (dmem->device);

  gst_d3d11_device_lock (dmem->device);
  if (GST_MEMORY_FLAG_IS_SET (dmem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD)) {
    ID3D11DeviceContext_CopySubresourceRegion (device_context,
        staging, 0, 0, 0, 0, texture, 0, NULL);
  }

  hr = ID3D11DeviceContext_Map (device_context,
      staging, 0, map_type, 0, &dmem->map);

  if (!gst_d3d11_result (hr)) {
    GST_ERROR_OBJECT (GST_MEMORY_CAST (dmem)->allocator,
        "Failed to map staging texture (0x%x)", (guint) hr);
    ret = FALSE;
  }

  gst_d3d11_device_unlock (dmem->device);

  return ret;
}

static gpointer
gst_d3d11_memory_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstD3D11Memory *dmem = (GstD3D11Memory *) mem;

  if (dmem->desc.Usage == D3D11_USAGE_DYNAMIC) {
    GST_FIXME_OBJECT (mem->allocator,
        "D3D11_USAGE_DYNAMIC shouldn't be used with gst_memory_map");
    return NULL;
  }

  g_mutex_lock (&dmem->lock);
  if ((flags & GST_MAP_D3D11) == GST_MAP_D3D11) {
    if (dmem->staging &&
        GST_MEMORY_FLAG_IS_SET (dmem, GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD)) {
      ID3D11DeviceContext *device_context =
          gst_d3d11_device_get_device_context_handle (dmem->device);

      gst_d3d11_device_lock (dmem->device);
      ID3D11DeviceContext_CopySubresourceRegion (device_context,
          (ID3D11Resource *) dmem->texture, 0, 0, 0, 0,
          (ID3D11Resource *) dmem->staging, 0, NULL);
      gst_d3d11_device_unlock (dmem->device);
    }

    GST_MEMORY_FLAG_UNSET (dmem, GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD);

    if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (dmem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);

    g_assert (dmem->texture != NULL);
    g_mutex_unlock (&dmem->lock);

    return dmem->texture;
  }

  if (dmem->cpu_map_count == 0) {
    D3D11_MAP map_type;

    /* Allocate staging texture for CPU access */
    if (!dmem->staging) {
      dmem->staging = create_staging_texture (dmem->device, &dmem->desc);
      if (!dmem->staging) {
        GST_ERROR_OBJECT (mem->allocator, "Couldn't create staging texture");
        g_mutex_unlock (&dmem->lock);

        return NULL;
      }

      /* first memory, always need download to staging */
      GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
    }

    map_type = gst_map_flags_to_d3d11 (flags);

    if (!map_cpu_access_data (dmem, map_type)) {
      GST_ERROR_OBJECT (mem->allocator, "Couldn't map staging texture");
      g_mutex_unlock (&dmem->lock);

      return NULL;
    }
  }

  if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD);

  GST_MEMORY_FLAG_UNSET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);

  dmem->cpu_map_count++;
  g_mutex_unlock (&dmem->lock);

  return dmem->map.pData;
}

static void
unmap_cpu_access_data (GstD3D11Memory * dmem)
{
  ID3D11Resource *staging = (ID3D11Resource *) dmem->staging;
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context_handle (dmem->device);

  gst_d3d11_device_lock (dmem->device);
  ID3D11DeviceContext_Unmap (device_context, staging, 0);
  gst_d3d11_device_unlock (dmem->device);
}

static void
gst_d3d11_memory_unmap_full (GstMemory * mem, GstMapInfo * info)
{
  GstD3D11Memory *dmem = (GstD3D11Memory *) mem;

  if (dmem->desc.Usage == D3D11_USAGE_DYNAMIC) {
    GST_FIXME_OBJECT (mem->allocator,
        "D3D11_USAGE_DYNAMIC shouldn't be used with gst_memory_unmap");
    return;
  }

  g_mutex_lock (&dmem->lock);
  if ((info->flags & GST_MAP_D3D11) == GST_MAP_D3D11) {
    if ((info->flags & GST_MAP_WRITE) == GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);

    g_mutex_unlock (&dmem->lock);
    return;
  }

  if ((info->flags & GST_MAP_WRITE))
    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD);

  dmem->cpu_map_count--;
  if (dmem->cpu_map_count > 0) {
    g_mutex_unlock (&dmem->lock);
    return;
  }

  unmap_cpu_access_data (dmem);

  g_mutex_unlock (&dmem->lock);
}

static GstMemory *
gst_d3d11_memory_share (GstMemory * mem, gssize offset, gssize size)
{
  /* TODO: impl. */
  return NULL;
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
  GstD3D11Memory *dmem = (GstD3D11Memory *) mem;
  gint i;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (dmem->render_target_view[i])
      ID3D11RenderTargetView_Release (dmem->render_target_view[i]);
    dmem->render_target_view[i] = NULL;

    if (dmem->shader_resource_view[i])
      ID3D11ShaderResourceView_Release (dmem->shader_resource_view[i]);
    dmem->shader_resource_view[i] = NULL;
  }

  if (dmem->texture)
    ID3D11Texture2D_Release (dmem->texture);

  if (dmem->staging)
    ID3D11Texture2D_Release (dmem->staging);

  gst_clear_object (&dmem->device);
  g_mutex_clear (&dmem->lock);

  g_free (dmem);
}

static void
gst_d3d11_allocator_dispose (GObject * object)
{
  GstD3D11Allocator *alloc = GST_D3D11_ALLOCATOR (object);

  gst_clear_object (&alloc->device);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_allocator_class_init (GstD3D11AllocatorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  gobject_class->dispose = gst_d3d11_allocator_dispose;

  allocator_class->alloc = gst_d3d11_allocator_dummy_alloc;
  allocator_class->free = gst_d3d11_allocator_free;

  GST_DEBUG_CATEGORY_INIT (gst_d3d11_allocator_debug, "d3d11allocator", 0,
      "d3d11allocator object");
}

static void
gst_d3d11_allocator_init (GstD3D11Allocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_D3D11_MEMORY_NAME;
  alloc->mem_map = gst_d3d11_memory_map;
  alloc->mem_unmap_full = gst_d3d11_memory_unmap_full;
  alloc->mem_share = gst_d3d11_memory_share;
  /* fallback copy */

  GST_OBJECT_FLAG_SET (alloc, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

GstD3D11Allocator *
gst_d3d11_allocator_new (GstD3D11Device * device)
{
  GstD3D11Allocator *allocator;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  allocator = g_object_new (GST_TYPE_D3D11_ALLOCATOR, NULL);
  allocator->device = gst_object_ref (device);

  return allocator;
}

static gboolean
calculate_mem_size (GstD3D11Device * device, ID3D11Texture2D * texture,
    D3D11_TEXTURE2D_DESC * desc, D3D11_MAP map_type,
    gint stride[GST_VIDEO_MAX_PLANES], gsize * size)
{
  HRESULT hr;
  gboolean ret = TRUE;
  D3D11_MAPPED_SUBRESOURCE map;
  gsize offset[GST_VIDEO_MAX_PLANES];
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context_handle (device);

  gst_d3d11_device_lock (device);
  hr = ID3D11DeviceContext_Map (device_context,
      (ID3D11Resource *) texture, 0, map_type, 0, &map);

  if (!gst_d3d11_result (hr)) {
    GST_ERROR_OBJECT (device, "Failed to map texture (0x%x)", (guint) hr);
    gst_d3d11_device_unlock (device);
    return FALSE;
  }

  ret = gst_d3d11_dxgi_format_get_size (desc->Format,
      desc->Width, desc->Height, map.RowPitch, offset, stride, size);

  ID3D11DeviceContext_Unmap (device_context, (ID3D11Resource *) texture, 0);
  gst_d3d11_device_unlock (device);

  return ret;
}

static void
create_shader_resource_views (GstD3D11Memory * mem)
{
  gint i;
  HRESULT hr;
  guint num_views = 0;
  ID3D11Device *device_handle;
  D3D11_SHADER_RESOURCE_VIEW_DESC resource_desc = { 0, };
  DXGI_FORMAT formats[GST_VIDEO_MAX_PLANES] = { DXGI_FORMAT_UNKNOWN, };

  device_handle = gst_d3d11_device_get_device_handle (mem->device);

  switch (mem->desc.Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
      num_views = 1;
      formats[0] = mem->desc.Format;
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
      num_views = 2;
      formats[0] = DXGI_FORMAT_R16_UNORM;
      formats[1] = DXGI_FORMAT_R16G16_UNORM;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if ((mem->desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) ==
      D3D11_BIND_SHADER_RESOURCE) {
    resource_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    resource_desc.Texture2D.MipLevels = 1;

    for (i = 0; i < num_views; i++) {
      resource_desc.Format = formats[i];
      hr = ID3D11Device_CreateShaderResourceView (device_handle,
          (ID3D11Resource *) mem->texture, &resource_desc,
          &mem->shader_resource_view[i]);

      if (!gst_d3d11_result (hr)) {
        GST_ERROR_OBJECT (GST_MEMORY_CAST (mem)->allocator,
            "Failed to create %dth resource view (0x%x)", i, (guint) hr);
        goto error;
      }
    }

    mem->num_shader_resource_views = num_views;
  }

  return;

error:
  for (i = 0; i < num_views; i++) {
    if (mem->shader_resource_view[i])
      ID3D11ShaderResourceView_Release (mem->shader_resource_view[i]);
    mem->shader_resource_view[i] = NULL;
  }

  mem->num_shader_resource_views = 0;
}

static void
create_render_target_views (GstD3D11Memory * mem)
{
  gint i;
  HRESULT hr;
  guint num_views = 0;
  ID3D11Device *device_handle;
  D3D11_RENDER_TARGET_VIEW_DESC render_desc = { 0, };
  DXGI_FORMAT formats[GST_VIDEO_MAX_PLANES] = { DXGI_FORMAT_UNKNOWN, };

  device_handle = gst_d3d11_device_get_device_handle (mem->device);

  switch (mem->desc.Format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
      num_views = 1;
      formats[0] = mem->desc.Format;
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
      num_views = 2;
      formats[0] = DXGI_FORMAT_R16_UNORM;
      formats[1] = DXGI_FORMAT_R16G16_UNORM;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if ((mem->desc.BindFlags & D3D11_BIND_RENDER_TARGET) ==
      D3D11_BIND_RENDER_TARGET) {
    render_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    render_desc.Texture2D.MipSlice = 0;

    for (i = 0; i < num_views; i++) {
      render_desc.Format = formats[i];

      hr = ID3D11Device_CreateRenderTargetView (device_handle,
          (ID3D11Resource *) mem->texture, &render_desc,
          &mem->render_target_view[i]);
      if (!gst_d3d11_result (hr)) {
        GST_ERROR_OBJECT (GST_MEMORY_CAST (mem)->allocator,
            "Failed to create %dth render target view (0x%x)", i, (guint) hr);
        goto error;
      }
    }

    mem->num_render_target_views = num_views;
  }

  return;

error:
  for (i = 0; i < num_views; i++) {
    if (mem->render_target_view[i])
      ID3D11RenderTargetView_Release (mem->render_target_view[i]);
    mem->render_target_view[i] = NULL;
  }

  mem->num_render_target_views = 0;
}

GstMemory *
gst_d3d11_allocator_alloc (GstD3D11Allocator * allocator,
    GstD3D11AllocationParams * params)
{
  GstD3D11Memory *mem;
  GstD3D11Device *device;
  ID3D11Texture2D *texture = NULL;
  ID3D11Texture2D *staging = NULL;
  D3D11_TEXTURE2D_DESC *desc;
  gsize *size;
  gboolean is_first = FALSE;
  GstMemoryFlags memory_flags;

  g_return_val_if_fail (GST_IS_D3D11_ALLOCATOR (allocator), NULL);
  g_return_val_if_fail (params != NULL, NULL);

  device = allocator->device;
  desc = &params->desc[params->plane];
  size = &params->size[params->plane];

  if (desc->Usage == D3D11_USAGE_DEFAULT) {
    memory_flags = 0;
  } else if (desc->Usage == D3D11_USAGE_DYNAMIC) {
    /* FIXME: how we can make D3D11_USAGE_DYNAMIC work with GST_MAP_READWRITE ? */
    memory_flags = GST_MEMORY_FLAG_NOT_MAPPABLE;
  } else {
    GST_FIXME_OBJECT (allocator, "Cannot support usage %d", desc->Usage);
    return NULL;
  }

  if (*size == 0)
    is_first = TRUE;

  texture = gst_d3d11_device_create_texture (device, desc, NULL);
  if (!texture) {
    GST_ERROR_OBJECT (allocator, "Couldn't create texture");
    goto error;
  }

  /* per plane, allocated staging texture to calculate actual size,
   * stride, and offset */
  if (is_first) {
    ID3D11Texture2D *target;
    D3D11_MAP map_type;
    gint num_plane;
    gint stride[GST_VIDEO_MAX_PLANES];
    gsize mem_size;
    gint i;

    if (desc->Usage == D3D11_USAGE_DEFAULT) {
      staging = create_staging_texture (device, desc);
      if (!staging) {
        GST_ERROR_OBJECT (allocator, "Couldn't create staging texture");
        goto error;
      }

      target = staging;
      map_type = D3D11_MAP_READ;
    } else if (desc->Usage == D3D11_USAGE_DYNAMIC) {
      target = texture;
      map_type = D3D11_MAP_WRITE_DISCARD;
    } else {
      g_assert_not_reached ();
    }

    if (!calculate_mem_size (device, target, desc, map_type, stride, &mem_size))
      goto error;

    num_plane = gst_d3d11_dxgi_format_n_planes (desc->Format);

    for (i = 0; i < num_plane; i++) {
      params->stride[params->plane + i] = stride[i];
    }

    *size = mem_size;
  }

  mem = g_new0 (GstD3D11Memory, 1);

  gst_memory_init (GST_MEMORY_CAST (mem),
      memory_flags, GST_ALLOCATOR_CAST (allocator), NULL, *size, 0, 0, *size);

  g_mutex_init (&mem->lock);
  mem->info = params->info;
  mem->plane = params->plane;
  mem->desc = *desc;
  mem->texture = texture;
  mem->staging = staging;
  mem->device = gst_object_ref (device);

  if (staging)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);

  return GST_MEMORY_CAST (mem);

error:
  if (texture)
    gst_d3d11_device_release_texture (device, texture);

  if (staging)
    gst_d3d11_device_release_texture (device, texture);

  return NULL;
}

gboolean
gst_is_d3d11_memory (GstMemory * mem)
{
  return mem != NULL && mem->allocator != NULL &&
      GST_IS_D3D11_ALLOCATOR (mem->allocator);
}

gboolean
gst_d3d11_memory_ensure_shader_resource_view (GstD3D11Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), FALSE);

  if (mem->num_shader_resource_views)
    return TRUE;

  if (!(mem->desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
    GST_WARNING_OBJECT (GST_MEMORY_CAST (mem)->allocator,
        "Need BindFlags, current flag 0x%x", mem->desc.BindFlags);

    return FALSE;
  }

  create_shader_resource_views (mem);

  return ! !mem->num_shader_resource_views;
}

gboolean
gst_d3d11_memory_ensure_render_target_view (GstD3D11Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d11_memory (GST_MEMORY_CAST (mem)), FALSE);

  if (mem->num_render_target_views)
    return TRUE;

  if (!(mem->desc.BindFlags & D3D11_BIND_RENDER_TARGET)) {
    GST_WARNING_OBJECT (GST_MEMORY_CAST (mem)->allocator,
        "Need BindFlags, current flag 0x%x", mem->desc.BindFlags);

    return FALSE;
  }

  create_render_target_views (mem);

  return ! !mem->num_render_target_views;
}
