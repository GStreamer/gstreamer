/* GStreamer Intel MSDK plugin
 * Copyright (c) 2018, Intel Corporation
 * Copyright (c) 2018, Igalia S.L.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGDECE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _WIN32
#include <unistd.h>
#endif
#include <stdlib.h>
#include "gstmsdksystemmemory.h"
#include <string.h>

#ifdef _WIN32
#define posix_memalign(d, a, s) ((*((void**)d) = _aligned_malloc(s, a)) ? 0 : -1)
#endif

#ifndef _WIN32
#define _aligned_free free
#endif

static gboolean
ensure_data (GstMsdkSystemMemory * mem)
{
  gsize size;
  void *data;
  GstVideoInfo *info;
  GstAllocator *allocator;
  GstMsdkSystemAllocator *msdk_allocator;

  allocator = GST_MEMORY_CAST (mem)->allocator;
  msdk_allocator = GST_MSDK_SYSTEM_ALLOCATOR_CAST (allocator);

  info = &msdk_allocator->image_info;
  size = GST_VIDEO_INFO_SIZE (info);

  if (mem->cache)
    return TRUE;

  if (posix_memalign (&data, 32, size) != 0) {
    GST_ERROR ("Memory allocation failed");
    return FALSE;
  }

  mem->cache = data;
  mem->cached_data[0] = mem->cache;
  mem->cached_data[1] = mem->cache + GST_VIDEO_INFO_PLANE_OFFSET (info, 1);
  mem->cached_data[2] = mem->cache + GST_VIDEO_INFO_PLANE_OFFSET (info, 2);

  mem->destination_pitches[0] = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  mem->destination_pitches[1] = GST_VIDEO_INFO_PLANE_STRIDE (info, 1);
  mem->destination_pitches[2] = GST_VIDEO_INFO_PLANE_STRIDE (info, 2);

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
      mem->surface->Data.Y = mem->cached_data[0];
      mem->surface->Data.UV = mem->cached_data[1];
      mem->surface->Data.Pitch = mem->destination_pitches[0];
      break;
    case GST_VIDEO_FORMAT_YV12:
      mem->surface->Data.Y = mem->cached_data[0];
      mem->surface->Data.U = mem->cached_data[2];
      mem->surface->Data.V = mem->cached_data[1];
      mem->surface->Data.Pitch = mem->destination_pitches[0];
      break;
    case GST_VIDEO_FORMAT_I420:
      mem->surface->Data.Y = mem->cached_data[0];
      mem->surface->Data.U = mem->cached_data[1];
      mem->surface->Data.V = mem->cached_data[2];
      mem->surface->Data.Pitch = mem->destination_pitches[0];
      break;
    case GST_VIDEO_FORMAT_YUY2:
      mem->surface->Data.Y = mem->cached_data[0];
      mem->surface->Data.U = mem->surface->Data.Y + 1;
      mem->surface->Data.V = mem->surface->Data.Y + 3;
      mem->surface->Data.Pitch = mem->destination_pitches[0];
      break;
    case GST_VIDEO_FORMAT_UYVY:
      mem->surface->Data.Y = mem->cached_data[0];
      mem->surface->Data.U = mem->surface->Data.Y;
      mem->surface->Data.V = mem->surface->Data.U + 2;
      mem->surface->Data.Pitch = mem->destination_pitches[0];
      break;
    case GST_VIDEO_FORMAT_BGRA:
      mem->surface->Data.B = mem->cached_data[0];
      mem->surface->Data.G = mem->surface->Data.B + 1;
      mem->surface->Data.R = mem->surface->Data.B + 2;
      mem->surface->Data.A = mem->surface->Data.B + 3;
      mem->surface->Data.Pitch = mem->destination_pitches[0];
      break;
#if (MFX_VERSION >= 1028)
    case GST_VIDEO_FORMAT_RGB16:
      mem->surface->Data.R = mem->cached_data[0];
      mem->surface->Data.G = mem->surface->Data.R;
      mem->surface->Data.B = mem->surface->Data.R;
      mem->surface->Data.Pitch = mem->destination_pitches[0];
      break;
#endif
    case GST_VIDEO_FORMAT_VUYA:
      mem->surface->Data.V = mem->cached_data[0];
      mem->surface->Data.U = mem->surface->Data.V + 1;
      mem->surface->Data.Y = mem->surface->Data.V + 2;
      mem->surface->Data.A = mem->surface->Data.V + 3;
      mem->surface->Data.PitchHigh =
          (mfxU16) (mem->destination_pitches[0] / (1 << 16));
      mem->surface->Data.PitchLow =
          (mfxU16) (mem->destination_pitches[0] % (1 << 16));
      break;
    case GST_VIDEO_FORMAT_BGR10A2_LE:
      mem->surface->Data.R = mem->cached_data[0];
      mem->surface->Data.G = mem->surface->Data.R;
      mem->surface->Data.B = mem->surface->Data.R;
      mem->surface->Data.A = mem->surface->Data.R;
      mem->surface->Data.Pitch = mem->destination_pitches[0];
      break;
    case GST_VIDEO_FORMAT_Y210:
    case GST_VIDEO_FORMAT_Y212_LE:
      mem->surface->Data.Y = mem->cached_data[0];
      mem->surface->Data.U = mem->surface->Data.Y + 2;
      mem->surface->Data.V = mem->surface->Data.Y + 6;
      mem->surface->Data.Pitch = mem->destination_pitches[0];
      break;
    case GST_VIDEO_FORMAT_Y410:
      mem->surface->Data.U = mem->cached_data[0];       /* Data.Y410 */
      mem->surface->Data.Pitch = mem->destination_pitches[0];
      break;
    case GST_VIDEO_FORMAT_Y412_LE:
      mem->surface->Data.U = mem->cached_data[0];
      mem->surface->Data.Y = mem->surface->Data.U + 2;
      mem->surface->Data.V = mem->surface->Data.U + 4;
      mem->surface->Data.A = mem->surface->Data.U + 6;
      mem->surface->Data.Pitch = mem->destination_pitches[0];
      break;

    default:
      g_assert_not_reached ();
      break;
  }

  return TRUE;
}

