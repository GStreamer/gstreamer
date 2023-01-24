/*
 *  gstvaapiminiobject.c - A lightweight reference counted object
 *
 *  Copyright (C) 2012-2014 Intel Corporation
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

#include <string.h>
#include "gstvaapiminiobject.h"

static void
gst_vaapi_mini_object_free (GstVaapiMiniObject * object)
{
  const GstVaapiMiniObjectClass *const klass = object->object_class;

  g_atomic_int_inc (&object->ref_count);

  if (klass->finalize)
    klass->finalize (object);

  if (G_LIKELY (g_atomic_int_dec_and_test (&object->ref_count)))
    g_free (object);
}

/**
 * gst_vaapi_mini_object_new:
 * @object_class: (optional): The object class
 *
 * Creates a new #GstVaapiMiniObject. If @object_class is NULL, then the
 * size of the allocated object is the same as sizeof(GstVaapiMiniObject).
 * If @object_class is not NULL, typically when a sub-class is implemented,
 * that pointer shall reference a statically allocated descriptor.
 *
 * This function does *not* zero-initialize the derived object data,
 * use gst_vaapi_mini_object_new0() to fill this purpose.
 *
 * Returns: The newly allocated #GstVaapiMiniObject
 */
GstVaapiMiniObject *
gst_vaapi_mini_object_new (const GstVaapiMiniObjectClass * object_class)
{
  GstVaapiMiniObject *object;

  static const GstVaapiMiniObjectClass default_object_class = {
    .size = sizeof (GstVaapiMiniObject),
  };

  if (G_UNLIKELY (!object_class))
    object_class = &default_object_class;

  g_return_val_if_fail (object_class->size >= sizeof (*object), NULL);

  object = g_malloc (object_class->size);
  if (!object)
    return NULL;

  object->object_class = object_class;
  g_atomic_int_set (&object->ref_count, 1);
  object->flags = 0;
  return object;
}

/**
 * gst_vaapi_mini_object_new0:
 * @object_class: (optional): The object class
 *
 * Creates a new #GstVaapiMiniObject. This function is similar to
 * gst_vaapi_mini_object_new() but derived object data is initialized
 * to zeroes.
 *
 * Returns: The newly allocated #GstVaapiMiniObject
 */
GstVaapiMiniObject *
gst_vaapi_mini_object_new0 (const GstVaapiMiniObjectClass * object_class)
{
  GstVaapiMiniObject *object;
  guint sub_size;

  object = gst_vaapi_mini_object_new (object_class);
  if (!object)
    return NULL;

  object_class = object->object_class;

  sub_size = object_class->size - sizeof (*object);
  if (sub_size > 0)
    memset (((guchar *) object) + sizeof (*object), 0, sub_size);
  return object;
}

/**
 * gst_vaapi_mini_object_ref_internal:
 * @object: a #GstVaapiMiniObject
 *
 * Atomically increases the reference count of the given @object by one.
 * This is an internal function that does not do any run-time type check.
 *
 * Returns: The same @object argument
 */
static inline GstVaapiMiniObject *
gst_vaapi_mini_object_ref_internal (GstVaapiMiniObject * object)
{
  g_atomic_int_inc (&object->ref_count);
  return object;
}

/**
 * gst_vaapi_mini_object_ref:
 * @object: a #GstVaapiMiniObject
 *
 * Atomically increases the reference count of the given @object by one.
 *
 * Returns: The same @object argument
 */
GstVaapiMiniObject *
gst_vaapi_mini_object_ref (GstVaapiMiniObject * object)
{
  g_return_val_if_fail (object != NULL, NULL);

  return gst_vaapi_mini_object_ref_internal (object);
}

/**
 * gst_vaapi_mini_object_unref_internal:
 * @object: a #GstVaapiMiniObject
 *
 * Atomically decreases the reference count of the @object by one. If
 * the reference count reaches zero, the object will be free'd.
 *
 * This is an internal function that does not do any run-time type check.
 */
static inline void
gst_vaapi_mini_object_unref_internal (GstVaapiMiniObject * object)
{
  if (g_atomic_int_dec_and_test (&object->ref_count))
    gst_vaapi_mini_object_free (object);
}

/**
 * gst_vaapi_mini_object_unref:
 * @object: a #GstVaapiMiniObject
 *
 * Atomically decreases the reference count of the @object by one. If
 * the reference count reaches zero, the object will be free'd.
 */
void
gst_vaapi_mini_object_unref (GstVaapiMiniObject * object)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (object->ref_count > 0);

  gst_vaapi_mini_object_unref_internal (object);
}

/**
 * gst_vaapi_mini_object_replace:
 * @old_object_ptr: a pointer to a #GstVaapiMiniObject
 * @new_object: a #GstVaapiMiniObject
 *
 * Atomically replaces the object held in @old_object_ptr with
 * @new_object. This means that @old_object_ptr shall reference a
 * valid object. However, @new_object can be NULL.
 */
void
gst_vaapi_mini_object_replace (GstVaapiMiniObject ** old_object_ptr,
    GstVaapiMiniObject * new_object)
{
  GstVaapiMiniObject *old_object;

  g_return_if_fail (old_object_ptr != NULL);

  old_object = g_atomic_pointer_get ((gpointer *) old_object_ptr);

  if (old_object == new_object)
    return;

  if (new_object)
    gst_vaapi_mini_object_ref_internal (new_object);

  while (!g_atomic_pointer_compare_and_exchange ((gpointer *) old_object_ptr,
          (gpointer) old_object, new_object))
    old_object = g_atomic_pointer_get ((gpointer *) old_object_ptr);

  if (old_object)
    gst_vaapi_mini_object_unref_internal (old_object);
}
