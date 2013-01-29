/*
 *  gstvaapivideopool.c - Video object pool abstraction
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
 * SECTION:gstvaapivideopool
 * @short_description: Video object pool abstraction
 */

#include "sysdeps.h"
#include "gstvaapivideopool.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiVideoPool, gst_vaapi_video_pool, G_TYPE_OBJECT)

#define GST_VAAPI_VIDEO_POOL_GET_PRIVATE(obj)                   \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_VIDEO_POOL,	\
                                 GstVaapiVideoPoolPrivate))

struct _GstVaapiVideoPoolPrivate {
    GstVaapiDisplay    *display;
    GQueue              free_objects;
    GList              *used_objects;
    GstCaps            *caps;
    guint               used_count;
    guint               capacity;
};

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_CAPS,
    PROP_CAPACITY
};

static void
gst_vaapi_video_pool_set_caps(GstVaapiVideoPool *pool, GstCaps *caps);

static inline gpointer
gst_vaapi_video_pool_alloc_object(GstVaapiVideoPool *pool)
{
    GstVaapiVideoPoolClass * const klass = GST_VAAPI_VIDEO_POOL_GET_CLASS(pool);

    return klass->alloc_object(pool, pool->priv->display);
}

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

    g_clear_object(&priv->display);
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
    case PROP_CAPACITY:
        gst_vaapi_video_pool_set_capacity(pool, g_value_get_uint(value));
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
        g_value_set_object(value, gst_vaapi_video_pool_get_display(pool));
        break;
    case PROP_CAPS:
        g_value_set_pointer(value, gst_vaapi_video_pool_get_caps(pool));
        break;
    case PROP_CAPACITY:
        g_value_set_uint(value, gst_vaapi_video_pool_get_capacity(pool));
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

    /**
     * GstVaapiVidePool:capacity:
     *
     * The maximum number of objects in the pool. Or zero, the pool
     * will allocate as many objects as possible.
     */
    g_object_class_install_property
        (object_class,
         PROP_CAPACITY,
         g_param_spec_uint("capacity",
                           "capacity",
                           "The maximum number of objects in the pool",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE));
}

static void
gst_vaapi_video_pool_init(GstVaapiVideoPool *pool)
{
    GstVaapiVideoPoolPrivate *priv = GST_VAAPI_VIDEO_POOL_GET_PRIVATE(pool);

    pool->priv          = priv;
    priv->display       = NULL;
    priv->used_objects  = NULL;
    priv->caps          = NULL;
    priv->used_count    = 0;
    priv->capacity      = 0;

    g_queue_init(&priv->free_objects);
}

/**
 * gst_vaapi_video_pool_get_display:
 * @pool: a #GstVaapiVideoPool
 *
 * Retrieves the #GstVaapiDisplay the @pool is bound to. The @pool
 * owns the returned object and it shall not be unref'ed.
 *
 * Return value: the #GstVaapiDisplay the @pool is bound to
 */
GstVaapiDisplay *
gst_vaapi_video_pool_get_display(GstVaapiVideoPool *pool)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), NULL);

    return pool->priv->display;
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
    GstVaapiVideoPoolPrivate *priv;
    gpointer object;

    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), NULL);

    priv = pool->priv;
    if (priv->capacity && priv->used_count >= priv->capacity)
        return NULL;

    object = g_queue_pop_head(&priv->free_objects);
    if (!object) {
        object = gst_vaapi_video_pool_alloc_object(pool);
        if (!object)
            return NULL;
    }

    ++priv->used_count;
    priv->used_objects = g_list_prepend(priv->used_objects, object);
    return g_object_ref(object);
}

