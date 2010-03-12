/*
 *  gstvaapivideopool.c - Video object pool abstraction
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
#include "gstvaapivideopool.h"

#define DEBUG 1
#include "vaapi_debug.h"

G_DEFINE_TYPE(GstVaapiVideoPool, gst_vaapi_video_pool, G_TYPE_OBJECT);

#define GST_VAAPI_VIDEO_POOL_GET_PRIVATE(obj)                   \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_VIDEO_POOL,	\
                                 GstVaapiVideoPoolPrivate))

struct _GstVaapiVideoPoolPrivate {
    GstVaapiDisplay    *display;
    GQueue              free_objects;
    GList              *used_objects;
    GstCaps            *caps;
};

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_CAPS,
};

static void
gst_vaapi_video_pool_set_caps(GstVaapiVideoPool *pool, GstCaps *caps);

static void
gst_vaapi_video_pool_clear(GstVaapiVideoPool *pool)
{
    GstVaapiVideoPoolPrivate * const priv = pool->priv;
    gpointer object;
    GList *list, *next;

    for (list = priv->used_objects; list; list = next) {
        next = list->next;
        g_object_unref(list->data);
        g_list_free_1(list);
    }
    priv->used_objects = NULL;

    while ((object = g_queue_pop_head(&priv->free_objects)))
        g_object_unref(object);
}

static void
gst_vaapi_video_pool_destroy(GstVaapiVideoPool *pool)
{
    GstVaapiVideoPoolPrivate * const priv = pool->priv;

    gst_vaapi_video_pool_clear(pool);

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
gst_vaapi_video_pool_finalize(GObject *object)
{
    gst_vaapi_video_pool_destroy(GST_VAAPI_VIDEO_POOL(object));

    G_OBJECT_CLASS(gst_vaapi_video_pool_parent_class)->finalize(object);
}

static void
gst_vaapi_video_pool_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiVideoPool * const pool = GST_VAAPI_VIDEO_POOL(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        pool->priv->display = g_object_ref(g_value_get_object(value));
        break;
    case PROP_CAPS:
        gst_vaapi_video_pool_set_caps(pool, g_value_get_pointer(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_video_pool_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiVideoPool * const pool = GST_VAAPI_VIDEO_POOL(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_object(value, pool->priv->display);
        break;
    case PROP_CAPS:
        g_value_set_pointer(value, gst_vaapi_video_pool_get_caps(pool));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_video_pool_class_init(GstVaapiVideoPoolClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiVideoPoolPrivate));

    object_class->finalize      = gst_vaapi_video_pool_finalize;
    object_class->set_property  = gst_vaapi_video_pool_set_property;
    object_class->get_property  = gst_vaapi_video_pool_get_property;

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
                              "caps",
                              "Caps",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_video_pool_init(GstVaapiVideoPool *pool)
{
    GstVaapiVideoPoolPrivate *priv = GST_VAAPI_VIDEO_POOL_GET_PRIVATE(pool);

    pool->priv          = priv;
    priv->display       = NULL;
    priv->used_objects  = NULL;
    priv->caps          = NULL;

    g_queue_init(&priv->free_objects);
}

GstVaapiVideoPool *
gst_vaapi_video_pool_new(GstVaapiDisplay *display, GstCaps *caps)
{
    return g_object_new(GST_VAAPI_TYPE_VIDEO_POOL,
                        "display", display,
                        "caps",    caps,
                        NULL);
}

GstCaps *
gst_vaapi_video_pool_get_caps(GstVaapiVideoPool *pool)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), NULL);

    return pool->priv->caps;
}

void
gst_vaapi_video_pool_set_caps(GstVaapiVideoPool *pool, GstCaps *caps)
{
    GstVaapiVideoPoolClass * const klass = GST_VAAPI_VIDEO_POOL_GET_CLASS(pool);

    pool->priv->caps = gst_caps_ref(caps);

    if (klass->set_caps)
        klass->set_caps(pool, caps);
}

gpointer
gst_vaapi_video_pool_get_object(GstVaapiVideoPool *pool)
{
    gpointer object;

    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), NULL);

    object = g_queue_pop_head(&pool->priv->free_objects);
    if (!object) {
        object = GST_VAAPI_VIDEO_POOL_GET_CLASS(pool)->alloc_object(
            pool,
            pool->priv->display
        );
        if (!object)
            return NULL;
    }

    pool->priv->used_objects = g_list_prepend(pool->priv->used_objects, object);
    return g_object_ref(object);
}

void
gst_vaapi_video_pool_put_object(GstVaapiVideoPool *pool, gpointer object)
{
    GstVaapiVideoPoolPrivate *priv;
    GList *elem;

    g_return_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool));
    g_return_if_fail(G_IS_OBJECT(object));

    priv = pool->priv;
    elem = g_list_find(priv->used_objects, object);
    if (!elem)
        return;

    g_object_unref(object);
    priv->used_objects = g_list_delete_link(priv->used_objects, elem);
    g_queue_push_tail(&priv->free_objects, object);
}
