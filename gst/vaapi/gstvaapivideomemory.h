/*
 *  gstvaapivideomemory.h - Gstreamer/VA video memory
 *
 *  Copyright (C) 2013 Intel Corporation
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

/**
 * GstVaapiVideoMemory:
 *
 * A VA video memory object holder, including VA surfaces, images and
 * proxies.
 */
struct _GstVaapiVideoMemory {
    GstMemory parent_instance;

    /*< private >*/
    const GstVideoInfo *surface_info;
    GstVaapiSurface    *surface;
    const GstVideoInfo *image_info;
    GstVaapiImage      *image;
    GstVaapiVideoMeta  *meta;
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
    GstVideoInfo        image_info;
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
gst_vaapi_video_allocator_new(GstVaapiDisplay *display, GstCaps *caps);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_MEMORY_H */