static mfxFrameSurface1 *
gst_msdk_system_allocator_create_surface (GstAllocator * allocator)
{
  mfxFrameInfo frame_info = { {0,}, 0, };
  mfxFrameSurface1 *surface;
  GstMsdkSystemAllocator *msdk_system_allocator =
      GST_MSDK_SYSTEM_ALLOCATOR_CAST (allocator);

  surface = (mfxFrameSurface1 *) g_slice_new0 (mfxFrameSurface1);

  if (!surface) {
    GST_ERROR ("failed to allocate surface");
    return NULL;
  }

  gst_msdk_set_mfx_frame_info_from_video_info (&frame_info,
      &msdk_system_allocator->image_info);

  surface->Info = frame_info;

  return surface;
}

GstMemory *
gst_msdk_system_memory_new (GstAllocator * base_allocator)
{
  GstMsdkSystemAllocator *allocator;
  GstVideoInfo *vip;
  GstMsdkSystemMemory *mem;

  g_return_val_if_fail (base_allocator, NULL);
  g_return_val_if_fail (GST_IS_MSDK_SYSTEM_ALLOCATOR (base_allocator), NULL);

  allocator = GST_MSDK_SYSTEM_ALLOCATOR_CAST (base_allocator);

  mem = g_slice_new0 (GstMsdkSystemMemory);
  if (!mem)
    return NULL;

  mem->surface = gst_msdk_system_allocator_create_surface (base_allocator);

  if (!mem->surface) {
    g_slice_free (GstMsdkSystemMemory, mem);
    return NULL;
  }

  vip = &allocator->image_info;
  gst_memory_init (&mem->parent_instance, 0,
      base_allocator, NULL, GST_VIDEO_INFO_SIZE (vip), 0, 0,
      GST_VIDEO_INFO_SIZE (vip));

  if (!ensure_data (mem)) {
    g_slice_free (mfxFrameSurface1, mem->surface);
    g_slice_free (GstMsdkSystemMemory, mem);
    return NULL;
  }

  return GST_MEMORY_CAST (mem);
}

