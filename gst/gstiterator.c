/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstiterator.h: Base class for iterating lists.
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

GstIteratorResult
gst_iterator_next (GstIterator * it, gpointer * elem)
{
  GstIteratorResult result;

  g_return_val_if_fail (it != NULL, GST_ITERATOR_ERROR);
  g_return_val_if_fail (elem != NULL, GST_ITERATOR_ERROR);

  if (it->lock)
    g_mutex_lock (it->lock);
  if (*it->master_cookie != it->cookie) {
    result = GST_ITERATOR_RESYNC;
    goto done;
  }

  result = it->next (it, elem);

done:
  if (it->lock)
    g_mutex_unlock (it->lock);

  return result;
}

void
gst_iterator_resync (GstIterator * it)
{
  g_return_if_fail (it != NULL);

  if (it->lock)
    g_mutex_lock (it->lock);
  it->resync (it);
  it->cookie = *it->master_cookie;
  if (it->lock)
    g_mutex_unlock (it->lock);
}

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

static GstIteratorResult
filter_next (GstIteratorFilter * it, gpointer * elem)
{
  GstIteratorResult result;
  gboolean done = FALSE;

  *elem = NULL;

  if (it->found)
    return GST_ITERATOR_DONE;

  while (!done) {
    gpointer item;

    result = gst_iterator_next (it->slave, &item);
    switch (result) {
      case GST_ITERATOR_OK:
        if (GST_ITERATOR (it)->lock)
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
        if (GST_ITERATOR (it)->lock)
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
