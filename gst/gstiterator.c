/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstiterator.h: Base class for iterating datastructures.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gst_private.h"
#include <gst/gstiterator.h>

static void
gst_iterator_init (GstIterator * it,
    GMutex * lock,
    guint32 * master_cookie,
    GstIteratorNextFunction next,
    GstIteratorItemFunction item,
    GstIteratorResyncFunction resync, GstIteratorFreeFunction free)
{
  it->lock = lock;
  it->master_cookie = master_cookie;
  it->cookie = *master_cookie;
  it->next = next;
  it->item = item;
  it->resync = resync;
  it->free = free;
  it->pushed = NULL;
}

/**
 * gst_iterator_new:
 * @size: the size of the iterator structure
 * @lock: pointer to a #GMutex.
 * @master_cookie: pointer to a guint32 to protect the iterated object.
 * @next: function to get next item
 * @item: function to call on each item retrieved
 * @resync: function to resync the iterator
 * @free: function to free the iterator
 *
 * Create a new iterator. This function is mainly used for objects
 * implementing the next/resync/free function to iterate a data structure.
 *
 * For each item retrieved, the @item function is called with the lock
 * held. The @free function is called when the iterator is freed.
 *
 * Returns: the new #GstIterator.
 *
 * MT safe.
 */
GstIterator *
gst_iterator_new (guint size,
    GMutex * lock,
    guint32 * master_cookie,
    GstIteratorNextFunction next,
    GstIteratorItemFunction item,
    GstIteratorResyncFunction resync, GstIteratorFreeFunction free)
{
  GstIterator *result;

  g_return_val_if_fail (size >= sizeof (GstIterator), NULL);
  g_return_val_if_fail (master_cookie != NULL, NULL);
  g_return_val_if_fail (next != NULL, NULL);
  g_return_val_if_fail (resync != NULL, NULL);
  g_return_val_if_fail (free != NULL, NULL);

  result = g_malloc (size);
  gst_iterator_init (result, lock, master_cookie, next, item, resync, free);

  return result;
}

/*
 * list iterator
 */
typedef struct _GstListIterator
{
  GstIterator iterator;
  gpointer owner;
  GList **orig;
  GList *list;                  /* pointer in list */
  GstIteratorDisposeFunction freefunc;
} GstListIterator;

static GstIteratorResult
gst_list_iterator_next (GstListIterator * it, gpointer * elem)
{
  if (it->list == NULL)
    return GST_ITERATOR_DONE;

  *elem = it->list->data;
  it->list = g_list_next (it->list);

  return GST_ITERATOR_OK;
}

static void
gst_list_iterator_resync (GstListIterator * it)
{
  it->list = *it->orig;
}

static void
gst_list_iterator_free (GstListIterator * it)
{
  if (it->freefunc) {
    it->freefunc (it->owner);
  }
  g_free (it);
}

/**
 * gst_iterator_new_list:
 * @lock: pointer to a #GMutex protecting the list.
 * @master_cookie: pointer to a guint32 to protect the list.
 * @list: pointer to the list
 * @owner: object owning the list
 * @item: function to call for each item
 * @free: function to call when the iterator is freed
 *
 * Create a new iterator designed for iterating @list.
 *
 * Returns: the new #GstIterator for @list.
 *
 * MT safe.
 */
GstIterator *
gst_iterator_new_list (GMutex * lock,
    guint32 * master_cookie,
    GList ** list,
    gpointer owner,
    GstIteratorItemFunction item, GstIteratorDisposeFunction free)
{
  GstListIterator *result;

  /* no need to lock, nothing can change here */
  result = (GstListIterator *) gst_iterator_new (sizeof (GstListIterator),
      lock,
      master_cookie,
      (GstIteratorNextFunction) gst_list_iterator_next,
      (GstIteratorItemFunction) item,
      (GstIteratorResyncFunction) gst_list_iterator_resync,
      (GstIteratorFreeFunction) gst_list_iterator_free);

  result->owner = owner;
  result->orig = list;
  result->list = *list;
  result->freefunc = free;

  return GST_ITERATOR (result);
}

static void
gst_iterator_pop (GstIterator * it)
{
  if (it->pushed) {
    gst_iterator_free (it->pushed);
    it->pushed = NULL;
  }
}

/**
 * gst_iterator_next:
 * @it: The #GstIterator to iterate
 * @elem: pointer to hold next element
 *
 * Get the next item from the iterator.
 * 
 * Returns: The result of the iteration.
 *
 * MT safe.
 */
