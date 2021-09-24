/*
 *  gstvaapivideopool.c - Video object pool abstraction
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
 * SECTION:gstvaapivideopool
 * @short_description: Video object pool abstraction
 */

#include "sysdeps.h"
#include "gstvaapivideopool.h"
#include "gstvaapivideopool_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_VIDEO_POOL_GET_CLASS(obj) \
  gst_vaapi_video_pool_get_class (GST_VAAPI_VIDEO_POOL (obj))

static inline const GstVaapiVideoPoolClass *
gst_vaapi_video_pool_get_class (GstVaapiVideoPool * pool)
{
  return GST_VAAPI_VIDEO_POOL_CLASS (GST_VAAPI_MINI_OBJECT_GET_CLASS (pool));
}

static inline gpointer
gst_vaapi_video_pool_alloc_object (GstVaapiVideoPool * pool)
{
  return GST_VAAPI_VIDEO_POOL_GET_CLASS (pool)->alloc_object (pool);
}

void
gst_vaapi_video_pool_init (GstVaapiVideoPool * pool, GstVaapiDisplay * display,
    GstVaapiVideoPoolObjectType object_type)
{
  pool->object_type = object_type;
  pool->display = gst_object_ref (display);
  pool->used_objects = NULL;
  pool->used_count = 0;
  pool->capacity = 0;

  g_queue_init (&pool->free_objects);
  g_mutex_init (&pool->mutex);
}

void
gst_vaapi_video_pool_finalize (GstVaapiVideoPool * pool)
{
  g_list_free_full (pool->used_objects, (GDestroyNotify) gst_mini_object_unref);
  g_queue_foreach (&pool->free_objects, (GFunc) gst_mini_object_unref, NULL);
  g_queue_clear (&pool->free_objects);
  gst_vaapi_display_replace (&pool->display, NULL);
  g_mutex_clear (&pool->mutex);
}

/**
 * gst_vaapi_video_pool_ref:
 * @pool: a #GstVaapiVideoPool
 *
 * Atomically increases the reference count of the given @pool by one.
 *
 * Returns: The same @pool argument
 */
GstVaapiVideoPool *
gst_vaapi_video_pool_ref (GstVaapiVideoPool * pool)
{
  return (GstVaapiVideoPool *)
      gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT (pool));
}

/**
 * gst_vaapi_video_pool_unref:
 * @pool: a #GstVaapiVideoPool
 *
 * Atomically decreases the reference count of the @pool by one. If
 * the reference count reaches zero, the pool will be free'd.
 */
void
gst_vaapi_video_pool_unref (GstVaapiVideoPool * pool)
{
  gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (pool));
}

/**
 * gst_vaapi_video_pool_replace:
 * @old_pool_ptr: a pointer to a #GstVaapiVideoPool
 * @new_pool: a #GstVaapiVideoPool
 *
 * Atomically replaces the pool pool held in @old_pool_ptr with
 * @new_pool. This means that @old_pool_ptr shall reference a valid
 * pool. However, @new_pool can be NULL.
 */
void
gst_vaapi_video_pool_replace (GstVaapiVideoPool ** old_pool_ptr,
    GstVaapiVideoPool * new_pool)
{
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) (old_pool_ptr),
      GST_VAAPI_MINI_OBJECT (new_pool));
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
gst_vaapi_video_pool_get_display (GstVaapiVideoPool * pool)
{
  g_return_val_if_fail (pool != NULL, NULL);

  return pool->display;
}

/**
 * gst_vaapi_video_pool_get_object_type:
 * @pool: a #GstVaapiVideoPool
 *
 * Retrieves the type of objects the video @pool supports.
 *
 * Return value: the #GstVaapiVideoPoolObjectType of the underlying pool
 *   objects
 */
