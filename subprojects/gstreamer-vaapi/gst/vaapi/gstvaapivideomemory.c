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

#include "gstcompat.h"
#include <unistd.h>
#include <gst/vaapi/gstvaapisurface_drm.h>
#include <gst/vaapi/gstvaapisurfacepool.h>
#include <gst/vaapi/gstvaapiimagepool.h>
#include "gstvaapivideomemory.h"
#include "gstvaapipluginutil.h"

GST_DEBUG_CATEGORY_STATIC (CAT_PERFORMANCE);
GST_DEBUG_CATEGORY_STATIC (gst_debug_vaapivideomemory);
#define GST_CAT_DEFAULT gst_debug_vaapivideomemory

#ifndef GST_VIDEO_INFO_FORMAT_STRING
#define GST_VIDEO_INFO_FORMAT_STRING(vip) \
  gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (vip))
#endif

/* ------------------------------------------------------------------------ */
/* --- GstVaapiVideoMemory                                              --- */
/* ------------------------------------------------------------------------ */

static void gst_vaapi_video_memory_reset_image (GstVaapiVideoMemory * mem);

static void
_init_performance_debug (void)
{
#ifndef GST_DISABLE_GST_DEBUG
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_GET (CAT_PERFORMANCE, "GST_PERFORMANCE");
    g_once_init_leave (&_init, 1);
  }
#endif
}

static void
_init_vaapi_video_memory_debug (void)
{
#ifndef GST_DISABLE_GST_DEBUG
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_debug_vaapivideomemory, "vaapivideomemory", 0,
        "VA-API video memory allocator");
    g_once_init_leave (&_init, 1);
  }
#endif
}

static inline void
reset_image_usage (GstVaapiImageUsageFlags * flag)
{
  _init_performance_debug ();
  GST_CAT_INFO (CAT_PERFORMANCE, "derive image failed, fallbacking to copy");
  *flag = GST_VAAPI_IMAGE_USAGE_FLAG_NATIVE_FORMATS;
}

static inline gboolean
use_native_formats (GstVaapiImageUsageFlags flag)
{
  return flag == GST_VAAPI_IMAGE_USAGE_FLAG_NATIVE_FORMATS;
}

static inline gboolean
use_direct_rendering (GstVaapiImageUsageFlags flag)
{
  return flag == GST_VAAPI_IMAGE_USAGE_FLAG_DIRECT_RENDER;
}

static inline gboolean
use_direct_uploading (GstVaapiImageUsageFlags flag)
{
  return flag == GST_VAAPI_IMAGE_USAGE_FLAG_DIRECT_UPLOAD;
}

static guchar *
get_image_data (GstVaapiImage * image)
{
  guchar *data;
  VAImage va_image;

  data = gst_vaapi_image_get_plane (image, 0);
  if (!data || !gst_vaapi_image_get_image (image, &va_image))
    return NULL;

  data -= va_image.offsets[0];
  return data;
}

static GstVaapiImage *
new_image (GstVaapiDisplay * display, const GstVideoInfo * vip)
{
  if (!GST_VIDEO_INFO_WIDTH (vip) || !GST_VIDEO_INFO_HEIGHT (vip))
    return NULL;
  return gst_vaapi_image_new (display, GST_VIDEO_INFO_FORMAT (vip),
      GST_VIDEO_INFO_WIDTH (vip), GST_VIDEO_INFO_HEIGHT (vip));
}

static gboolean
ensure_image (GstVaapiVideoMemory * mem)
{
  if (!mem->image && !use_native_formats (mem->usage_flag)) {
    mem->image = gst_vaapi_surface_derive_image (mem->surface);
    if (!mem->image) {
      reset_image_usage (&mem->usage_flag);
    } else if (gst_vaapi_surface_get_format (mem->surface) !=
        GST_VIDEO_INFO_FORMAT (mem->image_info)) {
      gst_mini_object_replace ((GstMiniObject **) & mem->image, NULL);
      reset_image_usage (&mem->usage_flag);
    }
  }

  if (!mem->image) {
    GstVaapiVideoAllocator *const allocator =
        GST_VAAPI_VIDEO_ALLOCATOR_CAST (GST_MEMORY_CAST (mem)->allocator);

    mem->image = gst_vaapi_video_pool_get_object (allocator->image_pool);
    if (!mem->image)
      return FALSE;
  }
  gst_vaapi_video_meta_set_image (mem->meta, mem->image);
  return TRUE;
}

static gboolean
ensure_image_is_current (GstVaapiVideoMemory * mem)
{
  if (!use_native_formats (mem->usage_flag))
    return TRUE;

  if (!GST_VAAPI_VIDEO_MEMORY_FLAG_IS_SET (mem,
          GST_VAAPI_VIDEO_MEMORY_FLAG_IMAGE_IS_CURRENT)) {
    if (!gst_vaapi_surface_get_image (mem->surface, mem->image))
      return FALSE;

    GST_VAAPI_VIDEO_MEMORY_FLAG_SET (mem,
        GST_VAAPI_VIDEO_MEMORY_FLAG_IMAGE_IS_CURRENT);
  }
  return TRUE;
}

static GstVaapiSurfaceProxy *
new_surface_proxy (GstVaapiVideoMemory * mem)
{
  GstVaapiVideoAllocator *const allocator =
      GST_VAAPI_VIDEO_ALLOCATOR_CAST (GST_MEMORY_CAST (mem)->allocator);

  return
      gst_vaapi_surface_proxy_new_from_pool (GST_VAAPI_SURFACE_POOL
      (allocator->surface_pool));
}

static gboolean
ensure_surface (GstVaapiVideoMemory * mem)
{
  if (!mem->proxy) {
    gst_vaapi_surface_proxy_replace (&mem->proxy,
        gst_vaapi_video_meta_get_surface_proxy (mem->meta));

    if (!mem->proxy) {
      mem->proxy = new_surface_proxy (mem);
      if (!mem->proxy)
        return FALSE;
      gst_vaapi_video_meta_set_surface_proxy (mem->meta, mem->proxy);
    }
  }
  mem->surface = GST_VAAPI_SURFACE_PROXY_SURFACE (mem->proxy);
  return mem->surface != NULL;
}

