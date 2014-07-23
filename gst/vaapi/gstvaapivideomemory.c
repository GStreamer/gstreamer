/*
 *  gstvaapivideomemory.c - Gstreamer/VA video memory
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

static guchar *
get_image_data(GstVaapiImage *image)
{
    guchar *data;
    VAImage va_image;

    data = gst_vaapi_image_get_plane(image, 0);
    if (!data || !gst_vaapi_image_get_image(image, &va_image))
        return NULL;

    data -= va_image.offsets[0];
    return data;
}

static GstVaapiImage *
new_image(GstVaapiDisplay *display, const GstVideoInfo *vip)
{
    return gst_vaapi_image_new(display, GST_VIDEO_INFO_FORMAT(vip),
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
        else if (gst_vaapi_surface_get_format(mem->surface) !=
                 GST_VIDEO_INFO_FORMAT(mem->image_info)) {
            gst_vaapi_object_replace(&mem->image, NULL);
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
    GstVaapiSurface *surface;
    GstVaapiChromaType chroma_type;

    /* Try with explicit format first */
    if (GST_VIDEO_INFO_FORMAT(vip) != GST_VIDEO_FORMAT_ENCODED) {
        surface = gst_vaapi_surface_new_with_format(display,
            GST_VIDEO_INFO_FORMAT(vip), GST_VIDEO_INFO_WIDTH(vip),
            GST_VIDEO_INFO_HEIGHT(vip));
        if (surface)
            return surface;
    }

    /* Try to pick something compatible, i.e. with same chroma type */
    chroma_type = gst_vaapi_video_format_get_chroma_type(
        GST_VIDEO_INFO_FORMAT(vip));
    if (!chroma_type)
        return NULL;
    return gst_vaapi_surface_new(display, chroma_type,
        GST_VIDEO_INFO_WIDTH(vip), GST_VIDEO_INFO_HEIGHT(vip));
}

static GstVaapiSurfaceProxy *
new_surface_proxy(GstVaapiVideoMemory *mem)
{
    GstVaapiVideoAllocator * const allocator =
        GST_VAAPI_VIDEO_ALLOCATOR_CAST(GST_MEMORY_CAST(mem)->allocator);

    return gst_vaapi_surface_proxy_new_from_pool(
        GST_VAAPI_SURFACE_POOL(allocator->surface_pool));
}

static gboolean
ensure_surface(GstVaapiVideoMemory *mem)
{
    if (!mem->proxy) {
        gst_vaapi_surface_proxy_replace(&mem->proxy,
            gst_vaapi_video_meta_get_surface_proxy(mem->meta));

        if (!mem->proxy) {
            mem->proxy = new_surface_proxy(mem);
            if (!mem->proxy)
                return FALSE;
            gst_vaapi_video_meta_set_surface_proxy(mem->meta, mem->proxy);
        }
    }
    mem->surface = GST_VAAPI_SURFACE_PROXY_SURFACE(mem->proxy);
    return mem->surface != NULL;
}

