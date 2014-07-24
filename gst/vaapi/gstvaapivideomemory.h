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

G_BEGIN_DECLS

typedef struct _GstVaapiVideoMemory             GstVaapiVideoMemory;
typedef struct _GstVaapiVideoAllocator          GstVaapiVideoAllocator;
typedef struct _GstVaapiVideoAllocatorClass     GstVaapiVideoAllocatorClass;

/* ------------------------------------------------------------------------ */
/* --- GstVaapiVideoMemory                                              --- */
/* ------------------------------------------------------------------------ */

#define GST_VAAPI_VIDEO_MEMORY_CAST(mem) \
    ((GstVaapiVideoMemory *)(mem))

#define GST_VAAPI_VIDEO_MEMORY_NAME             "GstVaapiVideoMemory"

#if GST_CHECK_VERSION(1,1,0)
#define GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE   "memory:VASurface"
#endif

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
typedef enum {
    GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_SURFACE = 1,
    GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_PLANAR,
    GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_LINEAR
} GstVaapiVideoMemoryMapType;

/**
 * GstVaapiVideoMemory:
 *
 * A VA video memory object holder, including VA surfaces, images and
 * proxies.
 */
struct _GstVaapiVideoMemory {
    GstMemory parent_instance;

    /*< private >*/
    GstVaapiSurfaceProxy *proxy;
    const GstVideoInfo *surface_info;
    GstVaapiSurface    *surface;
    const GstVideoInfo *image_info;
    GstVaapiImage      *image;
    GstVaapiVideoMeta  *meta;
    guint               map_type;
    gint                map_count;
    gboolean            use_direct_rendering;
};

G_GNUC_INTERNAL
GstMemory *
gst_vaapi_video_memory_new(GstAllocator *allocator, GstVaapiVideoMeta *meta);

G_GNUC_INTERNAL
gboolean
gst_video_meta_map_vaapi_memory(GstVideoMeta *meta, guint plane,
    GstMapInfo *info, gpointer *data, gint *stride, GstMapFlags flags);

G_GNUC_INTERNAL
gboolean
gst_video_meta_unmap_vaapi_memory(GstVideoMeta *meta, guint plane,
    GstMapInfo *info);

G_GNUC_INTERNAL
void
gst_vaapi_video_memory_reset_surface(GstVaapiVideoMemory *mem);

/* ------------------------------------------------------------------------ */
/* --- GstVaapiVideoAllocator                                           --- */
/* ------------------------------------------------------------------------ */

#define GST_VAAPI_VIDEO_ALLOCATOR_CAST(allocator) \
    ((GstVaapiVideoAllocator *)(allocator))

#define GST_VAAPI_TYPE_VIDEO_ALLOCATOR \
    (gst_vaapi_video_allocator_get_type())

#define GST_VAAPI_VIDEO_ALLOCATOR(obj)          \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),          \
        GST_VAAPI_TYPE_VIDEO_ALLOCATOR,         \
        GstVaapiVideoAllocator))

#define GST_VAAPI_IS_VIDEO_ALLOCATOR(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_VIDEO_ALLOCATOR))

#define GST_VAAPI_VIDEO_ALLOCATOR_NAME          "GstVaapiVideoAllocator"

/**
 * GstVaapiVideoAllocator:
 *
 * A VA video memory allocator object.
 */
struct _GstVaapiVideoAllocator {
    GstAllocator parent_instance;

    /*< private >*/
    GstVideoInfo        video_info;
    GstVideoInfo        surface_info;
    GstVaapiVideoPool  *surface_pool;
    GstVideoInfo        image_info;
    GstVaapiVideoPool  *image_pool;
    gboolean            has_direct_rendering;
};

/**
 * GstVaapiVideoAllocatorClass:
 *
 * A VA video memory allocator class.
 */
struct _GstVaapiVideoAllocatorClass {
    GstAllocatorClass parent_class;
};

G_GNUC_INTERNAL
GType
gst_vaapi_video_allocator_get_type(void) G_GNUC_CONST;

G_GNUC_INTERNAL
GstAllocator *
gst_vaapi_video_allocator_new(GstVaapiDisplay *display,
    const GstVideoInfo *vip);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_MEMORY_H */