static gboolean
ensure_surface_is_current (GstVaapiVideoMemory * mem)
{
  if (!use_native_formats (mem->usage_flag))
    return TRUE;

  if (!GST_VAAPI_VIDEO_MEMORY_FLAG_IS_SET (mem,
          GST_VAAPI_VIDEO_MEMORY_FLAG_SURFACE_IS_CURRENT)) {
    if (GST_VAAPI_VIDEO_MEMORY_FLAG_IS_SET (mem,
            GST_VAAPI_VIDEO_MEMORY_FLAG_IMAGE_IS_CURRENT)
        && !gst_vaapi_surface_put_image (mem->surface, mem->image))
      return FALSE;

    GST_VAAPI_VIDEO_MEMORY_FLAG_SET (mem,
        GST_VAAPI_VIDEO_MEMORY_FLAG_SURFACE_IS_CURRENT);
  }
  return TRUE;
}

static inline gboolean
map_vaapi_memory (GstVaapiVideoMemory * mem, GstMapFlags flags)
{
  if (!ensure_surface (mem))
    goto error_no_surface;
  if (!ensure_image (mem))
    goto error_no_image;

  /* Load VA image from surface only for read flag since it returns
   * raw pixels */
  if ((flags & GST_MAP_READ) && !ensure_image_is_current (mem))
    goto error_no_current_image;

  if (!gst_vaapi_image_map (mem->image))
    goto error_map_image;

  /* Mark surface as dirty and expect updates from image */
  if (flags & GST_MAP_WRITE)
    GST_VAAPI_VIDEO_MEMORY_FLAG_UNSET (mem,
        GST_VAAPI_VIDEO_MEMORY_FLAG_SURFACE_IS_CURRENT);

  return TRUE;

error_no_surface:
  {
    const GstVideoInfo *const vip = mem->surface_info;
    GST_ERROR ("failed to extract VA surface of size %ux%u and format %s",
        GST_VIDEO_INFO_WIDTH (vip), GST_VIDEO_INFO_HEIGHT (vip),
        GST_VIDEO_INFO_FORMAT_STRING (vip));
    return FALSE;
  }
error_no_image:
  {
    const GstVideoInfo *const vip = mem->image_info;
    GST_ERROR ("failed to extract VA image of size %ux%u and format %s",
        GST_VIDEO_INFO_WIDTH (vip), GST_VIDEO_INFO_HEIGHT (vip),
        GST_VIDEO_INFO_FORMAT_STRING (vip));
    return FALSE;
  }
error_no_current_image:
  {
    GST_ERROR ("failed to make image current");
    return FALSE;
  }
error_map_image:
  {
    GST_ERROR ("failed to map image %" GST_VAAPI_ID_FORMAT,
        GST_VAAPI_ID_ARGS (gst_vaapi_image_get_id (mem->image)));
    return FALSE;
  }
}

static inline void
unmap_vaapi_memory (GstVaapiVideoMemory * mem, GstMapFlags flags)
{
  gst_vaapi_image_unmap (mem->image);

  if (flags & GST_MAP_WRITE) {
    GST_VAAPI_VIDEO_MEMORY_FLAG_SET (mem,
        GST_VAAPI_VIDEO_MEMORY_FLAG_IMAGE_IS_CURRENT);
  }

  if (!use_native_formats (mem->usage_flag)) {
    gst_vaapi_video_meta_set_image (mem->meta, NULL);
    gst_vaapi_video_memory_reset_image (mem);
  }
}

gboolean
gst_video_meta_map_vaapi_memory (GstVideoMeta * meta, guint plane,
    GstMapInfo * info, gpointer * data, gint * stride, GstMapFlags flags)
{
  gboolean ret = FALSE;
  GstAllocator *allocator;
  GstVaapiVideoMemory *const mem =
      GST_VAAPI_VIDEO_MEMORY_CAST (gst_buffer_peek_memory (meta->buffer, 0));

  g_return_val_if_fail (mem, FALSE);
  g_return_val_if_fail (mem->meta, FALSE);

  allocator = GST_MEMORY_CAST (mem)->allocator;
  g_return_val_if_fail (GST_VAAPI_IS_VIDEO_ALLOCATOR (allocator), FALSE);

  g_mutex_lock (&mem->lock);
  if (mem->map_type && mem->map_type != GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_PLANAR)
    goto error_incompatible_map;

  /* Map for writing */
  if (mem->map_count == 0) {
    if (!map_vaapi_memory (mem, flags))
      goto out;
    mem->map_type = GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_PLANAR;
  }
  mem->map_count++;

  *data = gst_vaapi_image_get_plane (mem->image, plane);
  *stride = gst_vaapi_image_get_pitch (mem->image, plane);
  info->flags = flags;
  ret = (*data != NULL);

out:
  g_mutex_unlock (&mem->lock);
  return ret;

  /* ERRORS */
error_incompatible_map:
  {
    GST_ERROR ("incompatible map type (%d)", mem->map_type);
    goto out;
  }
}

gboolean
gst_video_meta_unmap_vaapi_memory (GstVideoMeta * meta, guint plane,
    GstMapInfo * info)
{
  GstAllocator *allocator;
  GstVaapiVideoMemory *const mem =
      GST_VAAPI_VIDEO_MEMORY_CAST (gst_buffer_peek_memory (meta->buffer, 0));

  g_return_val_if_fail (mem, FALSE);
  g_return_val_if_fail (mem->meta, FALSE);
  g_return_val_if_fail (mem->surface, FALSE);
  g_return_val_if_fail (mem->image, FALSE);

  allocator = GST_MEMORY_CAST (mem)->allocator;
  g_return_val_if_fail (GST_VAAPI_IS_VIDEO_ALLOCATOR (allocator), FALSE);

  g_mutex_lock (&mem->lock);
  if (--mem->map_count == 0) {
    mem->map_type = 0;

    /* Unmap VA image used for read/writes */
    if (info->flags & GST_MAP_READWRITE)
      unmap_vaapi_memory (mem, info->flags);
  }
  g_mutex_unlock (&mem->lock);
  return TRUE;
}

