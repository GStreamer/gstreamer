/*
 *  gstvaapisurfacepool.c - Gst VA surface pool
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "config.h"
#include "gstvaapisurfacepool.h"

#define DEBUG 1
#include "vaapi_debug.h"

G_DEFINE_TYPE(GstVaapiSurfacePool, gst_vaapi_surface_pool, G_TYPE_OBJECT);

#define GST_VAAPI_SURFACE_POOL_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_SURFACE_POOL,	\
                                 GstVaapiSurfacePoolPrivate))

struct _GstVaapiSurfacePoolPrivate {
    GstVaapiDisplay    *display;
    GQueue              free_surfaces;
    GList              *used_surfaces;
    GstCaps            *caps;
    GstVaapiChromaType  chroma_type;
    guint               width;
    guint               height;
};

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_CAPS,
};

static void
gst_vaapi_surface_pool_clear(GstVaapiSurfacePool *pool)
{
    GstVaapiSurfacePoolPrivate * const priv = pool->priv;
    GstVaapiSurface *surface;
    GList *list, *next;

    for (list = priv->used_surfaces; list; list = next) {
        next = list->next;
        g_object_unref(list->data);
        g_list_free_1(list);
    }
    priv->used_surfaces = NULL;

    while ((surface = g_queue_pop_head(&priv->free_surfaces)))
        g_object_unref(surface);
}

static void
gst_vaapi_surface_pool_destroy(GstVaapiSurfacePool *pool)
{
    GstVaapiSurfacePoolPrivate * const priv = pool->priv;

    gst_vaapi_surface_pool_clear(pool);

    if (priv->caps) {
        gst_caps_unref(priv->caps);
        priv->caps = NULL;
    }

    if (priv->display) {
        g_object_unref(priv->display);
        priv->display = NULL;
    }
}

static void
gst_vaapi_surface_pool_finalize(GObject *object)
{
    gst_vaapi_surface_pool_destroy(GST_VAAPI_SURFACE_POOL(object));

    G_OBJECT_CLASS(gst_vaapi_surface_pool_parent_class)->finalize(object);
}

static void
gst_vaapi_surface_pool_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiSurfacePool * const pool = GST_VAAPI_SURFACE_POOL(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        pool->priv->display = g_object_ref(g_value_get_object(value));
        break;
    case PROP_CAPS:
        gst_vaapi_surface_pool_set_caps(pool, g_value_get_pointer(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_surface_pool_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiSurfacePool * const pool = GST_VAAPI_SURFACE_POOL(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_object(value, pool->priv->display);
        break;
    case PROP_CAPS:
        g_value_set_pointer(value, gst_vaapi_surface_pool_get_caps(pool));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_surface_pool_class_init(GstVaapiSurfacePoolClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiSurfacePoolPrivate));

    object_class->finalize      = gst_vaapi_surface_pool_finalize;
    object_class->set_property  = gst_vaapi_surface_pool_set_property;
    object_class->get_property  = gst_vaapi_surface_pool_get_property;

    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_object("display",
                             "display",
                             "Gstreamer/VA display",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_CAPS,
         g_param_spec_pointer("caps",
                              "surface caps",
                              "Surface caps",
                              G_PARAM_READWRITE));
}

static void
gst_vaapi_surface_pool_init(GstVaapiSurfacePool *pool)
{
    GstVaapiSurfacePoolPrivate *priv = GST_VAAPI_SURFACE_POOL_GET_PRIVATE(pool);

    pool->priv          = priv;
    priv->display       = NULL;
    priv->used_surfaces = NULL;
    priv->caps          = NULL;
    priv->chroma_type   = 0;
    priv->width         = 0;
    priv->height        = 0;

    g_queue_init(&priv->free_surfaces);
}

GstVaapiSurfacePool *
gst_vaapi_surface_pool_new(GstVaapiDisplay *display, GstCaps *caps)
{
    return g_object_new(GST_VAAPI_TYPE_SURFACE_POOL,
                        "display", display,
                        "caps",    caps,
                        NULL);
}

GstCaps *
gst_vaapi_surface_pool_get_caps(GstVaapiSurfacePool *pool)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_POOL(pool), NULL);

    return pool->priv->caps;
}

void
gst_vaapi_surface_pool_set_caps(GstVaapiSurfacePool *pool, GstCaps *caps)
{
    GstVaapiSurfacePoolPrivate *priv;
    GstStructure *structure;
    gint width, height;

    g_return_if_fail(GST_VAAPI_IS_SURFACE_POOL(pool));
    g_return_if_fail(GST_IS_CAPS(caps));

    priv = pool->priv;

    structure = gst_caps_get_structure(caps, 0);
    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);

    /* Don't do anything if caps have not changed */
    if (width == priv->width && height == priv->height)
        return;

    gst_vaapi_surface_pool_clear(pool);

    priv->caps          = gst_caps_ref(caps);
    priv->chroma_type   = GST_VAAPI_CHROMA_TYPE_YUV420;
    priv->width         = width;
    priv->height        = height;
}

GstVaapiSurface *
gst_vaapi_surface_pool_new_surface(GstVaapiSurfacePool *pool)
{
    GstVaapiSurfacePoolPrivate *priv;
    GstVaapiSurface *surface;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_POOL(pool), NULL);

    priv = pool->priv;

    surface = g_queue_pop_head(&priv->free_surfaces);
    if (!surface) {
        surface = gst_vaapi_surface_new(
            priv->display,
            priv->chroma_type,
            priv->width,
            priv->height
        );
        if (!surface)
            return NULL;
    }

    priv->used_surfaces = g_list_prepend(priv->used_surfaces, surface);
    return g_object_ref(surface);
}

void
gst_vaapi_surface_pool_free_surface(
    GstVaapiSurfacePool *pool,
    GstVaapiSurface     *surface
)
{
    GstVaapiSurfacePoolPrivate *priv;
    GList *list;

    g_return_if_fail(GST_VAAPI_IS_SURFACE_POOL(pool));
    g_return_if_fail(GST_VAAPI_IS_SURFACE(surface));

    priv = pool->priv;
    list = g_list_find(priv->used_surfaces, surface);
    if (!list)
        return;

    g_object_unref(surface);
    priv->used_surfaces = g_list_delete_link(priv->used_surfaces, list);
    g_queue_push_tail(&priv->free_surfaces, surface);
}