gboolean
gst_video_meta_map_vaapi_memory(GstVideoMeta *meta, guint plane,
    GstMapInfo *info, gpointer *data, gint *stride, GstMapFlags flags)
{
    GstVaapiVideoMemory * const mem =
        GST_VAAPI_VIDEO_MEMORY_CAST(gst_buffer_peek_memory(meta->buffer, 0));

    g_return_val_if_fail(mem, FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_ALLOCATOR(mem->parent_instance.
                             allocator), FALSE);
    g_return_val_if_fail(mem->meta, FALSE);

    if (mem->map_type &&
        mem->map_type != GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_PLANAR)
        goto error_incompatible_map;

    /* Map for writing */
    if (++mem->map_count == 1) {
        if (!ensure_surface(mem))
            goto error_ensure_surface;
        if (!ensure_image(mem))
            goto error_ensure_image;

        // Check that we can actually map the surface, or image
        if ((flags & GST_MAP_READWRITE) == GST_MAP_WRITE &&
            !mem->use_direct_rendering)
            goto error_unsupported_map;

        // Load VA image from surface
        if ((flags & GST_MAP_READ) && !mem->use_direct_rendering)
            gst_vaapi_surface_get_image(mem->surface, mem->image);

        if (!gst_vaapi_image_map(mem->image))
            goto error_map_image;
        mem->map_type = GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_PLANAR;
    }

    *data = gst_vaapi_image_get_plane(mem->image, plane);
    *stride = gst_vaapi_image_get_pitch(mem->image, plane);
    info->flags = flags;
    return TRUE;

    /* ERRORS */
error_incompatible_map:
    {
        GST_ERROR("incompatible map type (%d)", mem->map_type);
        return FALSE;
    }
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
        GST_VAAPI_VIDEO_MEMORY_CAST(gst_buffer_peek_memory(meta->buffer, 0));

    g_return_val_if_fail(mem, FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_ALLOCATOR(mem->parent_instance.
                             allocator), FALSE);
    g_return_val_if_fail(mem->meta, FALSE);
    g_return_val_if_fail(mem->surface, FALSE);
    g_return_val_if_fail(mem->image, FALSE);

    if (--mem->map_count == 0) {
        mem->map_type = 0;

        /* Unmap VA image used for read/writes */
        if (info->flags & GST_MAP_READWRITE)
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
    gst_memory_init(&mem->parent_instance, 0, gst_object_ref(allocator), NULL,
        GST_VIDEO_INFO_SIZE(vip), 0, 0, GST_VIDEO_INFO_SIZE(vip));

    mem->proxy = NULL;
    mem->surface_info = &allocator->surface_info;
    mem->surface = NULL;
    mem->image_info = &allocator->image_info;
    mem->image = NULL;
    mem->meta = gst_vaapi_video_meta_ref(meta);
    mem->map_type = 0;
    mem->map_count = 0;
    mem->use_direct_rendering = allocator->has_direct_rendering;
    return GST_MEMORY_CAST(mem);
}

static void
gst_vaapi_video_memory_free(GstVaapiVideoMemory *mem)
{
    mem->surface = NULL;
    gst_vaapi_surface_proxy_replace(&mem->proxy, NULL);
    gst_vaapi_object_replace(&mem->image, NULL);
    gst_vaapi_video_meta_unref(mem->meta);
    gst_object_unref(GST_MEMORY_CAST(mem)->allocator);
    g_slice_free(GstVaapiVideoMemory, mem);
}

void
gst_vaapi_video_memory_reset_surface(GstVaapiVideoMemory *mem)
{
    mem->surface = NULL;
    gst_vaapi_surface_proxy_replace(&mem->proxy, NULL);
    if (mem->use_direct_rendering)
        gst_vaapi_object_replace(&mem->image, NULL);
    gst_vaapi_video_meta_set_surface_proxy(mem->meta, NULL);
}

static gpointer
gst_vaapi_video_memory_map(GstVaapiVideoMemory *mem, gsize maxsize, guint flags)
{
    gpointer data;

    if (mem->map_count == 0) {
        switch (flags & GST_MAP_READWRITE) {
        case 0:
            // No flags set: return a GstVaapiSurfaceProxy
            gst_vaapi_surface_proxy_replace(&mem->proxy,
                gst_vaapi_video_meta_get_surface_proxy(mem->meta));
            if (!mem->proxy)
                goto error_no_surface_proxy;
            mem->map_type = GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_SURFACE;
            break;
        case GST_MAP_READ:
            // Only read flag set: return raw pixels
            if (!ensure_surface(mem))
                return NULL;
            if (!ensure_image(mem))
                goto error_ensure_image;
            if (!mem->use_direct_rendering)
                gst_vaapi_surface_get_image(mem->surface, mem->image);
            if (!gst_vaapi_image_map(mem->image))
                goto error_map_image;
            mem->map_type = GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_LINEAR;
            break;
        default:
            goto error_unsupported_map;
        }
    }

    switch (mem->map_type) {
    case GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_SURFACE:
        if (!mem->proxy)
            goto error_no_surface_proxy;
        data = mem->proxy;
        break;
    case GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_LINEAR:
        if (!mem->image)
            goto error_no_image;
        data = get_image_data(mem->image);
        break;
    default:
        goto error_unsupported_map_type;
    }
    mem->map_count++;
    return data;

    /* ERRORS */
error_unsupported_map:
    GST_ERROR("unsupported map flags (0x%x)", flags);
    return NULL;
error_unsupported_map_type:
    GST_ERROR("unsupported map type (%d)", mem->map_type);
    return NULL;
error_no_surface_proxy:
    GST_ERROR("failed to extract GstVaapiSurfaceProxy from video meta");
    return NULL;
error_no_image:
    GST_ERROR("failed to extract raw pixels from mapped VA image");
    return NULL;
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

static void
gst_vaapi_video_memory_unmap(GstVaapiVideoMemory *mem)
{
    if (mem->map_count == 1) {
        switch (mem->map_type) {
        case GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_SURFACE:
            gst_vaapi_surface_proxy_replace(&mem->proxy, NULL);
            break;
        case GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_LINEAR:
            gst_vaapi_image_unmap(mem->image);
            break;
        default:
            goto error_incompatible_map;
        }
        mem->map_type = 0;
    }
    mem->map_count--;
    return;

    /* ERRORS */
error_incompatible_map:
    GST_ERROR("incompatible map type (%d)", mem->map_type);
    return;
}

static GstVaapiVideoMemory *
gst_vaapi_video_memory_copy(GstVaapiVideoMemory *mem,
    gssize offset, gssize size)
{
    GstMemory *out_mem;

    if (offset != 0 || size != -1)
        goto error_unsupported;

    out_mem = gst_vaapi_video_memory_new(mem->parent_instance.allocator,
        mem->meta);
    if (!out_mem)
        goto error_allocate_memory;
    return GST_VAAPI_VIDEO_MEMORY_CAST(out_mem);

    /* ERRORS */
error_unsupported:
    GST_ERROR("failed to copy partial memory (unsupported operation)");
    return NULL;
error_allocate_memory:
    GST_ERROR("failed to allocate GstVaapiVideoMemory copy");
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
gst_vaapi_video_allocator_finalize(GObject *object)
{
    GstVaapiVideoAllocator * const allocator =
        GST_VAAPI_VIDEO_ALLOCATOR_CAST(object);

    gst_vaapi_video_pool_replace(&allocator->surface_pool, NULL);

    G_OBJECT_CLASS(gst_vaapi_video_allocator_parent_class)->finalize(object);
}

static void
gst_vaapi_video_allocator_class_init(GstVaapiVideoAllocatorClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstAllocatorClass * const allocator_class = GST_ALLOCATOR_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapivideomemory,
        "vaapivideomemory", 0, "VA-API video memory allocator");

    object_class->finalize      = gst_vaapi_video_allocator_finalize;
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
    GstVideoFormat format;
    const guchar *data;
    guint i, num_planes, data_size, width, height;

    /* Reset format from image */
    format = gst_vaapi_image_get_format(image);
    gst_vaapi_image_get_size(image, &width, &height);
    gst_video_info_set_format(vip, format, width, height);

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
gst_vaapi_video_allocator_new(GstVaapiDisplay *display, const GstVideoInfo *vip)
{
    GstVaapiVideoAllocator *allocator;
    GstVaapiSurface *surface;
    GstVaapiImage *image;

    g_return_val_if_fail(display != NULL, NULL);
    g_return_val_if_fail(vip != NULL, NULL);

    allocator = g_object_new(GST_VAAPI_TYPE_VIDEO_ALLOCATOR, NULL);
    if (!allocator)
        return NULL;

    allocator->video_info = *vip;
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
            if (GST_VAAPI_IMAGE_FORMAT(image) != GST_VIDEO_INFO_FORMAT(vip))
               allocator->has_direct_rendering = FALSE;
            gst_vaapi_image_unmap(image);
            GST_INFO("has direct-rendering for %s surfaces: %s",
                     GST_VIDEO_INFO_FORMAT_STRING(&allocator->surface_info),
                     allocator->has_direct_rendering ? "yes" : "no");
        } while (0);
        if (surface)
            gst_vaapi_object_unref(surface);
        if (image)
            gst_vaapi_object_unref(image);
    }

    allocator->surface_pool = gst_vaapi_surface_pool_new(display,
        &allocator->surface_info);
    if (!allocator->surface_pool)
        goto error_create_pool;

    allocator->image_info = *vip;
    if (GST_VIDEO_INFO_FORMAT(vip) == GST_VIDEO_FORMAT_ENCODED)
        gst_video_info_set_format(&allocator->image_info, GST_VIDEO_FORMAT_I420,
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
        gst_vaapi_object_unref(image);
    }
    return GST_ALLOCATOR_CAST(allocator);

    /* ERRORS */
error_create_pool:
    {
        GST_ERROR("failed to allocate VA surface pool");
        gst_object_unref(allocator);
        return NULL;
    }
}
