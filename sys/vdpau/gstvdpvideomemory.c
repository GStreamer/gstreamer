/*
 * GStreamer
 * Copyright (C) 2012 Edward Hervey <edward@collabora.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>

#include "gstvdpvideomemory.h"
#include "gstvdputils.h"

GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);
GST_DEBUG_CATEGORY_STATIC (gst_vdp_video_mem_debug);
#define GST_CAT_DEFAULT gst_vdp_video_mem_debug

static GstAllocator *_vdp_video_allocator;


static void
_vdp_video_mem_init (GstVdpVideoMemory * mem, GstAllocator * allocator,
    GstMemory * parent, GstVdpDevice * device, GstVideoInfo * info)
{
  gst_memory_init (GST_MEMORY_CAST (mem), GST_MEMORY_FLAG_NO_SHARE,
      allocator, parent, GST_VIDEO_INFO_SIZE (info), 0, 0,
      GST_VIDEO_INFO_SIZE (info));

  mem->device = gst_object_ref (device);
  mem->info = info;
  mem->chroma_type = gst_video_info_to_vdp_chroma_type (info);
  mem->ycbcr_format =
      gst_video_format_to_vdp_ycbcr (GST_VIDEO_INFO_FORMAT (info));
  mem->refcount = 0;

  GST_DEBUG ("new VdpVideo memory");
}


static GstVdpVideoMemory *
_vdp_video_mem_new (GstAllocator * allocator, GstMemory * parent,
    GstVdpDevice * device, GstVideoInfo * info)
{
  VdpStatus status;
  GstVdpVideoMemory *mem;
  VdpVideoSurface surface;

  mem = g_slice_new0 (GstVdpVideoMemory);
  _vdp_video_mem_init (mem, allocator, parent, device, info);

  GST_TRACE
      ("Calling VdpVideoSurfaceCreate(chroma_type:%d, width:%d, height:%d)",
      mem->chroma_type, mem->info->width, mem->info->height);

  status =
      device->vdp_video_surface_create (device->device, mem->chroma_type,
      mem->info->width, mem->info->height, &surface);

  if (status != VDP_STATUS_OK)
    goto create_error;

  /* device->vdp_video_surface_get_parameters (device->device, &chroma_type, */
  /*     &width, &height); */

  GST_TRACE ("created surface %u", surface);

  mem->surface = surface;

  return mem;

  /* ERRORS */
create_error:
  {
    GST_ERROR ("Failed to create video surface: %s",
        device->vdp_get_error_string (status));
    g_slice_free (GstVdpVideoMemory, mem);
    return NULL;
  }
}

static gboolean
ensure_data (GstVdpVideoMemory * vmem)
{
  VdpStatus vdp_stat;
  GstVideoInfo *info = vmem->info;
#ifndef GST_DISABLE_GST_DEBUG
  GstClockTime before, after;
#endif

  if (g_atomic_int_add (&vmem->refcount, 1) > 1)
    return TRUE;

  /* Allocate enough room to store data */
  vmem->cache = g_malloc (GST_VIDEO_INFO_SIZE (info));
  vmem->cached_data[0] = vmem->cache;
  vmem->cached_data[1] = vmem->cache + GST_VIDEO_INFO_PLANE_OFFSET (info, 1);
  vmem->cached_data[2] = vmem->cache + GST_VIDEO_INFO_PLANE_OFFSET (info, 2);
  vmem->destination_pitches[0] = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  vmem->destination_pitches[1] = GST_VIDEO_INFO_PLANE_STRIDE (info, 1);
  vmem->destination_pitches[2] = GST_VIDEO_INFO_PLANE_STRIDE (info, 2);

  GST_DEBUG ("cached_data %p %p %p",
      vmem->cached_data[0], vmem->cached_data[1], vmem->cached_data[2]);
  GST_DEBUG ("pitches %d %d %d",
      vmem->destination_pitches[0],
      vmem->destination_pitches[1], vmem->destination_pitches[2]);

#ifndef GST_DISABLE_GST_DEBUG
  before = gst_util_get_timestamp ();
#endif
  vdp_stat =
      vmem->device->vdp_video_surface_get_bits_ycbcr (vmem->surface,
      vmem->ycbcr_format, vmem->cached_data, vmem->destination_pitches);
#ifndef GST_DISABLE_GST_DEBUG
  after = gst_util_get_timestamp ();
#endif

  GST_CAT_WARNING (GST_CAT_PERFORMANCE, "Downloading took %" GST_TIME_FORMAT,
      GST_TIME_ARGS (after - before));

  if (vdp_stat != VDP_STATUS_OK) {
    GST_ERROR ("Failed to get bits : %s",
        vmem->device->vdp_get_error_string (vdp_stat));
    g_free (vmem->cache);
    vmem->cache = NULL;
    return FALSE;
  }

  return TRUE;
}

static void
release_data (GstVdpVideoMemory * vmem)
{
  g_return_if_fail (vmem->refcount > 0);

  if (g_atomic_int_dec_and_test (&vmem->refcount)) {
    g_free (vmem->cache);
  }
}