GstMemory *
gst_vaapi_video_memory_new (GstAllocator * base_allocator,
    GstVaapiVideoMeta * meta)
{
  GstVaapiVideoAllocator *const allocator =
      GST_VAAPI_VIDEO_ALLOCATOR_CAST (base_allocator);
  const GstVideoInfo *vip;
  GstVaapiVideoMemory *mem;

  g_return_val_if_fail (GST_VAAPI_IS_VIDEO_ALLOCATOR (allocator), NULL);

  mem = g_new (GstVaapiVideoMemory, 1);
  if (!mem)
    return NULL;

  vip = &allocator->image_info;
  gst_memory_init (&mem->parent_instance, GST_MEMORY_FLAG_NO_SHARE,
      base_allocator, NULL, GST_VIDEO_INFO_SIZE (vip), 0,
      0, GST_VIDEO_INFO_SIZE (vip));

  mem->proxy = NULL;
  mem->surface_info = &allocator->surface_info;
  mem->surface = NULL;
  mem->image_info = &allocator->image_info;
  mem->image = NULL;
  mem->meta = meta ? gst_vaapi_video_meta_ref (meta) : NULL;
  mem->map_type = 0;
  mem->map_count = 0;
  mem->map_surface_id = VA_INVALID_ID;
  mem->usage_flag = allocator->usage_flag;
  g_mutex_init (&mem->lock);

  GST_VAAPI_VIDEO_MEMORY_FLAG_SET (mem,
      GST_VAAPI_VIDEO_MEMORY_FLAG_SURFACE_IS_CURRENT);
  return GST_MEMORY_CAST (mem);
}

void
gst_vaapi_video_memory_reset_image (GstVaapiVideoMemory * mem)
{
  GstVaapiVideoAllocator *const allocator =
      GST_VAAPI_VIDEO_ALLOCATOR_CAST (GST_MEMORY_CAST (mem)->allocator);

  if (!use_native_formats (mem->usage_flag))
    gst_mini_object_replace ((GstMiniObject **) & mem->image, NULL);
  else if (mem->image) {
    gst_vaapi_video_pool_put_object (allocator->image_pool, mem->image);
    mem->image = NULL;
  }

  /* Don't synchronize to surface, this shall have happened during
   * unmaps */
  GST_VAAPI_VIDEO_MEMORY_FLAG_UNSET (mem,
      GST_VAAPI_VIDEO_MEMORY_FLAG_IMAGE_IS_CURRENT);
}

void
gst_vaapi_video_memory_reset_surface (GstVaapiVideoMemory * mem)
{
  mem->surface = NULL;
  gst_vaapi_video_memory_reset_image (mem);
  gst_vaapi_surface_proxy_replace (&mem->proxy, NULL);
  if (mem->meta)
    gst_vaapi_video_meta_set_surface_proxy (mem->meta, NULL);

  GST_VAAPI_VIDEO_MEMORY_FLAG_UNSET (mem,
      GST_VAAPI_VIDEO_MEMORY_FLAG_SURFACE_IS_CURRENT);
}

gboolean
gst_vaapi_video_memory_sync (GstVaapiVideoMemory * mem)
{
  g_return_val_if_fail (mem, FALSE);

  return ensure_surface_is_current (mem);
}

