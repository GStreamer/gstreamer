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

/**
 * SECTION:gstvaapivideopool
 * @short_description: Video object pool abstraction
 */

#include "config.h"
#include "gstvaapivideopool.h"

#define DEBUG 1
#include "gstvaapidebug.h"

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

    /**
     * GstVaapiVideoPool:display:
     *
     * The #GstVaapiDisplay this pool is bound to.
     */
    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_object("display",
                             "Display",
                             "The GstVaapiDisplay this pool is bound to",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    /**
     * GstVaapiVidePool:caps:
     *
     * The video object capabilities represented as a #GstCaps. This
     * shall hold at least the "width" and "height" properties.
     */
    g_object_class_install_property
        (object_class,
         PROP_CAPS,
         g_param_spec_pointer("caps",
                              "caps",
                              "The video object capabilities",
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

/**
 * gst_vaapi_video_pool_get_caps:
 * @pool: a #GstVaapiVideoPool
 *
 * Retrieves the #GstCaps the @pool was created with. The @pool owns
 * the returned object and it shall not be unref'ed.
 *
 * Return value: the #GstCaps the @pool was created with
 */
GstCaps *
gst_vaapi_video_pool_get_caps(GstVaapiVideoPool *pool)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), NULL);

    return pool->priv->caps;
}

/*
 * gst_vaapi_video_pool_set_caps:
 * @pool: a #GstVaapiVideoPool
 * @caps: a #GstCaps
 *
 * Binds new @caps to the @pool and notify the sub-classes.
 */
void
gst_vaapi_video_pool_set_caps(GstVaapiVideoPool *pool, GstCaps *caps)
{
    GstVaapiVideoPoolClass * const klass = GST_VAAPI_VIDEO_POOL_GET_CLASS(pool);

    pool->priv->caps = gst_caps_ref(caps);

    if (klass->set_caps)
        klass->set_caps(pool, caps);
}

/**
 * gst_vaapi_video_pool_get_object:
 * @pool: a #GstVaapiVideoPool
 *
 * Retrieves a new object from the @pool, or allocates a new one if
 * none was found. The @pool holds a reference on the returned object
 * and thus shall be released through gst_vaapi_video_pool_put_object()
 * when it's no longer needed.
 *
 * Return value: a possibly newly allocated object, or %NULL on error
 */
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

/**
 * gst_vaapi_video_pool_put_object:
 * @pool: a #GstVaapiVideoPool
 * @object: the object to add to the pool
 *
 * Pushes the @object back into the pool. The @object shall be
 * previously allocated from the @pool. Calling this function with an
 * arbitrary object yields undefined behaviour.
 */
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
