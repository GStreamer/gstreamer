/*
 *  gstvaapisurfacepool.c - Gst VA surface pool
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2013 Intel Corporation
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

/**
 * SECTION:gstvaapisurfacepool
 * @short_description: VA surface pool
 */

#include "sysdeps.h"
#include "gstvaapisurfacepool.h"
#include "gstvaapivideopool_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/**
 * GstVaapiSurfacePool:
 *
 * A pool of lazily allocated #GstVaapiSurface objects.
 */
struct _GstVaapiSurfacePool
{
  /*< private > */
  GstVaapiVideoPool parent_instance;

  GstVaapiChromaType chroma_type;
  GstVideoInfo video_info;
  guint alloc_flags;
};

static gboolean
surface_pool_init (GstVaapiSurfacePool * pool, const GstVideoInfo * vip,
    guint surface_allocation_flags)
{
  const GstVideoFormat format = GST_VIDEO_INFO_FORMAT (vip);

  pool->video_info = *vip;
  pool->alloc_flags = surface_allocation_flags;

  if (format == GST_VIDEO_FORMAT_UNKNOWN)
    return FALSE;

  if (format == GST_VIDEO_FORMAT_ENCODED)
    pool->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
  else
    pool->chroma_type = gst_vaapi_video_format_get_chroma_type (format);
  if (!pool->chroma_type)
    return FALSE;
  return TRUE;
}

static gpointer
gst_vaapi_surface_pool_alloc_object (GstVaapiVideoPool * base_pool)
{
  GstVaapiSurfacePool *const pool = GST_VAAPI_SURFACE_POOL (base_pool);

  /* Try to allocate a surface with an explicit pixel format first */
  if (GST_VIDEO_INFO_FORMAT (&pool->video_info) != GST_VIDEO_FORMAT_ENCODED) {
    GstVaapiSurface *const surface =
        gst_vaapi_surface_new_full (base_pool->display, &pool->video_info,
        pool->alloc_flags);
    if (surface)
      return surface;
  }

  /* Otherwise, fallback to the original interface, based on chroma format */
  return gst_vaapi_surface_new (base_pool->display,
      pool->chroma_type, GST_VIDEO_INFO_WIDTH (&pool->video_info),
      GST_VIDEO_INFO_HEIGHT (&pool->video_info));
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_surface_pool_class (void)
{
  static const GstVaapiVideoPoolClass GstVaapiSurfacePoolClass = {
    {sizeof (GstVaapiSurfacePool),
        (GDestroyNotify) gst_vaapi_video_pool_finalize}
    ,
    .alloc_object = gst_vaapi_surface_pool_alloc_object
  };
  return GST_VAAPI_MINI_OBJECT_CLASS (&GstVaapiSurfacePoolClass);
}

/**
 * gst_vaapi_surface_pool_new:
 * @display: a #GstVaapiDisplay
 * @format: a #GstVideoFormat
 * @width: the desired width, in pixels
 * @height: the desired height, in pixels
 * @surface_allocation_flags: (optional) allocation flags
 *
 * Creates a new #GstVaapiVideoPool of #GstVaapiSurface with the specified
 * format and dimensions. If @format is GST_VIDEO_FORMAT_ENCODED, then
 * surfaces with best "native" format would be created. Typically, this is
 * NV12 format, but this is implementation (driver) defined.
 *
 * Return value: the newly allocated #GstVaapiVideoPool
 */
GstVaapiVideoPool *
gst_vaapi_surface_pool_new (GstVaapiDisplay * display, GstVideoFormat format,
    guint width, guint height, guint surface_allocation_flags)
{
  GstVideoInfo vi;

  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  gst_video_info_set_format (&vi, format, width, height);
  return gst_vaapi_surface_pool_new_full (display, &vi,
      surface_allocation_flags);
}

/**
 * gst_vaapi_surface_pool_new_full:
 * @display: a #GstVaapiDisplay
 * @vip: a #GstVideoInfo
 * @surface_allocation_flags: (optional) allocation flags
 *
 * Creates a new #GstVaapiVideoPool of #GstVaapiSurface with the
 * specified format and dimensions in @vip.
 *
 * Return value: the newly allocated #GstVaapiVideoPool
 */
GstVaapiVideoPool *
gst_vaapi_surface_pool_new_full (GstVaapiDisplay * display,
    const GstVideoInfo * vip, guint surface_allocation_flags)
{
  GstVaapiVideoPool *pool;

  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (vip != NULL, NULL);

  pool = (GstVaapiVideoPool *)
      gst_vaapi_mini_object_new (gst_vaapi_surface_pool_class ());
  if (!pool)
    return NULL;

  gst_vaapi_video_pool_init (pool, display,
      GST_VAAPI_VIDEO_POOL_OBJECT_TYPE_SURFACE);
  if (!surface_pool_init (GST_VAAPI_SURFACE_POOL (pool), vip,
          surface_allocation_flags))
    goto error;
  return pool;

  /* ERRORS */
error:
  {
    gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (pool));
    return NULL;
  }
}

/**
 * gst_vaapi_surface_pool_new_with_chroma_type:
 * @display: a #GstVaapiDisplay
 * @chroma_type: a #GstVaapiChromatype
 * @width: the desired width, in pixels
 * @height: the desired height, in pixels
 * @surface_allocation_flags: (optional) allocation flags
 *
 * Creates a new #GstVaapiVideoPool of #GstVaapiSurface with the specified
 * chroma type and dimensions. The underlying format of the surfaces is
 * implementation (driver) defined.
 *
 * Return value: the newly allocated #GstVaapiVideoPool
 */
GstVaapiVideoPool *
gst_vaapi_surface_pool_new_with_chroma_type (GstVaapiDisplay * display,
    GstVaapiChromaType chroma_type, guint width, guint height,
    guint surface_allocation_flags)
{
  GstVaapiVideoPool *pool;
  GstVideoInfo vi;

  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (chroma_type > 0, NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  gst_video_info_set_format (&vi, GST_VIDEO_FORMAT_ENCODED, width, height);

  pool =
      gst_vaapi_surface_pool_new_full (display, &vi, surface_allocation_flags);
  if (!pool)
    return NULL;

  GST_VAAPI_SURFACE_POOL (pool)->chroma_type = chroma_type;

  return pool;
}
