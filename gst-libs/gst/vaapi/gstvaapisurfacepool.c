/*
 *  gstvaapisurfacepool.c - Gst VA surface pool
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2012 Intel Corporation
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
struct _GstVaapiSurfacePool {
    /*< private >*/
    GstVaapiVideoPool   parent_instance;

    GstVaapiChromaType  chroma_type;
    GstVideoFormat      format;
    guint               width;
    guint               height;
};

static gboolean
surface_pool_init(GstVaapiSurfacePool *pool, const GstVideoInfo *vip)
{
    pool->format = GST_VIDEO_INFO_FORMAT(vip);
    pool->width  = GST_VIDEO_INFO_WIDTH(vip);
    pool->height = GST_VIDEO_INFO_HEIGHT(vip);

    if (pool->format == GST_VIDEO_FORMAT_UNKNOWN)
        return FALSE;

    if (pool->format == GST_VIDEO_FORMAT_ENCODED)
        pool->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
    else
        pool->chroma_type = gst_video_format_get_chroma_type(pool->format);
    if (!pool->chroma_type)
        return FALSE;
    return TRUE;
}

static gpointer
gst_vaapi_surface_pool_alloc_object(GstVaapiVideoPool *base_pool)
{
    GstVaapiSurfacePool * const pool = GST_VAAPI_SURFACE_POOL(base_pool);

    /* Try to allocate a surface with an explicit pixel format first */
    if (pool->format != GST_VIDEO_FORMAT_ENCODED) {
        GstVaapiSurface * const surface = gst_vaapi_surface_new_with_format(
            base_pool->display, pool->format, pool->width, pool->height);
        if (surface)
            return surface;
    }

    /* Otherwise, fallback to the original interface, based on chroma format */
    return gst_vaapi_surface_new(base_pool->display,
        pool->chroma_type, pool->width, pool->height);
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_surface_pool_class(void)
{
    static const GstVaapiVideoPoolClass GstVaapiSurfacePoolClass = {
        { sizeof(GstVaapiSurfacePool),
          (GDestroyNotify)gst_vaapi_video_pool_finalize },

        .alloc_object   = gst_vaapi_surface_pool_alloc_object
    };
    return GST_VAAPI_MINI_OBJECT_CLASS(&GstVaapiSurfacePoolClass);
}

/**
 * gst_vaapi_surface_pool_new:
 * @display: a #GstVaapiDisplay
 * @vip: a #GstVideoInfo
 *
 * Creates a new #GstVaapiVideoPool of #GstVaapiSurface with the
 * specified format and dimensions in @vip.
 *
 * Return value: the newly allocated #GstVaapiVideoPool
 */
GstVaapiVideoPool *
gst_vaapi_surface_pool_new(GstVaapiDisplay *display, const GstVideoInfo *vip)
{
    GstVaapiVideoPool *pool;

    g_return_val_if_fail(display != NULL, NULL);
    g_return_val_if_fail(vip != NULL, NULL);

    pool = (GstVaapiVideoPool *)
        gst_vaapi_mini_object_new(gst_vaapi_surface_pool_class());
    if (!pool)
        return NULL;

    gst_vaapi_video_pool_init(pool, display,
        GST_VAAPI_VIDEO_POOL_OBJECT_TYPE_SURFACE);
    if (!surface_pool_init(GST_VAAPI_SURFACE_POOL(pool), vip))
        goto error;
    return pool;

error:
    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(pool));
    return NULL;
}