GstIteratorResult
gst_iterator_next (GstIterator * it, gpointer * elem)
{
  GstIteratorResult result;

  g_return_val_if_fail (it != NULL, GST_ITERATOR_ERROR);
  g_return_val_if_fail (elem != NULL, GST_ITERATOR_ERROR);

restart:
  if (it->pushed) {
    result = gst_iterator_next (it->pushed, elem);
    if (result == GST_ITERATOR_DONE) {
      /* we are done with this iterator, pop it and
       * fallthrough iterating the main iterator again. */
      gst_iterator_pop (it);
    } else {
      return result;
    }
  }

  if (G_LIKELY (it->lock))
    g_mutex_lock (it->lock);

  if (G_UNLIKELY (*it->master_cookie != it->cookie)) {
    result = GST_ITERATOR_RESYNC;
    goto done;
  }

  result = it->next (it, elem);
  if (result == GST_ITERATOR_OK && it->item) {
    GstIteratorItem itemres;

    itemres = it->item (it, *elem);
    switch (itemres) {
      case GST_ITERATOR_ITEM_SKIP:
        if (G_LIKELY (it->lock))
          g_mutex_unlock (it->lock);
        goto restart;
      case GST_ITERATOR_ITEM_END:
        result = GST_ITERATOR_DONE;
        break;
      case GST_ITERATOR_ITEM_PASS:
        break;
    }
  }

done:
  if (G_LIKELY (it->lock))
    g_mutex_unlock (it->lock);

  return result;
}

/**
 * gst_iterator_resync:
 * @it: The #GstIterator to resync
 *
 * Resync the iterator. this function is mostly called
 * after #gst_iterator_next() returned #GST_ITERATOR_RESYNC.
 * 
 * MT safe.
 */
void
gst_iterator_resync (GstIterator * it)
{
  g_return_if_fail (it != NULL);

  gst_iterator_pop (it);

  if (G_LIKELY (it->lock))
    g_mutex_lock (it->lock);
  it->resync (it);
  it->cookie = *it->master_cookie;
  if (G_LIKELY (it->lock))
    g_mutex_unlock (it->lock);
}

/**
 * gst_iterator_free:
 * @it: The #GstIterator to free
 *
 * Free the iterator.
 *
 * MT safe.
 */
void
gst_iterator_free (GstIterator * it)
{
  g_return_if_fail (it != NULL);

  gst_iterator_pop (it);

  it->free (it);
}

/**
 * gst_iterator_push:
 * @it: The #GstIterator to use
 * @other: The #GstIterator to push
 *
 * Pushes @other iterator onto @it. All calls performed on @it are
 * forwarded tot @other. If @other returns #GST_ITERATOR_DONE, it is
 * popped again and calls are handled by @it again.
 *
 * This function is mainly used by objects implementing the iterator
 * next function to recurse into substructures.
 * 
 * MT safe.
 */
void
gst_iterator_push (GstIterator * it, GstIterator * other)
{
  g_return_if_fail (it != NULL);
  g_return_if_fail (other != NULL);

  it->pushed = other;
}

typedef struct _GstIteratorFilter
{
  GstIterator iterator;
  GstIterator *slave;

  GCompareFunc func;
  gpointer user_data;
} GstIteratorFilter;

