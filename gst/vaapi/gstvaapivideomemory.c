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

#ifndef GST_VIDEO_INFO_FORMAT_STRING
#define GST_VIDEO_INFO_FORMAT_STRING(vip) \
    gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(vip))
#endif

/* ------------------------------------------------------------------------ */
/* --- GstVaapiVideoMemory                                              --- */
/* ------------------------------------------------------------------------ */

static GstVaapiImage *
new_image(GstVaapiDisplay *display, const GstVideoInfo *vip)
{
    GstVaapiImageFormat format;

    format = gst_vaapi_image_format_from_video(GST_VIDEO_INFO_FORMAT(vip));
    if (!format)
        return NULL;

    return gst_vaapi_image_new(display, format,
        GST_VIDEO_INFO_WIDTH(vip), GST_VIDEO_INFO_HEIGHT(vip));
}

static gboolean
ensure_image(GstVaapiVideoMemory *mem)
{
    if (!mem->image && mem->use_direct_rendering) {
        mem->image = gst_vaapi_surface_derive_image(mem->surface);
        if (!mem->image) {
            GST_WARNING("failed to derive image, fallbacking to copy");
            mem->use_direct_rendering = FALSE;
        }
    }

    if (!mem->image) {
        GstVaapiDisplay * const display =
            gst_vaapi_video_meta_get_display(mem->meta);

        mem->image = new_image(display, mem->image_info);
        if (!mem->image)
            return FALSE;
    }
    gst_vaapi_video_meta_set_image(mem->meta, mem->image);
    return TRUE;
}

static GstVaapiSurface *
new_surface(GstVaapiDisplay *display, const GstVideoInfo *vip)
{
    if (GST_VIDEO_INFO_FORMAT(vip) != GST_VIDEO_FORMAT_NV12)
        return NULL;

    return gst_vaapi_surface_new(display, GST_VAAPI_CHROMA_TYPE_YUV420,
        GST_VIDEO_INFO_WIDTH(vip), GST_VIDEO_INFO_HEIGHT(vip));
}

static gboolean
ensure_surface(GstVaapiVideoMemory *mem)
{
    if (!mem->surface) {
        GstVaapiDisplay * const display =
            gst_vaapi_video_meta_get_display(mem->meta);

        mem->surface = new_surface(display, mem->surface_info);
        if (!mem->surface)
            return FALSE;
    }
    gst_vaapi_video_meta_set_surface(mem->meta, mem->surface);
    return TRUE;
}

gboolean
gst_video_meta_map_vaapi_memory(GstVideoMeta *meta, guint plane,
    GstMapInfo *info, gpointer *data, gint *stride, GstMapFlags flags)
{
    GstVaapiVideoMemory * const mem =
        GST_VAAPI_VIDEO_MEMORY_CAST(gst_buffer_get_memory(meta->buffer, 0));

    g_return_val_if_fail(mem, FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_ALLOCATOR(mem->parent_instance.
                             allocator), FALSE);
    g_return_val_if_fail(mem->meta, FALSE);

    if ((flags & GST_MAP_READWRITE) == GST_MAP_READ)
        goto error_unsupported_map;

    if (++mem->map_count == 1) {
        if (!ensure_surface(mem))
            goto error_ensure_surface;
        if (!ensure_image(mem))
            goto error_ensure_image;
        if (!gst_vaapi_image_map(mem->image))
            goto error_map_image;
    }

    *data = gst_vaapi_image_get_plane(mem->image, plane);
    *stride = gst_vaapi_image_get_pitch(mem->image, plane);
    info->flags = flags;
    return TRUE;

    /* ERRORS */
error_unsupported_map:
    {
        GST_ERROR("unsupported map flags (0x%x)", flags);
        return FALSE;
    }
error_ensure_surface:
    {
        const GstVideoInfo * const vip = mem->surface_info;
        GST_ERROR("failed to create %s surface of size %ux%u",
                  GST_VIDEO_INFO_FORMAT_STRING(vip),
                  GST_VIDEO_INFO_WIDTH(vip), GST_VIDEO_INFO_HEIGHT(vip));
        return FALSE;
    }
error_ensure_image:
    {
        const GstVideoInfo * const vip = mem->image_info;
        GST_ERROR("failed to create %s image of size %ux%u",
                  GST_VIDEO_INFO_FORMAT_STRING(vip),
                  GST_VIDEO_INFO_WIDTH(vip), GST_VIDEO_INFO_HEIGHT(vip));
        return FALSE;
    }
error_map_image:
    {
        GST_ERROR("failed to map image %" GST_VAAPI_ID_FORMAT,
                  GST_VAAPI_ID_ARGS(gst_vaapi_image_get_id(mem->image)));
        return FALSE;
    }
}

