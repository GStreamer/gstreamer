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

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(
    GstVaapiSurfacePool,
    gst_vaapi_surface_pool,
    GST_VAAPI_TYPE_VIDEO_POOL)

#define GST_VAAPI_SURFACE_POOL_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_SURFACE_POOL,	\
                                 GstVaapiSurfacePoolPrivate))

struct _GstVaapiSurfacePoolPrivate {
    GstVaapiChromaType  chroma_type;
    guint               width;
    guint               height;
};

static void
gst_vaapi_surface_pool_set_caps(GstVaapiVideoPool *pool, GstCaps *caps)
{
    GstVaapiSurfacePoolPrivate *priv = GST_VAAPI_SURFACE_POOL(pool)->priv;
    GstStructure *structure;
    gint width, height;

    structure = gst_caps_get_structure(caps, 0);
    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);

    priv->chroma_type   = GST_VAAPI_CHROMA_TYPE_YUV420;
    priv->width         = width;
    priv->height        = height;
}

gpointer
gst_vaapi_surface_pool_alloc_object(
    GstVaapiVideoPool *pool,
    GstVaapiDisplay   *display
)
{
    GstVaapiSurfacePoolPrivate *priv = GST_VAAPI_SURFACE_POOL(pool)->priv;

    return gst_vaapi_surface_new(display,
                                 priv->chroma_type,
                                 priv->width,
                                 priv->height);
}

static void
gst_vaapi_surface_pool_finalize(GObject *object)
{
    G_OBJECT_CLASS(gst_vaapi_surface_pool_parent_class)->finalize(object);
}

static void
gst_vaapi_surface_pool_class_init(GstVaapiSurfacePoolClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiVideoPoolClass * const pool_class = GST_VAAPI_VIDEO_POOL_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiSurfacePoolPrivate));

    object_class->finalize      = gst_vaapi_surface_pool_finalize;

    pool_class->set_caps        = gst_vaapi_surface_pool_set_caps;
    pool_class->alloc_object    = gst_vaapi_surface_pool_alloc_object;
}

static void
gst_vaapi_surface_pool_init(GstVaapiSurfacePool *pool)
{
    GstVaapiSurfacePoolPrivate *priv = GST_VAAPI_SURFACE_POOL_GET_PRIVATE(pool);

    pool->priv          = priv;
    priv->chroma_type   = 0;
    priv->width         = 0;
    priv->height        = 0;
}

/**
 * gst_vaapi_surface_pool_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps
 *
 * Creates a new #GstVaapiVideoPool of #GstVaapiSurface with the
 * specified dimensions in @caps.
 *
 * Return value: the newly allocated #GstVaapiVideoPool
 */
GstVaapiVideoPool *
gst_vaapi_surface_pool_new(GstVaapiDisplay *display, GstCaps *caps)
{
    return g_object_new(GST_VAAPI_TYPE_SURFACE_POOL,
                        "display", display,
                        "caps",    caps,
                        NULL);
}