static gpointer
_vdp_video_mem_map (GstVdpVideoMemory * vmem, gsize maxsize, GstMapFlags flags)
{
  GST_DEBUG ("surface:%d, maxsize:%" G_GSIZE_FORMAT ", flags:%d",
      vmem->surface, maxsize, flags);

  if (!ensure_data (vmem))
    return NULL;

  return vmem->cache;
}

static void
_vdp_video_mem_unmap (GstVdpVideoMemory * vmem)
{
  GST_DEBUG ("surface:%d", vmem->surface);

  release_data (vmem);
}


static GstMemory *
_vdp_video_mem_copy (GstVdpVideoMemory * src, gssize offset, gssize size)
{
  GST_FIXME ("Implement !");
  return NULL;
}

static GstMemory *
_vdp_video_mem_share (GstVdpVideoMemory * mem, gssize offset, gssize size)
{
  GST_FIXME ("Implement !");
  return NULL;
}

static gboolean
_vdp_video_mem_is_span (GstVdpVideoMemory * mem1, GstVdpVideoMemory * mem2,
    gsize * offset)
{
  return FALSE;
}

static GstMemory *
_vdp_video_mem_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_warning ("use gst_vdp_video_memory_alloc () to allocate from this "
      "GstVdpVideoMemory allocator");

  return NULL;
}

static void
_vdp_video_mem_free (GstAllocator * allocator, GstMemory * mem)
{
  GstVdpVideoMemory *vmem = (GstVdpVideoMemory *) mem;
  VdpStatus status;

  GST_DEBUG ("Destroying surface %d", vmem->surface);

  status = vmem->device->vdp_video_surface_destroy (vmem->surface);
  if (status != VDP_STATUS_OK)
    GST_ERROR ("Couldn't destroy the VdpVideoSurface: %s",
        vmem->device->vdp_get_error_string (status));

  gst_object_unref (vmem->device);

  g_free (vmem->cache);

  g_slice_free (GstVdpVideoMemory, vmem);
}

/**
 * gst_vdp_video_memory_alloc:
 * @device: a #GstVdpDevice
 * @info: the #GstVideoInfo describing the format to use
 *
 * Returns: a GstMemory object with a VdpVideoSurface specified by @info
 * from @device
 */
GstMemory *
gst_vdp_video_memory_alloc (GstVdpDevice * device, GstVideoInfo * info)
{
  return (GstMemory *) _vdp_video_mem_new (_vdp_video_allocator, NULL, device,
      info);
}

G_DEFINE_TYPE (GstVdpVideoAllocator, gst_vdp_video_allocator,
    GST_TYPE_ALLOCATOR);

static void
gst_vdp_video_allocator_class_init (GstVdpVideoAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = _vdp_video_mem_alloc;
  allocator_class->free = _vdp_video_mem_free;
}

static void
gst_vdp_video_allocator_init (GstVdpVideoAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_VDP_VIDEO_MEMORY_ALLOCATOR;
  alloc->mem_map = (GstMemoryMapFunction) _vdp_video_mem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) _vdp_video_mem_unmap;
  alloc->mem_copy = (GstMemoryCopyFunction) _vdp_video_mem_copy;
  alloc->mem_share = (GstMemoryShareFunction) _vdp_video_mem_share;
  alloc->mem_is_span = (GstMemoryIsSpanFunction) _vdp_video_mem_is_span;
}

/**
 * gst_vdp_video_memory_init:
 *
 * Initializes the GL Memory allocator. It is safe to call this function
 * multiple times.  This must be called before any other GstVdpVideoMemory operation.
 */
void
gst_vdp_video_memory_init (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    _vdp_video_allocator =
        g_object_new (gst_vdp_video_allocator_get_type (), NULL);

    gst_allocator_register (GST_VDP_VIDEO_MEMORY_ALLOCATOR,
        gst_object_ref (_vdp_video_allocator));
    GST_DEBUG_CATEGORY_INIT (gst_vdp_video_mem_debug, "vdpvideomem", 0,
        "VDPAU VideoSurface Memory/Allocator");
    GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
    g_once_init_leave (&_init, 1);
  }
}

gboolean
gst_vdp_video_memory_map (GstVideoMeta * meta, guint plane, GstMapInfo * info,
    gpointer * data, gint * stride, GstMapFlags flags)
{
  GstBuffer *buffer = meta->buffer;
  GstVdpVideoMemory *vmem =
      (GstVdpVideoMemory *) gst_buffer_get_memory (buffer, 0);

  /* Only handle GstVdpVideoMemory */
  g_return_val_if_fail (((GstMemory *) vmem)->allocator == _vdp_video_allocator,
      FALSE);

  GST_DEBUG ("plane:%d", plane);

  /* download if not already done */
  if (!ensure_data (vmem))
    return FALSE;

  *data = vmem->cached_data[plane];
  *stride = vmem->destination_pitches[plane];

  return TRUE;
}

gboolean
gst_vdp_video_memory_unmap (GstVideoMeta * meta, guint plane, GstMapInfo * info)
{
  GstVdpVideoMemory *vmem =
      (GstVdpVideoMemory *) gst_buffer_get_memory (meta->buffer, 0);

  GST_DEBUG ("plane:%d", plane);

  GST_FIXME ("implement unmap (and potential upload on last unmap)");

  release_data (vmem);

  return TRUE;
}