gboolean
gst_video_meta_unmap_vaapi_memory(GstVideoMeta *meta, guint plane,
    GstMapInfo *info)
{
    GstVaapiVideoMemory * const mem =
        GST_VAAPI_VIDEO_MEMORY_CAST(gst_buffer_get_memory(meta->buffer, 0));

    g_return_val_if_fail(mem, FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_ALLOCATOR(mem->parent_instance.
                             allocator), FALSE);
    g_return_val_if_fail(mem->meta, FALSE);
    g_return_val_if_fail(mem->surface, FALSE);
    g_return_val_if_fail(mem->image, FALSE);

    if (--mem->map_count == 0) {
        gst_vaapi_image_unmap(mem->image);

        /* Commit VA image to surface */
        if ((info->flags & GST_MAP_WRITE) && !mem->use_direct_rendering) {
            if (!gst_vaapi_surface_put_image(mem->surface, mem->image))
                goto error_upload_image;
        }
    }
    return TRUE;

    /* ERRORS */
error_upload_image:
    {
        GST_ERROR("failed to upload image");
        return FALSE;
    }
}

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

    vip = &allocator->image_info;
    gst_memory_init(&mem->parent_instance, 0, base_allocator, NULL,
        GST_VIDEO_INFO_SIZE(vip), 0, 0, GST_VIDEO_INFO_SIZE(vip));

    mem->surface_info = &allocator->surface_info;
    mem->surface = NULL;
    mem->image_info = &allocator->image_info;
    mem->image = NULL;
    mem->meta = gst_vaapi_video_meta_ref(meta);
    mem->map_count = 0;
    mem->use_direct_rendering = allocator->has_direct_rendering;
    return GST_MEMORY_CAST(mem);
}

static void
gst_vaapi_video_memory_free(GstVaapiVideoMemory *mem)
{
    g_clear_object(&mem->surface);
    g_clear_object(&mem->image);
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

static gboolean
gst_video_info_update_from_image(GstVideoInfo *vip, GstVaapiImage *image)
{
    const guchar *data;
    guint i, num_planes, data_size;

    num_planes = gst_vaapi_image_get_plane_count(image);
    g_return_val_if_fail(num_planes == GST_VIDEO_INFO_N_PLANES(vip), FALSE);

    /* Determine the base data pointer */
    data = gst_vaapi_image_get_plane(image, 0);
    for (i = 1; i < num_planes; i++) {
        const guchar * const plane = gst_vaapi_image_get_plane(image, i);
        if (data > plane)
            data = plane;
    }
    data_size = gst_vaapi_image_get_data_size(image);

    /* Check that we don't have disjoint planes */
    for (i = 0; i < num_planes; i++) {
        const guchar * const plane = gst_vaapi_image_get_plane(image, i);
        if (plane - data > data_size)
            return FALSE;
    }

    /* Update GstVideoInfo structure */
    for (i = 0; i < num_planes; i++) {
        const guchar * const plane = gst_vaapi_image_get_plane(image, i);
        GST_VIDEO_INFO_PLANE_OFFSET(vip, i) = plane - data;
        GST_VIDEO_INFO_PLANE_STRIDE(vip, i) =
            gst_vaapi_image_get_pitch(image, i);
    }
    GST_VIDEO_INFO_SIZE(vip) = data_size;
    return TRUE;
}

GstAllocator *
gst_vaapi_video_allocator_new(GstVaapiDisplay *display, GstCaps *caps)
{
    GstVaapiVideoAllocator *allocator;
    GstVideoInfo *vip;
    GstVaapiSurface *surface;
    GstVaapiImage *image;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    allocator = g_object_new(GST_VAAPI_TYPE_VIDEO_ALLOCATOR, NULL);
    if (!allocator)
        return NULL;

    vip = &allocator->video_info;
    gst_video_info_init(vip);
    gst_video_info_from_caps(vip, caps);

    gst_video_info_set_format(&allocator->surface_info, GST_VIDEO_FORMAT_NV12,
        GST_VIDEO_INFO_WIDTH(vip), GST_VIDEO_INFO_HEIGHT(vip));

    if (GST_VIDEO_INFO_FORMAT(vip) != GST_VIDEO_FORMAT_ENCODED) {
        image = NULL;
        do {
            surface = new_surface(display, vip);
            if (!surface)
                break;
            image = gst_vaapi_surface_derive_image(surface);
            if (!image)
                break;
            if (!gst_vaapi_image_map(image))
                break;
            allocator->has_direct_rendering = gst_video_info_update_from_image(
                &allocator->surface_info, image);
            gst_vaapi_image_unmap(image);
            GST_INFO("has direct-rendering for %s surfaces: %s",
                     GST_VIDEO_INFO_FORMAT_STRING(&allocator->surface_info),
                     allocator->has_direct_rendering ? "yes" : "no");
        } while (0);
        g_clear_object(&surface);
        g_clear_object(&image);
    }

    allocator->image_info = *vip;
    if (GST_VIDEO_INFO_FORMAT(vip) == GST_VIDEO_FORMAT_ENCODED)
        gst_video_info_set_format(&allocator->image_info, GST_VIDEO_FORMAT_NV12,
            GST_VIDEO_INFO_WIDTH(vip), GST_VIDEO_INFO_HEIGHT(vip));

    if (allocator->has_direct_rendering)
        allocator->image_info = allocator->surface_info;
    else {
        do {
            image = new_image(display, &allocator->image_info);
            if (!image)
                break;
            if (!gst_vaapi_image_map(image))
                break;
            gst_video_info_update_from_image(&allocator->image_info, image);
            gst_vaapi_image_unmap(image);
        } while (0);
        g_clear_object(&image);
    }
    return GST_ALLOCATOR_CAST(allocator);
}