static gpointer
gst_msdk_system_memory_map_full (GstMemory * base_mem, GstMapInfo * info,
    gsize maxsize)
{
  GstMsdkSystemMemory *const mem = GST_MSDK_SYSTEM_MEMORY_CAST (base_mem);

  g_return_val_if_fail (mem, NULL);

  if (!mem->surface) {
    GST_WARNING ("The surface is not allocated");
    return NULL;
  }

  if ((info->flags & GST_MAP_WRITE) && mem->surface
      && mem->surface->Data.Locked) {
    GST_WARNING ("The surface in memory %p is not still available", mem);
    return NULL;
  }

  switch (mem->surface->Info.FourCC) {
    case MFX_FOURCC_RGB4:
      return mem->surface->Data.B;      /* The first channel is B */

      /* The first channel in memory is V for MFX_FOURCC_AYUV (GST_VIDEO_FORMAT_VUYA) format */
    case MFX_FOURCC_AYUV:
      return mem->surface->Data.V;

#if (MFX_VERSION >= 1027)
    case MFX_FOURCC_Y410:
      return mem->surface->Data.U;      /* Data.Y410 */
#endif

#if (MFX_VERSION >= 1031)
    case MFX_FOURCC_Y416:
      return mem->surface->Data.U;      /* The first channel is U */
#endif

    default:
      return mem->surface->Data.Y;
  }
}

static void
gst_msdk_system_memory_unmap (GstMemory * base_mem)
{
}

static GstMemory *
gst_msdk_system_memory_copy (GstMemory * base_mem, gssize offset, gssize size)
{
  GstMsdkSystemMemory *copy;
  GstVideoInfo *info;
  GstMsdkSystemAllocator *msdk_allocator;
  gsize mem_size;

  /* FIXME: can we consider offset and size here ? */
  copy =
      (GstMsdkSystemMemory *) gst_msdk_system_memory_new (base_mem->allocator);

  msdk_allocator = GST_MSDK_SYSTEM_ALLOCATOR_CAST (base_mem->allocator);

  info = &msdk_allocator->image_info;
  mem_size = GST_VIDEO_INFO_SIZE (info);

  memcpy (copy->cache, GST_MSDK_SYSTEM_MEMORY_CAST (base_mem)->cache, mem_size);

  return GST_MEMORY_CAST (copy);
}

/* GstMsdkSystemAllocator */
G_DEFINE_TYPE (GstMsdkSystemAllocator, gst_msdk_system_allocator,
    GST_TYPE_ALLOCATOR);

static void
gst_msdk_system_allocator_free (GstAllocator * allocator, GstMemory * base_mem)
{
  GstMsdkSystemMemory *const mem = GST_MSDK_SYSTEM_MEMORY_CAST (base_mem);

  _aligned_free (mem->cache);
  g_slice_free (mfxFrameSurface1, mem->surface);
  g_slice_free (GstMsdkSystemMemory, mem);
}

static GstMemory *
gst_msdk_system_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  return gst_msdk_system_memory_new (allocator);
}

static void
gst_msdk_system_allocator_class_init (GstMsdkSystemAllocatorClass * klass)
{
  GstAllocatorClass *const allocator_class = GST_ALLOCATOR_CLASS (klass);

  allocator_class->alloc = gst_msdk_system_allocator_alloc;
  allocator_class->free = gst_msdk_system_allocator_free;
}

static void
gst_msdk_system_allocator_init (GstMsdkSystemAllocator * allocator)
{
  GstAllocator *const base_allocator = GST_ALLOCATOR_CAST (allocator);

  base_allocator->mem_type = GST_MSDK_SYSTEM_MEMORY_NAME;
  base_allocator->mem_map_full = gst_msdk_system_memory_map_full;
  base_allocator->mem_unmap = gst_msdk_system_memory_unmap;
  base_allocator->mem_copy = gst_msdk_system_memory_copy;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

GstAllocator *
gst_msdk_system_allocator_new (GstVideoInfo * image_info)
{
  GstMsdkSystemAllocator *allocator;

  g_return_val_if_fail (image_info != NULL, NULL);

  allocator = g_object_new (GST_TYPE_MSDK_SYSTEM_ALLOCATOR, NULL);
  if (!allocator)
    return NULL;

  allocator->image_info = *image_info;

  return GST_ALLOCATOR_CAST (allocator);
}