static gpointer
gst_vaapi_video_memory_map (GstMemory * base_mem, gsize maxsize, guint flags)
{
  gpointer data = NULL;
  GstVaapiVideoMemory *const mem = GST_VAAPI_VIDEO_MEMORY_CAST (base_mem);

  g_return_val_if_fail (mem, NULL);
  g_return_val_if_fail (mem->meta, NULL);

  g_mutex_lock (&mem->lock);
  if (mem->map_count == 0) {
    switch (flags & (GST_MAP_READWRITE | GST_MAP_VAAPI)) {
      case 0:
      case GST_MAP_VAAPI:
        // No flags set: return a GstVaapiSurfaceProxy
        gst_vaapi_surface_proxy_replace (&mem->proxy,
            gst_vaapi_video_meta_get_surface_proxy (mem->meta));
        if (!mem->proxy)
          goto error_no_surface_proxy;
        if (!ensure_surface_is_current (mem))
          goto error_no_current_surface;
        mem->map_type = GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_SURFACE;
        break;
      case GST_MAP_READ:
        if (!map_vaapi_memory (mem, flags))
          goto out;
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

      if (flags == GST_MAP_VAAPI) {
        mem->map_surface_id = GST_VAAPI_SURFACE_PROXY_SURFACE_ID (mem->proxy);
        if (mem->map_surface_id == VA_INVALID_ID)
          goto error_no_current_surface;

        data = &mem->map_surface_id;
      } else {
        data = mem->proxy;
      }
      break;
    case GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_LINEAR:
      if (!mem->image)
        goto error_no_image;
      data = get_image_data (mem->image);
      break;
    default:
      goto error_unsupported_map_type;
  }
  mem->map_count++;

out:
  g_mutex_unlock (&mem->lock);
  return data;

  /* ERRORS */
error_unsupported_map:
  {
    GST_ERROR ("unsupported map flags (0x%x)", flags);
    goto out;
  }
error_unsupported_map_type:
  {
    GST_ERROR ("unsupported map type (%d)", mem->map_type);
    goto out;
  }
error_no_surface_proxy:
  {
    GST_ERROR ("failed to extract GstVaapiSurfaceProxy from video meta");
    goto out;
  }
error_no_current_surface:
  {
    GST_ERROR ("failed to make surface current");
    goto out;
  }
error_no_image:
  {
    GST_ERROR ("failed to extract VA image from video buffer");
    goto out;
  }
}

static void
gst_vaapi_video_memory_unmap_full (GstMemory * base_mem, GstMapInfo * info)
{
  GstVaapiVideoMemory *const mem = GST_VAAPI_VIDEO_MEMORY_CAST (base_mem);

  g_mutex_lock (&mem->lock);
  if (mem->map_count == 1) {
    switch (mem->map_type) {
      case GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_SURFACE:
        mem->map_surface_id = VA_INVALID_ID;
        gst_vaapi_surface_proxy_replace (&mem->proxy, NULL);
        break;
      case GST_VAAPI_VIDEO_MEMORY_MAP_TYPE_LINEAR:
        unmap_vaapi_memory (mem, info->flags);
        break;
      default:
        goto error_incompatible_map;
    }
    mem->map_type = 0;
  }
  mem->map_count--;

out:
  g_mutex_unlock (&mem->lock);
  return;

  /* ERRORS */
error_incompatible_map:
  {
    GST_ERROR ("incompatible map type (%d)", mem->map_type);
    goto out;
  }
}

static GstMemory *
gst_vaapi_video_memory_copy (GstMemory * base_mem, gssize offset, gssize size)
{
  GstVaapiVideoMemory *const mem = GST_VAAPI_VIDEO_MEMORY_CAST (base_mem);
  GstVaapiVideoMeta *meta;
  GstAllocator *allocator;
  GstMemory *out_mem;
  gsize maxsize;

  g_return_val_if_fail (mem, NULL);
  g_return_val_if_fail (mem->meta, NULL);

  allocator = base_mem->allocator;
  g_return_val_if_fail (GST_VAAPI_IS_VIDEO_ALLOCATOR (allocator), FALSE);

  /* XXX: this implements a soft-copy, i.e. underlying VA surfaces
     are not copied */
  (void) gst_memory_get_sizes (base_mem, NULL, &maxsize);
  if (offset != 0 || (size != -1 && (gsize) size != maxsize))
    goto error_unsupported;

  if (!ensure_surface_is_current (mem))
    goto error_no_current_surface;

  meta = gst_vaapi_video_meta_copy (mem->meta);
  if (!meta)
    goto error_allocate_memory;

  out_mem = gst_vaapi_video_memory_new (allocator, meta);
  gst_vaapi_video_meta_unref (meta);
  if (!out_mem)
    goto error_allocate_memory;
  return out_mem;

  /* ERRORS */
error_no_current_surface:
  {
    GST_ERROR ("failed to make surface current");
    return NULL;
  }
error_unsupported:
  {
    GST_ERROR ("failed to copy partial memory (unsupported operation)");
    return NULL;
  }
error_allocate_memory:
  {
    GST_ERROR ("failed to allocate GstVaapiVideoMemory copy");
    return NULL;
  }
}

/* ------------------------------------------------------------------------ */
/* --- GstVaapiVideoAllocator                                           --- */
/* ------------------------------------------------------------------------ */

G_DEFINE_TYPE (GstVaapiVideoAllocator, gst_vaapi_video_allocator,
    GST_TYPE_ALLOCATOR);

static void
gst_vaapi_video_allocator_free (GstAllocator * allocator, GstMemory * base_mem)
{
  GstVaapiVideoMemory *const mem = GST_VAAPI_VIDEO_MEMORY_CAST (base_mem);

  mem->surface = NULL;
  gst_vaapi_video_memory_reset_image (mem);
  gst_vaapi_surface_proxy_replace (&mem->proxy, NULL);
  gst_vaapi_video_meta_replace (&mem->meta, NULL);
  g_mutex_clear (&mem->lock);
  g_free (mem);
}

static void
gst_vaapi_video_allocator_finalize (GObject * object)
{
  GstVaapiVideoAllocator *const allocator =
      GST_VAAPI_VIDEO_ALLOCATOR_CAST (object);

  gst_vaapi_video_pool_replace (&allocator->surface_pool, NULL);
  gst_vaapi_video_pool_replace (&allocator->image_pool, NULL);

  G_OBJECT_CLASS (gst_vaapi_video_allocator_parent_class)->finalize (object);
}

static void
gst_vaapi_video_allocator_class_init (GstVaapiVideoAllocatorClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstAllocatorClass *const allocator_class = GST_ALLOCATOR_CLASS (klass);

  _init_vaapi_video_memory_debug ();

  object_class->finalize = gst_vaapi_video_allocator_finalize;
  allocator_class->free = gst_vaapi_video_allocator_free;
}

static void
gst_vaapi_video_allocator_init (GstVaapiVideoAllocator * allocator)
{
  GstAllocator *const base_allocator = GST_ALLOCATOR_CAST (allocator);

  base_allocator->mem_type = GST_VAAPI_VIDEO_MEMORY_NAME;
  base_allocator->mem_map = gst_vaapi_video_memory_map;
  base_allocator->mem_unmap_full = gst_vaapi_video_memory_unmap_full;
  base_allocator->mem_copy = gst_vaapi_video_memory_copy;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static gboolean
gst_video_info_update_from_image (GstVideoInfo * vip, GstVaapiImage * image)
{
  GstVideoFormat format;
  const guchar *data;
  guint i, num_planes, data_size, width, height;

  /* Reset format from image */
  format = gst_vaapi_image_get_format (image);
  gst_vaapi_image_get_size (image, &width, &height);
  gst_video_info_set_format (vip, format, width, height);

  num_planes = gst_vaapi_image_get_plane_count (image);
  g_return_val_if_fail (num_planes == GST_VIDEO_INFO_N_PLANES (vip), FALSE);

  /* Determine the base data pointer */
  data = get_image_data (image);
  g_return_val_if_fail (data != NULL, FALSE);
  data_size = gst_vaapi_image_get_data_size (image);

  /* Check that we don't have disjoint planes */
  for (i = 0; i < num_planes; i++) {
    const guchar *const plane = gst_vaapi_image_get_plane (image, i);
    if (plane - data > data_size)
      return FALSE;
  }

  /* Update GstVideoInfo structure */
  for (i = 0; i < num_planes; i++) {
    const guchar *const plane = gst_vaapi_image_get_plane (image, i);
    GST_VIDEO_INFO_PLANE_OFFSET (vip, i) = plane - data;
    GST_VIDEO_INFO_PLANE_STRIDE (vip, i) = gst_vaapi_image_get_pitch (image, i);
  }
  GST_VIDEO_INFO_SIZE (vip) = data_size;
  return TRUE;
}

static gboolean
gst_video_info_update_from_surface (GstVideoInfo * vip,
    GstVaapiSurface * surface)
{
  GstVaapiImage *image;
  gboolean ret;

  ret = FALSE;
  image = gst_vaapi_surface_derive_image (surface);
  if (!image)
    goto error_no_derive_image;
  if (!gst_vaapi_image_map (image))
    goto error_cannot_map;
  ret = gst_video_info_update_from_image (vip, image);
  gst_vaapi_image_unmap (image);

bail:
  gst_vaapi_image_unref (image);
  return ret;

  /* ERRORS */
error_no_derive_image:
  {
    GST_INFO ("Cannot create a VA derived image from surface %p", surface);
    return FALSE;
  }
error_cannot_map:
  {
    GST_ERROR ("Cannot map VA derived image %p", image);
    goto bail;
  }
}

#ifndef GST_DISABLE_GST_DEBUG
static const gchar *
gst_vaapi_image_usage_flags_to_string (GstVaapiImageUsageFlags usage_flag)
{
  switch (usage_flag) {
    case GST_VAAPI_IMAGE_USAGE_FLAG_NATIVE_FORMATS:
      return "native uploading";
    case GST_VAAPI_IMAGE_USAGE_FLAG_DIRECT_RENDER:
      return "direct rendering";
    case GST_VAAPI_IMAGE_USAGE_FLAG_DIRECT_UPLOAD:
      return "direct uploading";
    default:
      return "unknown";
  }
}
#endif

static inline gboolean
allocator_configure_surface_try_specified_format (GstVaapiDisplay * display,
    const GstVideoInfo * allocation_info, GstVaapiImageUsageFlags usage_flag,
    guint surface_alloc_flag, GstVideoInfo * ret_surface_info,
    GstVaapiImageUsageFlags * ret_usage_flag)
{
  GstVaapiImageUsageFlags rflag;
  GstVaapiSurface *surface;
  GstVideoInfo sinfo, rinfo;

  /* Try to create a surface with the given allocation info */
  surface =
      gst_vaapi_surface_new_full (display, allocation_info, surface_alloc_flag);
  if (!surface)
    return FALSE;

  /* surface created and just native format usage was requested */
  if (use_native_formats (usage_flag)) {
    rflag = GST_VAAPI_IMAGE_USAGE_FLAG_NATIVE_FORMATS;
    rinfo = *allocation_info;
    goto out;
  }

  /* Further checks whether that surface can support direct
   * upload/render */
  if (gst_video_info_update_from_surface (&sinfo, surface)) {
    if (GST_VIDEO_INFO_FORMAT (&sinfo) ==
        GST_VIDEO_INFO_FORMAT (allocation_info)) {
      /* Set the correct flag */
      if (use_direct_rendering (usage_flag)
          && !use_direct_uploading (usage_flag)) {
        rflag = GST_VAAPI_IMAGE_USAGE_FLAG_DIRECT_RENDER;
      } else if (!use_direct_rendering (usage_flag)
          && use_direct_uploading (usage_flag)) {
        rflag = GST_VAAPI_IMAGE_USAGE_FLAG_DIRECT_UPLOAD;
      } else {
        g_assert_not_reached ();
      }
    } else {
      /* It shouldn't happen, but still it's possible. Just use
       * native. */
      GST_FIXME ("Got a derive image with different format!");
      rflag = GST_VAAPI_IMAGE_USAGE_FLAG_NATIVE_FORMATS;
    }

    rinfo = sinfo;
    goto out;
  }

  /* Can not derive image or not the same format, don't use derived
     images, just fallback to use native */
  rflag = GST_VAAPI_IMAGE_USAGE_FLAG_NATIVE_FORMATS;
  rinfo = *allocation_info;

out:
  gst_vaapi_surface_unref (surface);

  *ret_surface_info = rinfo;
  *ret_usage_flag = rflag;
  return TRUE;
}

static inline gboolean
allocator_configure_surface_try_other_format (GstVaapiDisplay * display,
    const GstVideoInfo * allocation_info, GstVideoInfo * ret_surface_info)
{
  GstVaapiSurface *surface;
  GstVideoFormat fmt;
  GstVideoInfo sinfo;

  /* Find a best native surface format if possible */
  fmt = gst_vaapi_video_format_get_best_native
      (GST_VIDEO_INFO_FORMAT (allocation_info));
  if (fmt == GST_VIDEO_FORMAT_UNKNOWN
      || fmt == GST_VIDEO_INFO_FORMAT (allocation_info))
    goto error_invalid_format;

  /* create a info with "best native" format */
  gst_video_info_set_format (&sinfo, fmt,
      GST_VIDEO_INFO_WIDTH (allocation_info),
      GST_VIDEO_INFO_HEIGHT (allocation_info));

  /* try it */
  surface = gst_vaapi_surface_new_full (display, &sinfo, 0);
  if (!surface)
    goto error_no_surface;
  gst_vaapi_surface_unref (surface);

  *ret_surface_info = sinfo;
  return TRUE;

  /* ERRORS */
error_invalid_format:
  {
    GST_ERROR ("Cannot handle format %s",
        GST_VIDEO_INFO_FORMAT_STRING (allocation_info));
    return FALSE;
  }
error_no_surface:
  {
    GST_ERROR ("Cannot create a VA Surface");
    return FALSE;
  }
}

static inline gboolean
allocator_configure_surface_info (GstVaapiDisplay * display,
    GstVaapiVideoAllocator * allocator, GstVaapiImageUsageFlags req_usage_flag,
    guint surface_alloc_flags)
{
  GstVaapiImageUsageFlags usage_flag;
  GstVideoInfo allocation_info, surface_info;

  /* get rid of possible encoded format and assume NV12 */
  allocation_info = allocator->allocation_info;
  gst_video_info_force_nv12_if_encoded (&allocation_info);

  /* Step1: Try the specified format and flag. May fallback to native if
     direct upload/rendering is unavailable. */
  if (allocator_configure_surface_try_specified_format (display,
          &allocation_info, req_usage_flag, surface_alloc_flags,
          &surface_info, &usage_flag)) {
    allocator->usage_flag = usage_flag;
    allocator->surface_info = surface_info;
    goto success;
  }

  /* Step2: Try other surface format. Because format is different,
     direct upload/rendering is unavailable, always use native */
  if (allocator_configure_surface_try_other_format (display, &allocation_info,
          &surface_info)) {
    allocator->usage_flag = GST_VAAPI_IMAGE_USAGE_FLAG_NATIVE_FORMATS;
    allocator->surface_info = surface_info;
    goto success;
  }

  GST_INFO_OBJECT (allocator, "Failed to configure the video format: %s"
      " with usage flag: %s",
      GST_VIDEO_INFO_FORMAT_STRING (&allocator->allocation_info),
      gst_vaapi_image_usage_flags_to_string (req_usage_flag));
  return FALSE;

success:
  GST_DEBUG_OBJECT (allocator, "success to set the surface format %s"
      " for video format %s with %s",
      GST_VIDEO_INFO_FORMAT_STRING (&allocator->surface_info),
      GST_VIDEO_INFO_FORMAT_STRING (&allocator->allocation_info),
      gst_vaapi_image_usage_flags_to_string (allocator->usage_flag));
  return TRUE;
}

static inline gboolean
allocator_configure_image_info (GstVaapiDisplay * display,
    GstVaapiVideoAllocator * allocator)
{
  GstVaapiImage *image = NULL;
  const GstVideoInfo *vinfo;
  gboolean ret = FALSE;

  if (!use_native_formats (allocator->usage_flag)) {
    allocator->image_info = allocator->surface_info;
    return TRUE;
  }

  vinfo = &allocator->allocation_info;
  allocator->image_info = *vinfo;
  gst_video_info_force_nv12_if_encoded (&allocator->image_info);

  image = new_image (display, &allocator->image_info);
  if (!image)
    goto error_no_image;
  if (!gst_vaapi_image_map (image))
    goto error_cannot_map;

  gst_video_info_update_from_image (&allocator->image_info, image);
  gst_vaapi_image_unmap (image);
  ret = TRUE;

bail:
  if (image)
    gst_vaapi_image_unref (image);
  return ret;

  /* ERRORS */
error_no_image:
  {
    GST_ERROR ("Cannot create VA image");
    return ret;
  }
error_cannot_map:
  {
    GST_ERROR ("Failed to map VA image %p", image);
    goto bail;
  }
}

static inline gboolean
allocator_params_init (GstVaapiVideoAllocator * allocator,
    GstVaapiDisplay * display, const GstVideoInfo * alloc_info,
    guint surface_alloc_flags, GstVaapiImageUsageFlags req_usage_flag)
{
  allocator->allocation_info = *alloc_info;

  if (!allocator_configure_surface_info (display, allocator, req_usage_flag,
          surface_alloc_flags))
    return FALSE;
  allocator->surface_pool = gst_vaapi_surface_pool_new_full (display,
      &allocator->surface_info, surface_alloc_flags);
  if (!allocator->surface_pool)
    goto error_create_surface_pool;

  if (!allocator_configure_image_info (display, allocator))
    return FALSE;
  allocator->image_pool = gst_vaapi_image_pool_new (display,
      &allocator->image_info);
  if (!allocator->image_pool)
    goto error_create_image_pool;

  gst_allocator_set_vaapi_video_info (GST_ALLOCATOR_CAST (allocator),
      &allocator->image_info, surface_alloc_flags);

  return TRUE;

  /* ERRORS */
error_create_surface_pool:
  {
    GST_ERROR ("failed to allocate VA surface pool");
    return FALSE;
  }
error_create_image_pool:
  {
    GST_ERROR ("failed to allocate VA image pool");
    return FALSE;
  }
}

GstAllocator *
gst_vaapi_video_allocator_new (GstVaapiDisplay * display,
    const GstVideoInfo * alloc_info, guint surface_alloc_flags,
    GstVaapiImageUsageFlags req_usage_flag)
{
  GstVaapiVideoAllocator *allocator;

  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (alloc_info != NULL, NULL);

  allocator = g_object_new (GST_VAAPI_TYPE_VIDEO_ALLOCATOR, NULL);
  if (!allocator)
    return NULL;

  if (!allocator_params_init (allocator, display, alloc_info,
          surface_alloc_flags, req_usage_flag)) {
    g_object_unref (allocator);
    return NULL;
  }

  return GST_ALLOCATOR_CAST (allocator);
}

/* ------------------------------------------------------------------------ */
/* --- GstVaapiDmaBufMemory                                             --- */
/* ------------------------------------------------------------------------ */

#define GST_VAAPI_BUFFER_PROXY_QUARK gst_vaapi_buffer_proxy_quark_get ()
static GQuark
gst_vaapi_buffer_proxy_quark_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("GstVaapiBufferProxy");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

/* Whether @mem holds an internal VA surface proxy created at
 * gst_vaapi_dmabuf_memory_new(). */
gboolean
gst_vaapi_dmabuf_memory_holds_surface (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, FALSE);

  return
      GPOINTER_TO_INT (gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (mem),
          GST_VAAPI_BUFFER_PROXY_QUARK)) == TRUE;
}

