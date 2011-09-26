/* GStreamer
 * Copyright (C) 2005 David Schleef <ds@schleef.org>
 *
 * gstminiobject.h: Header for GstMiniObject
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
/**
 * SECTION:gstminiobject
 * @short_description: Lightweight base class for the GStreamer object hierarchy
 *
 * #GstMiniObject is a baseclass like #GObject, but has been stripped down of
 * features to be fast and small.
 * It offers sub-classing and ref-counting in the same way as #GObject does.
 * It has no properties and no signal-support though.
 *
 * Last reviewed on 2005-11-23 (0.9.5)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst/gst_private.h"
#include "gst/gstminiobject.h"
#include "gst/gstinfo.h"
#include <gobject/gvaluecollector.h>

#define GST_DISABLE_TRACE

#ifndef GST_DISABLE_TRACE
#include "gsttrace.h"
static GstAllocTrace *_gst_mini_object_trace;
#endif

/* Mutex used for weak referencing */
G_LOCK_DEFINE_STATIC (weak_refs_mutex);

/**
 * gst_mini_object_init:
 * @mini_object: a #GstMiniObject 
 * @type: the #GType of the mini-object to create
 * @size: the size of the data
 *
 * Initializes a mini-object with the desired type and size.
 *
 * MT safe
 *
 * Returns: (transfer full): the new mini-object.
 */
void
gst_mini_object_init (GstMiniObject * mini_object, GType type, gsize size)
{
  mini_object->type = type;
  mini_object->refcount = 1;
  mini_object->flags = 0;
  mini_object->size = size;
  mini_object->n_weak_refs = 0;
  mini_object->weak_refs = NULL;
}

/**
 * gst_mini_object_copy:
 * @mini_object: the mini-object to copy
 *
 * Creates a copy of the mini-object.
 *
 * MT safe
 *
 * Returns: (transfer full): the new mini-object.
 */
GstMiniObject *
gst_mini_object_copy (const GstMiniObject * mini_object)
{
  GstMiniObject *copy;

  g_return_val_if_fail (mini_object != NULL, NULL);

  if (mini_object->copy)
    copy = mini_object->copy (mini_object);
  else
    copy = NULL;

  return copy;
}

/**
 * gst_mini_object_is_writable:
 * @mini_object: the mini-object to check
 *
 * Checks if a mini-object is writable.  A mini-object is writable
 * if the reference count is one and the #GST_MINI_OBJECT_FLAG_READONLY
 * flag is not set.  Modification of a mini-object should only be
 * done after verifying that it is writable.
 *
 * MT safe
 *
 * Returns: TRUE if the object is writable.
 */
gboolean
gst_mini_object_is_writable (const GstMiniObject * mini_object)
{
  g_return_val_if_fail (mini_object != NULL, FALSE);

  return (GST_MINI_OBJECT_REFCOUNT_VALUE (mini_object) == 1);
}

/**
 * gst_mini_object_make_writable:
 * @mini_object: (transfer full): the mini-object to make writable
 *
 * Checks if a mini-object is writable.  If not, a writable copy is made and
 * returned.  This gives away the reference to the original mini object,
 * and returns a reference to the new object.
 *
 * MT safe
 *
 * Returns: (transfer full): a mini-object (possibly the same pointer) that
 *     is writable.
 */
GstMiniObject *
gst_mini_object_make_writable (GstMiniObject * mini_object)
{
  GstMiniObject *ret;

  g_return_val_if_fail (mini_object != NULL, NULL);

  if (gst_mini_object_is_writable (mini_object)) {
    ret = mini_object;
  } else {
    ret = gst_mini_object_copy (mini_object);
    GST_CAT_DEBUG (GST_CAT_PERFORMANCE, "copy %s miniobject %p -> %p",
        g_type_name (GST_MINI_OBJECT_TYPE (mini_object)), mini_object, ret);
    gst_mini_object_unref (mini_object);
  }

  return ret;
}

/**
 * gst_mini_object_ref:
 * @mini_object: the mini-object
 *
 * Increase the reference count of the mini-object.
 *
 * Note that the refcount affects the writeability
 * of @mini-object, see gst_mini_object_is_writable(). It is
 * important to note that keeping additional references to
 * GstMiniObject instances can potentially increase the number
 * of memcpy operations in a pipeline, especially if the miniobject
 * is a #GstBuffer.
 *
 * Returns: (transfer full): the mini-object.
 */
