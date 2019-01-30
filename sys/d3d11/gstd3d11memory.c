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
gst_d3d11_allocation_params_new (GstAllocationParams * alloc_params,
    GstVideoInfo * info, GstVideoAlignment * align)
{
  GstD3D11AllocationParams *ret;

  g_return_val_if_fail (alloc_params != NULL, NULL);
  g_return_val_if_fail (info != NULL, NULL);

  ret = g_new0 (GstD3D11AllocationParams, 1);

  memcpy (&ret->info, info, sizeof (GstVideoInfo));
  if (align) {
    ret->align = *align;
  } else {
    gst_video_alignment_reset (&ret->align);
  }

  ret->desc.Width = GST_VIDEO_INFO_WIDTH (info);
  ret->desc.Height = GST_VIDEO_INFO_HEIGHT (info);
  ret->desc.MipLevels = 1;
  ret->desc.ArraySize = 1;
  ret->desc.Format =
      gst_d3d11_dxgi_format_from_gst (GST_VIDEO_INFO_FORMAT (info));
  ret->desc.SampleDesc.Count = 1;
  ret->desc.SampleDesc.Quality = 0;
  ret->desc.Usage = D3D11_USAGE_DEFAULT;
  /* User must set proper BindFlags and MiscFlags manually */

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

G_DEFINE_BOXED_TYPE (GstD3D11AllocationParams, gst_d3d11_allocation_params,
    (GBoxedCopyFunc) gst_d3d11_allocation_params_copy,
    (GBoxedFreeFunc) gst_d3d11_allocation_params_free);

#define gst_d3d11_allocator_parent_class parent_class
G_DEFINE_TYPE (GstD3D11Allocator, gst_d3d11_allocator, GST_TYPE_ALLOCATOR);

static inline D3D11_MAP
_gst_map_flags_to_d3d11 (GstMapFlags flags)
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
_create_staging_texture (GstD3D11Device * device,
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

typedef struct
{
  GstD3D11Memory *mem;
  D3D11_MAP map_flag;

  gboolean ret;
} D3D11MapData;

static void
_map_cpu_access_data (GstD3D11Device * device, gpointer data)
{
  D3D11MapData *map_data = (D3D11MapData *) data;
  GstD3D11Memory *dmem = map_data->mem;
  HRESULT hr;
  ID3D11Resource *texture = (ID3D11Resource *) dmem->texture;
  ID3D11Resource *staging = (ID3D11Resource *) dmem->staging;
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context (device);

  ID3D11DeviceContext_CopySubresourceRegion (device_context,
      staging, 0, 0, 0, 0, texture, 0, NULL);

  hr = ID3D11DeviceContext_Map (device_context,
      staging, 0, map_data->map_flag, 0, &dmem->map);

  if (FAILED (hr)) {
    GST_ERROR ("Failed to map staging texture (0x%x)", (guint) hr);
    map_data->ret = FALSE;
  }

  map_data->ret = TRUE;
}

static gpointer
gst_d3d11_memory_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstD3D11Memory *dmem = (GstD3D11Memory *) mem;

  if ((flags & GST_MAP_D3D11) == GST_MAP_D3D11)
    return dmem->texture;

  if (dmem->cpu_map_count == 0) {
    D3D11MapData map_data;
    GstD3D11Device *device = GST_D3D11_ALLOCATOR (mem->allocator)->device;

    map_data.mem = dmem;
    map_data.map_flag = _gst_map_flags_to_d3d11 (flags);

    gst_d3d11_device_thread_add (device, (GstD3D11DeviceThreadFunc)
        _map_cpu_access_data, &map_data);

    if (!map_data.ret)
      return NULL;
  }

  if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE)
    dmem->need_upload = TRUE;

  dmem->cpu_map_count++;

  return dmem->map.pData;
}

static void
_unmap_cpu_access_data (GstD3D11Device * device, gpointer data)
{
  GstD3D11Memory *dmem = (GstD3D11Memory *) data;
  ID3D11Resource *texture = (ID3D11Resource *) dmem->texture;
  ID3D11Resource *staging = (ID3D11Resource *) dmem->staging;
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context (device);

  ID3D11DeviceContext_Unmap (device_context, staging, 0);

  if (dmem->need_upload) {
    ID3D11DeviceContext_CopySubresourceRegion (device_context, texture,
        0, 0, 0, 0, staging, 0, NULL);
  }
  dmem->need_upload = FALSE;
}

