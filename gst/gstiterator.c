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
    GstIteratorResyncFunction resync, GstIteratorFreeFunction free)
{
  it->lock = lock;
  it->master_cookie = master_cookie;
  it->cookie = *master_cookie;
  it->next = next;
  it->resync = resync;
  it->free = free;
}

/**
 * gst_iterator_new:
 * @size: the size of the iterator structure
 * @lock: pointer to a #GMutex.
 * @master_cookie: pointer to a guint32 to protect the iterated object.
 * @next: function to get next item
 * @resync: function to resync the iterator
 * @free: function to free the iterator
 *
 * Create a new iterator. This function is mainly used for objects
 * implementing the next/resync/free function to iterate a data structure.
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
    GstIteratorResyncFunction resync, GstIteratorFreeFunction free)
{
  GstIterator *result;

  g_return_val_if_fail (size >= sizeof (GstIterator), NULL);
  g_return_val_if_fail (master_cookie != NULL, NULL);
  g_return_val_if_fail (next != NULL, NULL);
  g_return_val_if_fail (resync != NULL, NULL);
  g_return_val_if_fail (free != NULL, NULL);

  result = g_malloc (size);
  gst_iterator_init (result, lock, master_cookie, next, resync, free);

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
  GstIteratorRefFunction reffunc;
  GstIteratorUnrefFunction unreffunc;
  GstIteratorDisposeFunction freefunc;
} GstListIterator;

static GstIteratorResult
gst_list_iterator_next (GstListIterator * it, gpointer * elem)
{
  if (it->list == NULL)
    return GST_ITERATOR_DONE;

  *elem = it->list->data;
  if (it->reffunc) {
    it->reffunc (*elem);
  }
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
 * @ref: function to ref each item
 * @unref: function to unref each item
 * @free: function to free the owner of the list
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
    GstIteratorRefFunction ref,
    GstIteratorUnrefFunction unref, GstIteratorDisposeFunction free)
{
  GstListIterator *result;

  /* no need to lock, nothing can change here */
  result = (GstListIterator *) gst_iterator_new (sizeof (GstListIterator),
      lock,
      master_cookie,
      (GstIteratorNextFunction) gst_list_iterator_next,
      (GstIteratorResyncFunction) gst_list_iterator_resync,
      (GstIteratorFreeFunction) gst_list_iterator_free);

  result->owner = owner;
  result->orig = list;
  result->list = *list;
  result->reffunc = ref;
  result->unreffunc = unref;
  result->freefunc = free;

  return GST_ITERATOR (result);
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

  if (G_LIKELY (it->lock))
    g_mutex_lock (it->lock);
  if (G_UNLIKELY (*it->master_cookie != it->cookie)) {
    result = GST_ITERATOR_RESYNC;
    goto done;
  }

  result = it->next (it, elem);

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

  it->free (it);
}

typedef struct _GstIteratorFilter
{
  GstIterator iterator;
  GstIterator *slave;

  GCompareFunc func;
  gpointer user_data;

  gboolean compare;
  gboolean first;
  gboolean found;

} GstIteratorFilter;

/* this function can iterate in 3 modes:
 * filter, foreach and find_custom.
 */
static GstIteratorResult
filter_next (GstIteratorFilter * it, gpointer * elem)
{
  GstIteratorResult result;
  gboolean done = FALSE;

  *elem = NULL;

  if (G_UNLIKELY (it->found))
    return GST_ITERATOR_DONE;

  while (G_LIKELY (!done)) {
    gpointer item;

    result = gst_iterator_next (it->slave, &item);
    switch (result) {
      case GST_ITERATOR_OK:
        if (G_LIKELY (GST_ITERATOR (it)->lock))
          g_mutex_unlock (GST_ITERATOR (it)->lock);
        if (it->compare) {
          if (it->func (item, it->user_data) == 0) {
            *elem = item;
            done = TRUE;
            if (it->first)
              it->found = TRUE;
          }
        } else {
          it->func (item, it->user_data);
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
  it->found = FALSE;
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
gst_iterator_filter (GstIterator * it, gpointer user_data, GCompareFunc func)
{
  GstIteratorFilter *result;

  g_return_val_if_fail (it != NULL, NULL);
  g_return_val_if_fail (func != NULL, NULL);

  result = (GstIteratorFilter *) gst_iterator_new (sizeof (GstIteratorFilter),
      it->lock, it->master_cookie,
      (GstIteratorNextFunction) filter_next,
      (GstIteratorResyncFunction) filter_resync,
      (GstIteratorFreeFunction) filter_free);
  it->lock = NULL;
  result->func = func;
  result->user_data = user_data;
  result->slave = it;
  result->compare = TRUE;
  result->first = FALSE;
  result->found = FALSE;

  return GST_ITERATOR (result);
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
 * MT safe.
 */
void
gst_iterator_foreach (GstIterator * it, GFunc function, gpointer user_data)
{
  GstIteratorFilter filter;
  gpointer dummy;

  g_return_if_fail (it != NULL);
  g_return_if_fail (function != NULL);

  gst_iterator_init (GST_ITERATOR (&filter),
      it->lock, it->master_cookie,
      (GstIteratorNextFunction) filter_next,
      (GstIteratorResyncFunction) filter_resync,
      (GstIteratorFreeFunction) filter_uninit);
  it->lock = NULL;
  filter.func = (GCompareFunc) function;
  filter.user_data = user_data;
  filter.slave = it;
  filter.compare = FALSE;
  filter.first = FALSE;
  filter.found = FALSE;
  gst_iterator_next (GST_ITERATOR (&filter), &dummy);
  gst_iterator_free (GST_ITERATOR (&filter));
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
 * Returns: The element in the iterator that matches the compare
 * function or NULL when no element matched.
 *
 * MT safe.
 */
gpointer
gst_iterator_find_custom (GstIterator * it, gpointer user_data,
    GCompareFunc func)
{
  GstIteratorFilter filter;
  gpointer result = NULL;

  g_return_val_if_fail (it != NULL, NULL);
  g_return_val_if_fail (func != NULL, NULL);

  gst_iterator_init (GST_ITERATOR (&filter),
      it->lock, it->master_cookie,
      (GstIteratorNextFunction) filter_next,
      (GstIteratorResyncFunction) filter_resync,
      (GstIteratorFreeFunction) filter_uninit);
  it->lock = NULL;
  filter.func = func;
  filter.user_data = user_data;
  filter.slave = it;
  filter.compare = TRUE;
  filter.first = TRUE;
  filter.found = FALSE;

  gst_iterator_next (GST_ITERATOR (&filter), &result);
  gst_iterator_free (GST_ITERATOR (&filter));

  return result;
}