static GstIteratorResult
filter_next (GstIteratorFilter * it, gpointer * elem)
{
  GstIteratorResult result = GST_ITERATOR_ERROR;
  gboolean done = FALSE;

  *elem = NULL;

  while (G_LIKELY (!done)) {
    gpointer item;

    result = gst_iterator_next (it->slave, &item);
    switch (result) {
      case GST_ITERATOR_OK:
        if (G_LIKELY (GST_ITERATOR (it)->lock))
          g_mutex_unlock (GST_ITERATOR (it)->lock);
        if (it->func (item, it->user_data) == 0) {
          *elem = item;
          done = TRUE;
        }
        if (G_LIKELY (GST_ITERATOR (it)->lock))
          g_mutex_lock (GST_ITERATOR (it)->lock);
        break;
      case GST_ITERATOR_RESYNC:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }
  return result;
}

static void
filter_resync (GstIteratorFilter * it)
{
  gst_iterator_resync (it->slave);
}

static void
filter_uninit (GstIteratorFilter * it)
{
  it->slave->lock = GST_ITERATOR (it)->lock;
}

static void
filter_free (GstIteratorFilter * it)
{
  filter_uninit (it);
  gst_iterator_free (it->slave);
  g_free (it);
}

/**
 * gst_iterator_filter:
 * @it: The #GstIterator to filter
 * @user_data: user data passed to the compare function
 * @func: the compare function to select elements
 *
 * Create a new iterator from an existing iterator. The new iterator
 * will only return those elements that match the given compare function.
 * The GCompareFunc should return 0 for elements that should be included
 * in the iterator.
 *
 * When this iterator is freed, @it will also be freed.
 *
 * Returns: a new #GstIterator.
 * 
 * MT safe.
 */
GstIterator *
gst_iterator_filter (GstIterator * it, GCompareFunc func, gpointer user_data)
{
  GstIteratorFilter *result;

  g_return_val_if_fail (it != NULL, NULL);
  g_return_val_if_fail (func != NULL, NULL);

  result = (GstIteratorFilter *) gst_iterator_new (sizeof (GstIteratorFilter),
      it->lock, it->master_cookie,
      (GstIteratorNextFunction) filter_next,
      (GstIteratorItemFunction) NULL,
      (GstIteratorResyncFunction) filter_resync,
      (GstIteratorFreeFunction) filter_free);
  it->lock = NULL;
  result->func = func;
  result->user_data = user_data;
  result->slave = it;

  return GST_ITERATOR (result);
}

/**
 * gst_iterator_fold:
 * @iter: The #GstIterator to fold over
 * @func: the fold function
 * @ret: the seed value passed to the fold function
 * @user_data: user data passed to the fold function
 *
 * Folds @func over the elements of @iter. That is to say, @proc will be called
 * as @proc (object, @ret, @user_data) for each object in @iter. The normal use
 * of this procedure is to accumulate the results of operating on the objects in
 * @ret.
 *
 * This procedure can be used (and is used internally) to implement the foreach
 * and find_custom operations.
 *
 * The fold will proceed as long as @func returns TRUE. When the iterator has no
 * more arguments, GST_ITERATOR_DONE will be returned. If @func returns FALSE,
 * the fold will stop, and GST_ITERATOR_OK will be returned. Errors or resyncs
 * will cause fold to return GST_ITERATOR_ERROR or GST_ITERATOR_RESYNC as
 * appropriate.
 *
 * The iterator will not be freed.
 *
 * Returns: A #GstIteratorResult, as described above.
 * 
 * MT safe.
 */
GstIteratorResult
gst_iterator_fold (GstIterator * iter, GstIteratorFoldFunction func,
    GValue * ret, gpointer user_data)
{
  gpointer item;
  GstIteratorResult result;

  while (1) {
    result = gst_iterator_next (iter, &item);
    switch (result) {
      case GST_ITERATOR_OK:
        /* fixme: is there a way to ref/unref items? */
        if (!func (item, ret, user_data))
          goto fold_done;
        else
          break;
      case GST_ITERATOR_RESYNC:
      case GST_ITERATOR_ERROR:
        goto fold_done;
      case GST_ITERATOR_DONE:
        goto fold_done;
    }
  }

fold_done:
  return result;
}

typedef struct
{
  GFunc func;
  gpointer user_data;
} ForeachFoldData;

static gboolean
foreach_fold_func (gpointer item, GValue * unused, ForeachFoldData * data)
{
  data->func (item, data->user_data);
  return TRUE;
}

/**
 * gst_iterator_foreach:
 * @it: The #GstIterator to iterate
 * @function: the function to call for each element.
 * @user_data: user data passed to the function
 *
 * Iterate over all element of @it and call the given function for
 * each element.
 *
 * Returns: the result call to gst_iterator_fold(). The iterator will not be
 * freed.
 *
 * MT safe.
 */
GstIteratorResult
gst_iterator_foreach (GstIterator * iter, GFunc func, gpointer user_data)
{
  ForeachFoldData data;

  data.func = func;
  data.user_data = user_data;

  return gst_iterator_fold (iter, (GstIteratorFoldFunction) foreach_fold_func,
      NULL, &data);
}

typedef struct
{
  GCompareFunc func;
  gpointer user_data;
} FindCustomFoldData;

static gboolean
find_custom_fold_func (gpointer item, GValue * ret, FindCustomFoldData * data)
{
  if (data->func (item, data->user_data) == 0) {
    g_value_set_pointer (ret, item);
    return FALSE;
  } else {
    return TRUE;
  }
}

/**
 * gst_iterator_find_custom:
 * @it: The #GstIterator to iterate
 * @user_data: user data passed to the compare function
 * @func: the compare function to use
 *
 * Find the first element in @it that matches the compare function.
 * The compare function should return 0 when the element is found.
 *
 * The iterator will not be freed.
 *
 * Returns: The element in the iterator that matches the compare
 * function or NULL when no element matched.
 *
 * MT safe.
 */
gpointer
gst_iterator_find_custom (GstIterator * iter, GCompareFunc func,
    gpointer user_data)
{
  GValue ret = { 0, };
  GstIteratorResult res;
  FindCustomFoldData data;

  g_value_init (&ret, G_TYPE_POINTER);
  data.func = func;
  data.user_data = user_data;

  res =
      gst_iterator_fold (iter, (GstIteratorFoldFunction) find_custom_fold_func,
      &ret, &data);

  /* no need to unset, it's just a pointer */
  return g_value_get_pointer (&ret);
}
