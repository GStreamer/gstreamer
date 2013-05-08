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

#ifndef _GST_VDP_VIDEO_MEMORY_H_
#define _GST_VDP_VIDEO_MEMORY_H_

#include <gst/gst.h>
#include <gst/gstmemory.h>
#include <gst/gstallocator.h>
#include <gst/video/video-info.h>
#include <gst/video/gstvideometa.h>

#include "gstvdpdevice.h"

G_BEGIN_DECLS

#define GST_TYPE_VDP_VIDEO_ALLOCATOR (gst_vdp_video_allocator_get_type())
GType gst_vdp_video_allocator_get_type(void);

#define GST_IS_VDP_VIDEO_ALLOCATOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VDP_VIDEO_ALLOCATOR))
#define GST_IS_VDP_VIDEO_ALLOCATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VDP_VIDEO_ALLOCATOR))
#define GST_VDP_VIDEO_ALLOCATOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VDP_VIDEO_ALLOCATOR, GstVdpVideoAllocatorClass))
#define GST_VDP_VIDEO_ALLOCATOR(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VDP_VIDEO_ALLOCATOR, GstVdpVideoAllocator))
#define GST_VDP_VIDEO_ALLOCATOR_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VDP_VIDEO_ALLOCATOR, GstVdpVideoAllocatorClass))
#define GST_VDP_VIDEO_ALLOCATOR_CAST(obj)            ((GstVdpVideoAllocator *)(obj))

typedef struct _GstVdpVideoMemory GstVdpVideoMemory;
typedef struct _GstVdpVideoAllocator GstVdpVideoAllocator;
typedef struct _GstVdpVideoAllocatorClass GstVdpVideoAllocatorClass;

/**
 * GstVdpVideoMemory:
 * @mem: the parent object
 * @device: the #GstVdpDevice to use
 * @surface: the #VdpVideoSurface
 *
 * Represents information about a #VdpVideoSurface
 */
struct _GstVdpVideoMemory
{
  GstMemory          mem;

  GstVdpDevice      *device;
  VdpVideoSurface    surface;

  GstVideoInfo      *info;
  VdpChromaType      chroma_type;
  VdpYCbCrFormat     ycbcr_format;

  /* Cached data for mapping */
  volatile gint      refcount;
  GstMapFlags        map_flags;
  guint		     n_planes;
  guint8            *cache;
  void *             cached_data[4];
  uint32_t           destination_pitches[4];
};

#define GST_VDP_VIDEO_MEMORY_ALLOCATOR   "VdpVideoMemory"

#define GST_CAPS_FEATURE_MEMORY_VDPAU    "memory:VdpVideoSurface"

void gst_vdp_video_memory_init (void);

GstMemory *
gst_vdp_video_memory_alloc (GstVdpDevice * device, GstVideoInfo *info);

gboolean gst_vdp_video_memory_map(GstVideoMeta * meta, guint plane,
				  GstMapInfo * info, gpointer * data,
				  gint * stride, GstMapFlags flags);
gboolean gst_vdp_video_memory_unmap(GstVideoMeta * meta, guint plane,
				    GstMapInfo * info);

struct _GstVdpVideoAllocator
{
  GstAllocator parent;
};

struct _GstVdpVideoAllocatorClass
{
  GstAllocatorClass parent_class;
};

G_END_DECLS

#endif /* _GST_VDP_VIDEO_MEMORY_H_ */