GstMemory *
gst_vaapi_dmabuf_memory_new (GstAllocator * base_allocator,
    GstVaapiVideoMeta * meta)
{
  GstMemory *mem;
  GstVaapiDisplay *display;
  GstVaapiSurface *surface;
  GstVaapiSurfaceProxy *proxy;
  GstVaapiBufferProxy *dmabuf_proxy;
  gint dmabuf_fd;
  const GstVideoInfo *surface_info;
  guint surface_alloc_flags;
  gboolean needs_surface;
  GstVaapiDmaBufAllocator *const allocator =
      GST_VAAPI_DMABUF_ALLOCATOR_CAST (base_allocator);

  g_return_val_if_fail (allocator != NULL, NULL);
  g_return_val_if_fail (meta != NULL, NULL);

  surface_info = gst_allocator_get_vaapi_video_info (base_allocator,
      &surface_alloc_flags);
  if (!surface_info)
    return NULL;

  display = gst_vaapi_video_meta_get_display (meta);
  if (!display)
    return NULL;

  proxy = gst_vaapi_video_meta_get_surface_proxy (meta);
  needs_surface = (proxy == NULL);

  if (needs_surface) {
    /* When exporting output VPP surfaces, or when exporting input
     * surfaces to be filled/imported by an upstream element, such as
     * v4l2src, we have to instantiate a VA surface to store it. */
    surface = gst_vaapi_surface_new_full (display, surface_info,
        surface_alloc_flags);
    if (!surface)
      goto error_create_surface;
    proxy = gst_vaapi_surface_proxy_new (surface);
    if (!proxy)
      goto error_create_surface_proxy;
    /* The proxy has incremented the surface ref count. */
    gst_vaapi_surface_unref (surface);
  } else {
    /* When exporting existing surfaces that come from decoder's
     * context. */
    surface = GST_VAAPI_SURFACE_PROXY_SURFACE (proxy);
  }

  dmabuf_proxy = gst_vaapi_surface_peek_dma_buf_handle (surface);
  if (!dmabuf_proxy)
    goto error_create_dmabuf_proxy;

  if (needs_surface) {
    gst_vaapi_video_meta_set_surface_proxy (meta, proxy);
    /* meta holds the proxy's reference */
    gst_vaapi_surface_proxy_unref (proxy);
  }

  /* Need dup because GstDmabufMemory creates the GstFdMemory with flag
   * GST_FD_MEMORY_FLAG_NONE. So when being freed it calls close on the fd
   * because GST_FD_MEMORY_FLAG_DONT_CLOSE is not set. */
  dmabuf_fd = gst_vaapi_buffer_proxy_get_handle (dmabuf_proxy);
  if (dmabuf_fd < 0 || (dmabuf_fd = dup (dmabuf_fd)) < 0)
    goto error_create_dmabuf_handle;

  mem = gst_dmabuf_allocator_alloc (base_allocator, dmabuf_fd,
      gst_vaapi_buffer_proxy_get_size (dmabuf_proxy));
  if (!mem)
    goto error_create_dmabuf_memory;

  if (needs_surface) {
    /* qdata express that memory has an associated surface. */
    gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (mem),
        GST_VAAPI_BUFFER_PROXY_QUARK, GINT_TO_POINTER (TRUE), NULL);
  }

  /* When a VA surface is going to be filled by a VAAPI element
   * (decoder or VPP), it has _not_ be marked as busy in the driver.
   * Releasing the surface's derived image, held by the buffer proxy,
   * the surface will be unmarked as busy. */
  if (allocator->direction == GST_PAD_SRC)
    gst_vaapi_buffer_proxy_release_data (dmabuf_proxy);

  return mem;

  /* ERRORS */