static void
gst_d3d11_memory_unmap_full (GstMemory * mem, GstMapInfo * info)
{
  GstD3D11Memory *dmem = (GstD3D11Memory *) mem;
  GstD3D11Device *device = GST_D3D11_ALLOCATOR (mem->allocator)->device;

  if ((info->flags & GST_MAP_D3D11) == GST_MAP_D3D11)
    return;

  dmem->cpu_map_count--;
  if (dmem->cpu_map_count > 0)
    return;

  gst_d3d11_device_thread_add (device, (GstD3D11DeviceThreadFunc)
      _unmap_cpu_access_data, dmem);
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
  GstD3D11Device *device = GST_D3D11_ALLOCATOR (allocator)->device;

  if (dmem->texture)
    gst_d3d11_device_release_texture (device, dmem->texture);

  if (dmem->staging)
    gst_d3d11_device_release_texture (device, dmem->staging);

  g_slice_free (GstD3D11Memory, dmem);
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

typedef struct _CalculateSizeData
{
  ID3D11Texture2D *staging;
  GstVideoInfo *info;
  gsize offset[GST_VIDEO_MAX_PLANES];
  gint stride[GST_VIDEO_MAX_PLANES];
  gsize size;
  gboolean ret;
} CalculateSizeData;

static void
_calculate_buffer_size (GstD3D11Device * device, CalculateSizeData * data)
{
  HRESULT hr;
  D3D11_MAPPED_SUBRESOURCE map;
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context (device);

  hr = ID3D11DeviceContext_Map (device_context,
      (ID3D11Resource *) data->staging, 0, GST_MAP_READWRITE, 0, &map);

  if (FAILED (hr)) {
    GST_ERROR ("Failed to map staging texture (0x%x)", (guint) hr);
    data->ret = FALSE;
    return;
  }

  ID3D11DeviceContext_Unmap (device_context, (ID3D11Resource *) data->staging,
      0);

  data->ret = gst_d3d11_calculate_buffer_size (data->info,
      map.RowPitch, data->offset, data->stride, &data->size);
}

GstMemory *
gst_d3d11_allocator_alloc (GstD3D11Allocator * allocator,
    GstD3D11AllocationParams * params)
{
  GstD3D11Memory *mem;
  GstD3D11Device *device;
  GstAllocationParams *alloc_params;
  gsize size, maxsize;
  ID3D11Texture2D *texture = NULL;
  ID3D11Texture2D *staging = NULL;
  CalculateSizeData data;
  gint i;

  g_return_val_if_fail (GST_IS_D3D11_ALLOCATOR (allocator), NULL);
  g_return_val_if_fail (params != NULL, NULL);

  device = allocator->device;

  texture = gst_d3d11_device_create_texture (device, &params->desc, NULL);
  if (!texture) {
    GST_ERROR_OBJECT (allocator, "Couldn't create texture");
    goto error;
  }

  staging = _create_staging_texture (device, &params->desc);
  if (!staging) {
    GST_ERROR_OBJECT (allocator, "Couldn't create staging texture");
    goto error;
  }

  /* try map staging texture to get actual stride and size */
  memset (&data, 0, sizeof (CalculateSizeData));
  data.staging = staging;
  data.info = &params->info;
  gst_d3d11_device_thread_add (device, (GstD3D11DeviceThreadFunc)
      _calculate_buffer_size, &data);

  if (!data.ret) {
    GST_ERROR_OBJECT (allocator, "Couldn't calculate stride");
    goto error;
  }

  alloc_params = (GstAllocationParams *) params;
  maxsize = size = data.size;
  maxsize += alloc_params->prefix + alloc_params->padding;

  mem = g_slice_new0 (GstD3D11Memory);

  gst_memory_init (GST_MEMORY_CAST (mem),
      alloc_params->flags, GST_ALLOCATOR_CAST (allocator), NULL, maxsize,
      alloc_params->align, 0, size);

  mem->desc = params->desc;
  mem->texture = texture;
  mem->staging = staging;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    mem->offset[i] = data.offset[i];
    mem->stride[i] = data.stride[i];
  }

  return GST_MEMORY_CAST (mem);

error:
  if (texture)
    gst_d3d11_device_release_texture (device, texture);
  if (staging)
    gst_d3d11_device_release_texture (device, staging);
  return NULL;
}