GstMiniObject *
gst_mini_object_ref (GstMiniObject * mini_object)
{
  g_return_val_if_fail (mini_object != NULL, NULL);
  /* we can't assert that the refcount > 0 since the _free functions
   * increments the refcount from 0 to 1 again to allow resurecting
   * the object
   g_return_val_if_fail (mini_object->refcount > 0, NULL);
   */

  GST_CAT_TRACE (GST_CAT_REFCOUNTING, "%p ref %d->%d", mini_object,
      GST_MINI_OBJECT_REFCOUNT_VALUE (mini_object),
      GST_MINI_OBJECT_REFCOUNT_VALUE (mini_object) + 1);

  g_atomic_int_inc (&mini_object->refcount);

  return mini_object;
}

static void
weak_refs_notify (GstMiniObject * obj)
{
  guint i;

  for (i = 0; i < obj->n_weak_refs; i++)
    obj->weak_refs[i].notify (obj->weak_refs[i].data, obj);
  g_free (obj->weak_refs);
}

/**
 * gst_mini_object_unref:
 * @mini_object: the mini-object
 *
 * Decreases the reference count of the mini-object, possibly freeing
 * the mini-object.
 */
void
gst_mini_object_unref (GstMiniObject * mini_object)
{
  g_return_if_fail (mini_object != NULL);
  g_return_if_fail (mini_object->refcount > 0);

  GST_CAT_TRACE (GST_CAT_REFCOUNTING, "%p unref %d->%d",
      mini_object,
      GST_MINI_OBJECT_REFCOUNT_VALUE (mini_object),
      GST_MINI_OBJECT_REFCOUNT_VALUE (mini_object) - 1);

  if (G_UNLIKELY (g_atomic_int_dec_and_test (&mini_object->refcount))) {
    gboolean do_free;

    if (mini_object->dispose)
      do_free = mini_object->dispose (mini_object);
    else
      do_free = TRUE;

    /* if the subclass recycled the object (and returned FALSE) we don't
     * want to free the instance anymore */
    if (G_LIKELY (do_free)) {
      /* The weak reference stack is freed in the notification function */
      if (mini_object->n_weak_refs)
        weak_refs_notify (mini_object);

#ifndef GST_DISABLE_TRACE
      gst_alloc_trace_free (_gst_mini_object_trace, mini_object);
#endif
      if (mini_object->free)
        mini_object->free (mini_object);
    }
  }
}

/**
 * gst_mini_object_replace:
 * @olddata: (inout) (transfer full): pointer to a pointer to a mini-object to
 *     be replaced
 * @newdata: pointer to new mini-object
 *
 * Atomically modifies a pointer to point to a new mini-object.
 * The reference count of @olddata is decreased and the reference count of
 * @newdata is increased.
 *
 * Either @newdata and the value pointed to by @olddata may be NULL.
 *
 * Returns: TRUE if @newdata was different from @olddata
 */
gboolean
gst_mini_object_replace (GstMiniObject ** olddata, GstMiniObject * newdata)
{
  GstMiniObject *olddata_val;

  g_return_val_if_fail (olddata != NULL, FALSE);

  GST_CAT_TRACE (GST_CAT_REFCOUNTING, "replace %p (%d) with %p (%d)",
      *olddata, *olddata ? (*olddata)->refcount : 0,
      newdata, newdata ? newdata->refcount : 0);

  olddata_val = g_atomic_pointer_get ((gpointer *) olddata);

  if (G_UNLIKELY (olddata_val == newdata))
    return FALSE;

  if (newdata)
    gst_mini_object_ref (newdata);

  while (G_UNLIKELY (!g_atomic_pointer_compare_and_exchange ((gpointer *)
              olddata, olddata_val, newdata))) {
    olddata_val = g_atomic_pointer_get ((gpointer *) olddata);
    if (G_UNLIKELY (olddata_val == newdata))
      break;
  }

  if (olddata_val)
    gst_mini_object_unref (olddata_val);

  return olddata_val != newdata;
}

/**
 * gst_mini_object_steal:
 * @olddata: (inout) (transfer full): pointer to a pointer to a mini-object to
 *     be stolen
 *
 * Replace the current #GstMiniObject pointer to by @olddata with NULL and
 * return the old value.
 *
 * Returns: the #GstMiniObject at @oldata
 */
GstMiniObject *
gst_mini_object_steal (GstMiniObject ** olddata)
{
  GstMiniObject *olddata_val;

  g_return_val_if_fail (olddata != NULL, NULL);

  GST_CAT_TRACE (GST_CAT_REFCOUNTING, "steal %p (%d)",
      *olddata, *olddata ? (*olddata)->refcount : 0);

  do {
    olddata_val = g_atomic_pointer_get ((gpointer *) olddata);
    if (olddata_val == NULL)
      break;
  } while (G_UNLIKELY (!g_atomic_pointer_compare_and_exchange ((gpointer *)
              olddata, olddata_val, NULL)));

  return olddata_val;
}