/**
 * gst_vaapi_video_pool_put_object:
 * @pool: a #GstVaapiVideoPool
 * @object: the object to add back to the pool
 *
 * Pushes the @object back into the pool. The @object shall be
 * obtained from the @pool through gst_vaapi_video_pool_get_object().
 * Calling this function with an arbitrary object yields undefined
 * behaviour.
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
    --priv->used_count;
    priv->used_objects = g_list_delete_link(priv->used_objects, elem);
    g_queue_push_tail(&priv->free_objects, object);
}

/**
 * gst_vaapi_video_pool_add_object:
 * @pool: a #GstVaapiVideoPool
 * @object: the object to add to the pool
 *
 * Adds the @object to the pool. The pool then holds a reference on
 * the @object. This operation does not change the capacity of the
 * pool.
 *
 * Return value: %TRUE on success.
 */
gboolean
gst_vaapi_video_pool_add_object(GstVaapiVideoPool *pool, gpointer object)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), FALSE);
    g_return_val_if_fail(G_IS_OBJECT(object), FALSE);

    g_queue_push_tail(&pool->priv->free_objects, g_object_ref(object));
    return TRUE;
}

/**
 * gst_vaapi_video_pool_add_objects:
 * @pool: a #GstVaapiVideoPool
 * @objects: a #GPtrArray of objects
 *
 * Adds the @objects to the pool. The pool then holds a reference on
 * the @objects. This operation does not change the capacity of the
 * pool and is just a wrapper around gst_vaapi_video_pool_add_object().
 *
 * Return value: %TRUE on success.
 */
gboolean
gst_vaapi_video_pool_add_objects(GstVaapiVideoPool *pool, GPtrArray *objects)
{
    guint i;

    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), FALSE);

    for (i = 0; i < objects->len; i++) {
        gpointer const object = g_ptr_array_index(objects, i);
        if (!gst_vaapi_video_pool_add_object(pool, object))
            return FALSE;
    }
    return TRUE;
}

/**
 * gst_vaapi_video_pool_get_size:
 * @pool: a #GstVaapiVideoPool
 *
 * Returns the number of free objects available in the pool.
 *
 * Return value: number of free objects in the pool
 */
guint
gst_vaapi_video_pool_get_size(GstVaapiVideoPool *pool)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), 0);

    return g_queue_get_length(&pool->priv->free_objects);
}

/**
 * gst_vaapi_video_pool_reserve:
 * @pool: a #GstVaapiVideoPool
 * @n: the number of objects to pre-allocate
 *
 * Pre-allocates up to @n objects in the pool. If @n is less than or
 * equal to the number of free and used objects in the pool, this call
 * has no effect. Otherwise, it is a request for allocation of
 * additional objects.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_video_pool_reserve(GstVaapiVideoPool *pool, guint n)
{
    guint i, num_allocated;

    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), 0);

    num_allocated = gst_vaapi_video_pool_get_size(pool) + pool->priv->used_count;
    if (n < num_allocated)
        return TRUE;

    if ((n -= num_allocated) > pool->priv->capacity)
        n = pool->priv->capacity;

    for (i = num_allocated; i < n; i++) {
        gpointer const object = gst_vaapi_video_pool_alloc_object(pool);
        if (!object)
            return FALSE;
        g_queue_push_tail(&pool->priv->free_objects, object);
    }
    return TRUE;
}

/**
 * gst_vaapi_video_pool_get_capacity:
 * @pool: a #GstVaapiVideoPool
 *
 * Returns the maximum number of objects in the pool. i.e. the maximum
 * number of objects that can be returned by gst_vaapi_video_pool_get_object().
 *
 * Return value: the capacity of the pool
 */
guint
gst_vaapi_video_pool_get_capacity(GstVaapiVideoPool *pool)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), 0);

    return pool->priv->capacity;
}

/**
 * gst_vaapi_video_pool_set_capacity:
 * @pool: a #GstVaapiVideoPool
 * @capacity: the maximal capacity of the pool
 *
 * Sets the maximum number of objects that can be allocated in the pool.
 */
void
gst_vaapi_video_pool_set_capacity(GstVaapiVideoPool *pool, guint capacity)
{
    g_return_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool));

    pool->priv->capacity = capacity;
}