error_create_surface:
  {
    GST_ERROR ("failed to create VA surface (format:%s size:%ux%u)",
        GST_VIDEO_INFO_FORMAT_STRING (surface_info),
        GST_VIDEO_INFO_WIDTH (surface_info),
        GST_VIDEO_INFO_HEIGHT (surface_info));
    return NULL;
  }
error_create_surface_proxy:
  {
    GST_ERROR ("failed to create VA surface proxy");
    gst_vaapi_surface_unref (surface);
    return NULL;
  }
error_create_dmabuf_proxy:
  {
    GST_ERROR ("failed to export VA surface to DMABUF");
    if (surface)
      gst_vaapi_surface_unref (surface);
    if (proxy)
      gst_vaapi_surface_proxy_unref (proxy);
    return NULL;
  }
error_create_dmabuf_handle:
  {
    GST_ERROR ("failed to duplicate DMABUF handle");
    gst_vaapi_buffer_proxy_unref (dmabuf_proxy);
    return NULL;
  }
error_create_dmabuf_memory:
  {
    GST_ERROR ("failed to create DMABUF memory");
    close (dmabuf_fd);
    gst_vaapi_buffer_proxy_unref (dmabuf_proxy);
    return NULL;
  }
}

/* ------------------------------------------------------------------------ */
/* --- GstVaapiDmaBufAllocator                                          --- */
/* ------------------------------------------------------------------------ */

