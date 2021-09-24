/*
 *  gstvaapivideomemory.h - Gstreamer/VA video memory
 *
 *  Copyright (C) 2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_VIDEO_MEMORY_H
#define GST_VAAPI_VIDEO_MEMORY_H

#include <gst/gstallocator.h>
#include <gst/video/video-info.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapivideopool.h>
#include "gstvaapivideometa.h"
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

typedef struct _GstVaapiVideoMemory GstVaapiVideoMemory;
typedef struct _GstVaapiVideoAllocator GstVaapiVideoAllocator;
typedef struct _GstVaapiVideoAllocatorClass GstVaapiVideoAllocatorClass;
typedef struct _GstVaapiDmaBufAllocator GstVaapiDmaBufAllocator;
typedef struct _GstVaapiDmaBufAllocatorClass GstVaapiDmaBufAllocatorClass;

/* ------------------------------------------------------------------------ */
/* --- GstVaapiVideoMemory                                              --- */
/* ------------------------------------------------------------------------ */

#define GST_VAAPI_VIDEO_MEMORY_CAST(mem) \
  ((GstVaapiVideoMemory *) (mem))

#define GST_VAAPI_IS_VIDEO_MEMORY(mem) \
  ((mem) && (mem)->allocator && GST_VAAPI_IS_VIDEO_ALLOCATOR((mem)->allocator))

#define GST_VAAPI_VIDEO_MEMORY_NAME             "GstVaapiVideoMemory"

#define GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE   "memory:VASurface"

#define GST_VAAPI_VIDEO_MEMORY_FLAG_IS_SET(mem, flag) \
  GST_MEMORY_FLAG_IS_SET (mem, flag)
#define GST_VAAPI_VIDEO_MEMORY_FLAG_SET(mem, flag) \
  GST_MINI_OBJECT_FLAG_SET (mem, flag)
#define GST_VAAPI_VIDEO_MEMORY_FLAG_UNSET(mem, flag) \
  GST_MEMORY_FLAG_UNSET (mem, flag)

/**
 * GstVaapiVideoMemoryMapType:
 * @GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_SURFACE: map with gst_buffer_map()
 *   and flags = 0x00 to return a #GstVaapiSurfaceProxy
 * @GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_PLANAR: map individual plane with
 *   gst_video_frame_map()
 * @GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_LINEAR: map with gst_buffer_map()
 *   and flags = GST_MAP_READ to return the raw pixels of the whole image
 *
 * The set of all #GstVaapiVideoMemory map types.
 */
typedef enum
{
  GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_SURFACE = 1,
  GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_PLANAR,
  GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_LINEAR
} GstVaapiVideoMemoryMapType;

/**
 * GstVaapiVideoMemoryFlags:
 * @GST_VAAPI_VIDEO_MEMORY_FLAG_SURFACE_IS_CURRENT: The embedded
 *   #GstVaapiSurface has the up-to-date video frame contents.
 * @GST_VAAPI_VIDEO_MEMORY_FLAG_IMAGE_IS_CURRENT: The embedded
 *   #GstVaapiImage has the up-to-date video frame contents.
 *
 * The set of extended #GstMemory flags.
 */
typedef enum
{
  GST_VAAPI_VIDEO_MEMORY_FLAG_SURFACE_IS_CURRENT = GST_MEMORY_FLAG_LAST << 0,
  GST_VAAPI_VIDEO_MEMORY_FLAG_IMAGE_IS_CURRENT = GST_MEMORY_FLAG_LAST << 1,
} GstVaapiVideoMemoryFlags;

/**
 * GstVaapiImageUsageFlags:
 * @GST_VAAPI_IMAGE_USAGE_FLAG_NATIVE_FORMATS: will use vaCreateImage +
 * va{Put,Get}Image when writing or reading onto the system memory.
 * @GST_VAAPI_IMAGE_USAGE_FLAG_DIRECT_UPLOAD: will try to use
 * vaDeriveImage when writing data from the system memory.
 * @GST_VAAPI_IMAGE_USAGE_FLAG_DIRECT_RENDER: will try to use
 * vaDeriveImage with reading data onto the system memory.
 *
 * Set the usage of GstVaapiImage in GstVaapiVideoMemory.
 **/
typedef enum {
  GST_VAAPI_IMAGE_USAGE_FLAG_NATIVE_FORMATS,
  GST_VAAPI_IMAGE_USAGE_FLAG_DIRECT_UPLOAD,
  GST_VAAPI_IMAGE_USAGE_FLAG_DIRECT_RENDER,
} GstVaapiImageUsageFlags;

/**
 * GstVaapiVideoMemory:
 *
 * A VA video memory object holder, including VA surfaces, images and
 * proxies.
 */
struct _GstVaapiVideoMemory
{
  GstMemory parent_instance;

  /*< private >*/
  GstVaapiSurfaceProxy *proxy;
  const GstVideoInfo *surface_info;
  GstVaapiSurface *surface;
  const GstVideoInfo *image_info;
  GstVaapiImage *image;
  GstVaapiVideoMeta *meta;
  guint map_type;
  gint map_count;
  VASurfaceID map_surface_id;
  GstVaapiImageUsageFlags usage_flag;
  GMutex lock;
};

G_GNUC_INTERNAL
GstMemory *
gst_vaapi_video_memory_new (GstAllocator * allocator, GstVaapiVideoMeta * meta);

G_GNUC_INTERNAL
gboolean
gst_video_meta_map_vaapi_memory (GstVideoMeta * meta, guint plane,
    GstMapInfo * info, gpointer * data, gint * stride, GstMapFlags flags);