/**
 * gst_mini_object_take:
 * @olddata: (inout) (transfer full): pointer to a pointer to a mini-object to
 *     be replaced
 * @newdata: pointer to new mini-object
 *
 * Modifies a pointer to point to a new mini-object. The modification
 * is done atomically. This version is similar to gst_mini_object_replace()
 * except that it does not increase the refcount of @newdata and thus
 * takes ownership of @newdata.
 *
 * Either @newdata and the value pointed to by @olddata may be NULL.
 *
 * Returns: TRUE if @newdata was different from @olddata
 */
gboolean
gst_mini_object_take (GstMiniObject ** olddata, GstMiniObject * newdata)
{
  GstMiniObject *olddata_val;

  g_return_val_if_fail (olddata != NULL, FALSE);

  GST_CAT_TRACE (GST_CAT_REFCOUNTING, "take %p (%d) with %p (%d)",
      *olddata, *olddata ? (*olddata)->refcount : 0,
      newdata, newdata ? newdata->refcount : 0);

  do {
    olddata_val = g_atomic_pointer_get ((gpointer *) olddata);
    if (G_UNLIKELY (olddata_val == newdata))
      break;
  } while (G_UNLIKELY (!g_atomic_pointer_compare_and_exchange ((gpointer *)
              olddata, olddata_val, newdata)));

  if (olddata_val)
    gst_mini_object_unref (olddata_val);

  return olddata_val != newdata;
}

/**
 * gst_mini_object_weak_ref: (skip)
 * @object: #GstMiniObject to reference weakly
 * @notify: callback to invoke before the mini object is freed
 * @data: extra data to pass to notify
 *
 * Adds a weak reference callback to a mini object. Weak references are
 * used for notification when a mini object is finalized. They are called
 * "weak references" because they allow you to safely hold a pointer
 * to the mini object without calling gst_mini_object_ref()
 * (gst_mini_object_ref() adds a strong reference, that is, forces the object
 * to stay alive).
 *
 * Since: 0.10.35
 */
void
gst_mini_object_weak_ref (GstMiniObject * object,
    GstMiniObjectWeakNotify notify, gpointer data)
{
  guint i;

  g_return_if_fail (object != NULL);
  g_return_if_fail (notify != NULL);
  g_return_if_fail (GST_MINI_OBJECT_REFCOUNT_VALUE (object) >= 1);

  G_LOCK (weak_refs_mutex);

  if (object->n_weak_refs) {
    /* Don't add the weak reference if it already exists. */
    for (i = 0; i < object->n_weak_refs; i++) {
      if (object->weak_refs[i].notify == notify &&
          object->weak_refs[i].data == data) {
        g_warning ("%s: Attempt to re-add existing weak ref %p(%p) failed.",
            G_STRFUNC, notify, data);
        goto found;
      }
    }

    i = object->n_weak_refs++;
    object->weak_refs =
        g_realloc (object->weak_refs, sizeof (object->weak_refs[0]) * i);
  } else {
    object->weak_refs = g_malloc0 (sizeof (object->weak_refs[0]));
    object->n_weak_refs = 1;
    i = 0;
  }
  object->weak_refs[i].notify = notify;
  object->weak_refs[i].data = data;
found:
  G_UNLOCK (weak_refs_mutex);
}

/**
 * gst_mini_object_weak_unref: (skip)
 * @object: #GstMiniObject to remove a weak reference from
 * @notify: callback to search for
 * @data: data to search for
 *
 * Removes a weak reference callback to a mini object.
 *
 * Since: 0.10.35
 */
void
gst_mini_object_weak_unref (GstMiniObject * object,
    GstMiniObjectWeakNotify notify, gpointer data)
{
  gboolean found_one = FALSE;

  g_return_if_fail (object != NULL);
  g_return_if_fail (notify != NULL);

  G_LOCK (weak_refs_mutex);

  if (object->n_weak_refs) {
    guint i;

    for (i = 0; i < object->n_weak_refs; i++)
      if (object->weak_refs[i].notify == notify &&
          object->weak_refs[i].data == data) {
        found_one = TRUE;
        object->n_weak_refs -= 1;
        if (i != object->n_weak_refs)
          object->weak_refs[i] = object->weak_refs[object->n_weak_refs];

        break;
      }
  }
  G_UNLOCK (weak_refs_mutex);
  if (!found_one)
    g_warning ("%s: couldn't find weak ref %p(%p)", G_STRFUNC, notify, data);
}