G_DEFINE_TYPE (GstVaapiDmaBufAllocator,
    gst_vaapi_dmabuf_allocator, GST_TYPE_DMABUF_ALLOCATOR);

static void
gst_vaapi_dmabuf_allocator_class_init (GstVaapiDmaBufAllocatorClass * klass)
{
  _init_vaapi_video_memory_debug ();
}

static void
gst_vaapi_dmabuf_allocator_init (GstVaapiDmaBufAllocator * allocator)
{
  GstAllocator *const base_allocator = GST_ALLOCATOR_CAST (allocator);

  base_allocator->mem_type = GST_VAAPI_DMABUF_ALLOCATOR_NAME;
  allocator->direction = GST_PAD_SINK;
}

GstAllocator *
gst_vaapi_dmabuf_allocator_new (GstVaapiDisplay * display,
    const GstVideoInfo * alloc_info, guint surface_alloc_flags,
    GstPadDirection direction)
{
  GstVaapiDmaBufAllocator *allocator = NULL;
  GstVaapiSurface *surface = NULL;
  GstVideoInfo surface_info;
  GstAllocator *base_allocator;

  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (alloc_info != NULL, NULL);

  allocator = g_object_new (GST_VAAPI_TYPE_DMABUF_ALLOCATOR, NULL);
  if (!allocator)
    goto error_no_allocator;

  base_allocator = GST_ALLOCATOR_CAST (allocator);

  gst_video_info_set_format (&surface_info, GST_VIDEO_INFO_FORMAT (alloc_info),
      GST_VIDEO_INFO_WIDTH (alloc_info), GST_VIDEO_INFO_HEIGHT (alloc_info));
  surface = gst_vaapi_surface_new_full (display, alloc_info,
      surface_alloc_flags);
  if (!surface)
    goto error_no_surface;
  if (!gst_video_info_update_from_surface (&surface_info, surface))
    goto fail;
  gst_mini_object_replace ((GstMiniObject **) & surface, NULL);

  gst_allocator_set_vaapi_video_info (base_allocator, &surface_info,
      surface_alloc_flags);

  allocator->direction = direction;

  return base_allocator;

  /* ERRORS */
fail:
  {
    gst_mini_object_replace ((GstMiniObject **) & surface, NULL);
    gst_object_replace ((GstObject **) & base_allocator, NULL);
    return NULL;
  }
error_no_allocator:
  {
    GST_ERROR ("failed to create a new dmabuf allocator");
    return NULL;
  }
error_no_surface:
  {
    GST_ERROR ("failed to create a new surface");
    goto fail;
  }
}

/* ------------------------------------------------------------------------ */
/* --- GstVaapiVideoInfo = { GstVideoInfo, flags }                      --- */
/* ------------------------------------------------------------------------ */

#define GST_VAAPI_VIDEO_INFO_QUARK gst_vaapi_video_info_quark_get ()
static GQuark
gst_vaapi_video_info_quark_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("GstVaapiVideoInfo");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

#define ALLOCATION_VINFO_QUARK allocation_vinfo_quark_get ()
static GQuark
allocation_vinfo_quark_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("allocation-vinfo");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

#define SURFACE_ALLOC_FLAGS_QUARK surface_alloc_flags_quark_get ()
static GQuark
surface_alloc_flags_quark_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("surface-alloc-flags");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

#define NEGOTIATED_VINFO_QUARK negotiated_vinfo_quark_get ()
static GQuark
negotiated_vinfo_quark_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("negotiated-vinfo");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