G_GNUC_INTERNAL
gboolean
gst_video_meta_unmap_vaapi_memory (GstVideoMeta * meta, guint plane,
    GstMapInfo * info);

G_GNUC_INTERNAL
void
gst_vaapi_video_memory_reset_surface (GstVaapiVideoMemory * mem);

G_GNUC_INTERNAL
gboolean
gst_vaapi_video_memory_sync (GstVaapiVideoMemory * mem);

/* ------------------------------------------------------------------------ */
/* --- GstVaapiVideoAllocator                                           --- */
/* ------------------------------------------------------------------------ */
#define GST_MAP_VAAPI (GST_MAP_FLAG_LAST << 1)

#define GST_VAAPI_VIDEO_ALLOCATOR_CAST(allocator) \
  ((GstVaapiVideoAllocator *) (allocator))

#define GST_VAAPI_TYPE_VIDEO_ALLOCATOR \
  (gst_vaapi_video_allocator_get_type ())
#define GST_VAAPI_VIDEO_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_VAAPI_TYPE_VIDEO_ALLOCATOR, \
      GstVaapiVideoAllocator))
#define GST_VAAPI_IS_VIDEO_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_VAAPI_TYPE_VIDEO_ALLOCATOR))

/**
 * GstVaapiVideoAllocator:
 *
 * A VA video memory allocator object.
 */
struct _GstVaapiVideoAllocator
{
  GstAllocator parent_instance;

  /*< private >*/
  GstVideoInfo allocation_info;
  GstVideoInfo surface_info;
  GstVaapiVideoPool *surface_pool;
  GstVideoInfo image_info;
  GstVaapiVideoPool *image_pool;
  GstVaapiImageUsageFlags usage_flag;
};

/**
 * GstVaapiVideoAllocatorClass:
 *
 * A VA video memory allocator class.
 */
struct _GstVaapiVideoAllocatorClass
{
  GstAllocatorClass parent_class;
};

G_GNUC_INTERNAL
GType
gst_vaapi_video_allocator_get_type (void) G_GNUC_CONST;

G_GNUC_INTERNAL
GstAllocator *
gst_vaapi_video_allocator_new (GstVaapiDisplay * display,
    const GstVideoInfo * alloc_info, guint surface_alloc_flags,
    GstVaapiImageUsageFlags req_usage_flag);

/* ------------------------------------------------------------------------ */
/* --- GstVaapiDmaBufMemory                                             --- */
/* ------------------------------------------------------------------------ */

G_GNUC_INTERNAL
GstMemory *
gst_vaapi_dmabuf_memory_new (GstAllocator * allocator,
    GstVaapiVideoMeta * meta);

G_GNUC_INTERNAL
gboolean
gst_vaapi_dmabuf_memory_holds_surface (GstMemory * mem);

/* ------------------------------------------------------------------------ */
/* --- GstVaapiDmaBufAllocator                                          --- */
/* ------------------------------------------------------------------------ */

#define GST_VAAPI_DMABUF_ALLOCATOR_CAST(allocator) \
  ((GstVaapiDmaBufAllocator *) (allocator))

#define GST_VAAPI_TYPE_DMABUF_ALLOCATOR \
  (gst_vaapi_dmabuf_allocator_get_type ())
#define GST_VAAPI_DMABUF_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_VAAPI_TYPE_DMABUF_ALLOCATOR, \
      GstVaapiDmaBufAllocator))
#define GST_VAAPI_IS_DMABUF_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_VAAPI_TYPE_DMABUF_ALLOCATOR))

#define GST_VAAPI_DMABUF_ALLOCATOR_NAME          "GstVaapiDmaBufAllocator"

/**
 * GstVaapiDmaBufAllocator:
 *
 * A VA dmabuf memory allocator object.
 */
struct _GstVaapiDmaBufAllocator
{
  GstDmaBufAllocator parent_instance;

  /*< private >*/
  GstPadDirection direction;
};

/**
 * GstVaapiDmaBufoAllocatorClass:
 *
 * A VA dmabuf memory allocator class.
 */
struct _GstVaapiDmaBufAllocatorClass
{
  GstDmaBufAllocatorClass parent_class;
};

G_GNUC_INTERNAL
GType
gst_vaapi_dmabuf_allocator_get_type (void) G_GNUC_CONST;

G_GNUC_INTERNAL
GstAllocator *
gst_vaapi_dmabuf_allocator_new (GstVaapiDisplay * display,
    const GstVideoInfo * alloc_info, guint surface_alloc_flags,
    GstPadDirection direction);

G_GNUC_INTERNAL
const GstVideoInfo *
gst_allocator_get_vaapi_video_info (GstAllocator * allocator,
    guint * out_flags_ptr);

G_GNUC_INTERNAL
gboolean
gst_allocator_set_vaapi_video_info (GstAllocator * allocator,
    const GstVideoInfo * alloc_info, guint surface_alloc_flags);

G_GNUC_INTERNAL
void
gst_allocator_set_vaapi_negotiated_video_info (GstAllocator * allocator,
    const GstVideoInfo * negotiated_vinfo);

G_GNUC_INTERNAL
GstVideoInfo *
gst_allocator_get_vaapi_negotiated_video_info (GstAllocator * allocator);

G_GNUC_INTERNAL
gboolean
gst_vaapi_is_dmabuf_allocator (GstAllocator * allocator);

G_GNUC_INTERNAL
gboolean
gst_vaapi_dmabuf_can_map (GstVaapiDisplay * display, GstAllocator * allocator);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_MEMORY_H */