GstVaapiVideoPoolObjectType
gst_vaapi_video_pool_get_object_type (GstVaapiVideoPool * pool)
{
  g_return_val_if_fail (pool != NULL, (GstVaapiVideoPoolObjectType) 0);

  return pool->object_type;
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
static gpointer
gst_vaapi_video_pool_get_object_unlocked (GstVaapiVideoPool * pool)
{
  gpointer object;

  if (pool->capacity && pool->used_count >= pool->capacity)
    return NULL;

  object = g_queue_pop_head (&pool->free_objects);
  if (!object) {
    g_mutex_unlock (&pool->mutex);
    object = gst_vaapi_video_pool_alloc_object (pool);
    g_mutex_lock (&pool->mutex);
    if (!object)
      return NULL;

    /* Others already allocated a new one before us during we
       release the mutex */
    if (pool->capacity && pool->used_count >= pool->capacity) {
      gst_mini_object_unref (object);
      return NULL;
    }
  }

  ++pool->used_count;
  pool->used_objects = g_list_prepend (pool->used_objects, object);
  return gst_mini_object_ref (object);
}

gpointer
gst_vaapi_video_pool_get_object (GstVaapiVideoPool * pool)
{
  gpointer object;

  g_return_val_if_fail (pool != NULL, NULL);

  g_mutex_lock (&pool->mutex);
  object = gst_vaapi_video_pool_get_object_unlocked (pool);
  g_mutex_unlock (&pool->mutex);
  return object;
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
static void
gst_vaapi_video_pool_put_object_unlocked (GstVaapiVideoPool * pool,
    gpointer object)
{
  GList *elem;

  elem = g_list_find (pool->used_objects, object);
  if (!elem)
    return;

  gst_mini_object_unref (object);
  --pool->used_count;
  pool->used_objects = g_list_delete_link (pool->used_objects, elem);
  g_queue_push_tail (&pool->free_objects, object);
}

void
gst_vaapi_video_pool_put_object (GstVaapiVideoPool * pool, gpointer object)
{
  g_return_if_fail (pool != NULL);
  g_return_if_fail (object != NULL);

  g_mutex_lock (&pool->mutex);
  gst_vaapi_video_pool_put_object_unlocked (pool, object);
  g_mutex_unlock (&pool->mutex);
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
static inline gboolean
gst_vaapi_video_pool_add_object_unlocked (GstVaapiVideoPool * pool,
    gpointer object)
{
  g_queue_push_tail (&pool->free_objects, gst_mini_object_ref (object));
  return TRUE;
}

gboolean
gst_vaapi_video_pool_add_object (GstVaapiVideoPool * pool, gpointer object)
{
  gboolean success;

  g_return_val_if_fail (pool != NULL, FALSE);
  g_return_val_if_fail (object != NULL, FALSE);

  g_mutex_lock (&pool->mutex);
  success = gst_vaapi_video_pool_add_object_unlocked (pool, object);
  g_mutex_unlock (&pool->mutex);
  return success;
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
static gboolean
gst_vaapi_video_pool_add_objects_unlocked (GstVaapiVideoPool * pool,
    GPtrArray * objects)
{
  guint i;

  for (i = 0; i < objects->len; i++) {
    gpointer const object = g_ptr_array_index (objects, i);
    if (!gst_vaapi_video_pool_add_object_unlocked (pool, object))
      return FALSE;
  }
  return TRUE;
}

gboolean
gst_vaapi_video_pool_add_objects (GstVaapiVideoPool * pool, GPtrArray * objects)
{
  gboolean success;

  g_return_val_if_fail (pool != NULL, FALSE);

  g_mutex_lock (&pool->mutex);
  success = gst_vaapi_video_pool_add_objects_unlocked (pool, objects);
  g_mutex_unlock (&pool->mutex);
  return success;
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
gst_vaapi_video_pool_get_size (GstVaapiVideoPool * pool)
{
  guint size;

  g_return_val_if_fail (pool != NULL, 0);

  g_mutex_lock (&pool->mutex);
  size = g_queue_get_length (&pool->free_objects);
  g_mutex_unlock (&pool->mutex);
  return size;
}

static gboolean
gst_vaapi_video_pool_reserve_unlocked (GstVaapiVideoPool * pool, guint n)
{
  guint i, num_allocated;

  num_allocated = g_queue_get_length (&pool->free_objects) + pool->used_count;
  if (n <= num_allocated)
    return TRUE;

  n = MIN (n, pool->capacity);
  for (i = num_allocated; i < n; i++) {
    gpointer object;

    g_mutex_unlock (&pool->mutex);
    object = gst_vaapi_video_pool_alloc_object (pool);
    g_mutex_lock (&pool->mutex);
    if (!object)
      return FALSE;
    g_queue_push_tail (&pool->free_objects, object);
  }
  return TRUE;
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
gst_vaapi_video_pool_reserve (GstVaapiVideoPool * pool, guint n)
{
  gboolean success;

  g_return_val_if_fail (pool != NULL, 0);

  g_mutex_lock (&pool->mutex);
  success = gst_vaapi_video_pool_reserve_unlocked (pool, n);
  g_mutex_unlock (&pool->mutex);
  return success;
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
gst_vaapi_video_pool_get_capacity (GstVaapiVideoPool * pool)
{
  guint capacity;

  g_return_val_if_fail (pool != NULL, 0);

  g_mutex_lock (&pool->mutex);
  capacity = pool->capacity;
  g_mutex_unlock (&pool->mutex);

  return capacity;
}

/**
 * gst_vaapi_video_pool_set_capacity:
 * @pool: a #GstVaapiVideoPool
 * @capacity: the maximal capacity of the pool
 *
 * Sets the maximum number of objects that can be allocated in the pool.
 */
void
gst_vaapi_video_pool_set_capacity (GstVaapiVideoPool * pool, guint capacity)
{
  g_return_if_fail (pool != NULL);

  g_mutex_lock (&pool->mutex);
  pool->capacity = capacity;
  g_mutex_unlock (&pool->mutex);
}