/**
 * gst_allocator_get_vaapi_video_info:
 * @allocator: a #GstAllocator
 * @out_flags_ptr: (out): the stored surface allocation flags
 *
 * Will get the @allocator qdata to fetch the flags and the
 * allocation's #GstVideoInfo stored in it.
 *
 * The allocation video info, is the image video info in the case of
 * the #GstVaapiVideoAllocator; and the allocation video info in the
 * case of #GstVaapiDmaBufAllocator.
 *
 * Returns: the stored #GstVideoInfo
 **/
const GstVideoInfo *
gst_allocator_get_vaapi_video_info (GstAllocator * allocator,
    guint * out_flags_ptr)
{
  const GstStructure *structure;
  const GValue *value;

  g_return_val_if_fail (GST_IS_ALLOCATOR (allocator), NULL);

  structure =
      g_object_get_qdata (G_OBJECT (allocator), GST_VAAPI_VIDEO_INFO_QUARK);
  if (!structure)
    return NULL;

  if (out_flags_ptr) {
    value = gst_structure_id_get_value (structure, SURFACE_ALLOC_FLAGS_QUARK);
    if (!value)
      return NULL;
    *out_flags_ptr = g_value_get_uint (value);
  }

  value = gst_structure_id_get_value (structure, ALLOCATION_VINFO_QUARK);
  if (!value)
    return NULL;
  return g_value_get_boxed (value);
}

/**
 * gst_allocator_set_vaapi_video_info:
 * @allocator: a #GstAllocator
 * @alloc_info: the allocation #GstVideoInfo to store
 * @surface_alloc_flags: the flags to store
 *
 * Stores as GObject's qdata the @alloc_info and the
 * @surface_alloc_flags in the allocator. This will "decorate" the
 * allocator as a GstVaapi one.
 *
 * Returns: always %TRUE
 **/
gboolean
gst_allocator_set_vaapi_video_info (GstAllocator * allocator,
    const GstVideoInfo * alloc_info, guint surface_alloc_flags)
{
  g_return_val_if_fail (GST_IS_ALLOCATOR (allocator), FALSE);
  g_return_val_if_fail (alloc_info != NULL, FALSE);

  g_object_set_qdata_full (G_OBJECT (allocator), GST_VAAPI_VIDEO_INFO_QUARK,
      gst_structure_new_id (GST_VAAPI_VIDEO_INFO_QUARK,
          ALLOCATION_VINFO_QUARK, GST_TYPE_VIDEO_INFO, alloc_info,
          SURFACE_ALLOC_FLAGS_QUARK, G_TYPE_UINT, surface_alloc_flags, NULL),
      (GDestroyNotify) gst_structure_free);

  return TRUE;
}

/**
 * gst_allocator_set_vaapi_negotiated_video_info:
 * @allocator: a #GstAllocator
 * @negotiated_vinfo: the negotiated #GstVideoInfo to store.  If NULL, then
 * removes any previously set value.
 *
 * Stores as GObject's qdata the @negotiated_vinfo in the allocator
 * instance.
 *
 * The @negotiated_vinfo is different of the @alloc_info from
 * gst_allocator_set_vaapi_video_info(), and might not be set.
 **/
void
gst_allocator_set_vaapi_negotiated_video_info (GstAllocator * allocator,
    const GstVideoInfo * negotiated_vinfo)
{
  g_return_if_fail (allocator && GST_IS_ALLOCATOR (allocator));

  if (negotiated_vinfo)
    g_object_set_qdata_full (G_OBJECT (allocator), NEGOTIATED_VINFO_QUARK,
        gst_video_info_copy (negotiated_vinfo),
        (GDestroyNotify) gst_video_info_free);
  else
    g_object_set_qdata (G_OBJECT (allocator), NEGOTIATED_VINFO_QUARK, NULL);
}

/**
 * gst_allocator_get_vaapi_negotiated_video_info:
 * @allocator: a #GstAllocator
 *
 * Returns: the stored negotiation #GstVideoInfo, if it was stored
 * previously. Otherwise, %NULL
 **/
GstVideoInfo *
gst_allocator_get_vaapi_negotiated_video_info (GstAllocator * allocator)
{
  g_return_val_if_fail (GST_IS_ALLOCATOR (allocator), NULL);

  return g_object_get_qdata (G_OBJECT (allocator), NEGOTIATED_VINFO_QUARK);
}

/**
 * gst_vaapi_is_dmabuf_allocator:
 * @allocator: an #GstAllocator
 *
 * Checks if the allocator is DMABuf allocator with the GstVaapi
 * decorator.
 *
 * Returns: %TRUE if @allocator is a DMABuf allocator type with
 * GstVaapi decorator.
 **/
gboolean
gst_vaapi_is_dmabuf_allocator (GstAllocator * allocator)
{
  GstStructure *st;

  g_return_val_if_fail (GST_IS_ALLOCATOR (allocator), FALSE);

  if (g_strcmp0 (allocator->mem_type, GST_VAAPI_DMABUF_ALLOCATOR_NAME) != 0)
    return FALSE;
  st = g_object_get_qdata (G_OBJECT (allocator), GST_VAAPI_VIDEO_INFO_QUARK);
  return (st != NULL);
}

/**
 * gst_vaapi_dmabuf_can_map:
 * @display: a #GstVaapiDisplay
 * @allocator: a #GstAllocator
 *
 * It will create a dmabuf-based buffer using @allocator, and it will
 * try to map it using gst_memory_map().
 *
 * Returns: %TRUE if the internal dummy buffer can be
 * mapped. Otherwise %FALSE.
 **/
gboolean
gst_vaapi_dmabuf_can_map (GstVaapiDisplay * display, GstAllocator * allocator)
{
  GstVaapiVideoMeta *meta;
  GstMemory *mem;
  GstMapInfo info;
  gboolean ret;

  g_return_val_if_fail (display != NULL, FALSE);

  ret = FALSE;
  mem = NULL;
  meta = NULL;
  if (!gst_vaapi_is_dmabuf_allocator (allocator))
    return FALSE;
  meta = gst_vaapi_video_meta_new (display);
  if (!meta)
    return FALSE;
  mem = gst_vaapi_dmabuf_memory_new (allocator, meta);
  if (!mem)
    goto bail;

  if (!gst_memory_map (mem, &info, GST_MAP_READWRITE) || info.size == 0)
    goto bail;

  gst_memory_unmap (mem, &info);
  ret = TRUE;

bail:
  if (mem)
    gst_memory_unref (mem);
  if (meta)
    gst_vaapi_video_meta_unref (meta);
  return ret;
}
