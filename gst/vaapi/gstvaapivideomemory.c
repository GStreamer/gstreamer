/*
 *  gstvaapivideomemory.c - Gstreamer/VA video memory
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

#include "gst/vaapi/sysdeps.h"
#include "gstvaapivideomemory.h"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapivideomemory);
#define GST_CAT_DEFAULT gst_debug_vaapivideomemory

/* ------------------------------------------------------------------------ */
/* --- GstVaapiVideoMemory                                              --- */
/* ------------------------------------------------------------------------ */

GstMemory *
gst_vaapi_video_memory_new(GstAllocator *base_allocator,
    GstVaapiVideoMeta *meta)
{
    GstVaapiVideoAllocator * const allocator =
        GST_VAAPI_VIDEO_ALLOCATOR_CAST(base_allocator);
    const GstVideoInfo *vip;
    GstVaapiVideoMemory *mem;

    mem = g_slice_new(GstVaapiVideoMemory);
    if (!mem)
        return NULL;

    vip = &allocator->video_info;
    gst_memory_init(&mem->parent_instance, 0, base_allocator, NULL,
        GST_VIDEO_INFO_SIZE(vip), 0, 0, GST_VIDEO_INFO_SIZE(vip));

    mem->meta = gst_vaapi_video_meta_ref(meta);
    return GST_MEMORY_CAST(mem);
}

static void
gst_vaapi_video_memory_free(GstVaapiVideoMemory *mem)
{
    gst_vaapi_video_meta_unref(mem->meta);
    g_slice_free(GstVaapiVideoMemory, mem);
}

static gpointer
gst_vaapi_video_memory_map(GstVaapiVideoMemory *mem, gsize maxsize, guint flags)
{
    GST_FIXME("unimplemented GstVaapiVideoAllocator::mem_map() hook");
    return NULL;
}

static void
gst_vaapi_video_memory_unmap(GstVaapiVideoMemory *mem)
{
    GST_FIXME("unimplemented GstVaapiVideoAllocator::mem_unmap() hook");
}

static GstVaapiVideoMemory *
gst_vaapi_video_memory_copy(GstVaapiVideoMemory *mem,
    gssize offset, gssize size)
{
    GST_FIXME("unimplemented GstVaapiVideoAllocator::mem_copy() hook");
    return NULL;
}

static GstVaapiVideoMemory *
gst_vaapi_video_memory_share(GstVaapiVideoMemory *mem,
    gssize offset, gssize size)
{
    GST_FIXME("unimplemented GstVaapiVideoAllocator::mem_share() hook");
    return NULL;
}

static gboolean
gst_vaapi_video_memory_is_span(GstVaapiVideoMemory *mem1,
    GstVaapiVideoMemory *mem2, gsize *offset_ptr)
{
    GST_FIXME("unimplemented GstVaapiVideoAllocator::mem_is_span() hook");
    return FALSE;
}

/* ------------------------------------------------------------------------ */
/* --- GstVaapiVideoAllocator                                           --- */
/* ------------------------------------------------------------------------ */

#define GST_VAAPI_VIDEO_ALLOCATOR_CLASS(klass)  \
    (G_TYPE_CHECK_CLASS_CAST((klass),           \
        GST_VAAPI_TYPE_VIDEO_ALLOCATOR,         \
        GstVaapiVideoAllocatorClass))

#define GST_VAAPI_IS_VIDEO_ALLOCATOR_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_VIDEO_ALLOCATOR))

G_DEFINE_TYPE(GstVaapiVideoAllocator,
              gst_vaapi_video_allocator,
              GST_TYPE_ALLOCATOR)

static GstMemory *
gst_vaapi_video_allocator_alloc(GstAllocator *allocator, gsize size,
    GstAllocationParams *params)
{
    g_warning("use gst_vaapi_video_memory_new() to allocate from "
        "GstVaapiVideoMemory allocator");

    return NULL;
}

static void
gst_vaapi_video_allocator_free(GstAllocator *allocator, GstMemory *mem)
{
    gst_vaapi_video_memory_free(GST_VAAPI_VIDEO_MEMORY_CAST(mem));
}

static void
gst_vaapi_video_allocator_class_init(GstVaapiVideoAllocatorClass *klass)
{
    GstAllocatorClass * const allocator_class = GST_ALLOCATOR_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapivideomemory,
        "vaapivideomemory", 0, "VA-API video memory allocator");

    allocator_class->alloc      = gst_vaapi_video_allocator_alloc;
    allocator_class->free       = gst_vaapi_video_allocator_free;
}

static void
gst_vaapi_video_allocator_init(GstVaapiVideoAllocator *allocator)
{
    GstAllocator * const base_allocator = GST_ALLOCATOR_CAST(allocator);

    base_allocator->mem_type = GST_VAAPI_VIDEO_MEMORY_NAME;
    base_allocator->mem_map = (GstMemoryMapFunction)
        gst_vaapi_video_memory_map;
    base_allocator->mem_unmap = (GstMemoryUnmapFunction)
        gst_vaapi_video_memory_unmap;
    base_allocator->mem_copy = (GstMemoryCopyFunction)
        gst_vaapi_video_memory_copy;
    base_allocator->mem_share = (GstMemoryShareFunction)
        gst_vaapi_video_memory_share;
    base_allocator->mem_is_span = (GstMemoryIsSpanFunction)
        gst_vaapi_video_memory_is_span;
}

GstAllocator *
gst_vaapi_video_allocator_new(GstVaapiDisplay *display, GstCaps *caps)
{
    GstVaapiVideoAllocator *allocator;
    GstVideoInfo *vip;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    allocator = g_object_new(GST_VAAPI_TYPE_VIDEO_ALLOCATOR, NULL);
    if (!allocator)
        return NULL;

    vip = &allocator->video_info;
    gst_video_info_init(vip);
    gst_video_info_from_caps(vip, caps);

    return GST_ALLOCATOR_CAST(allocator);
}
