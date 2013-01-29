/*
 *  gstvaapiminiobject.c - A lightweight reference counted object
 *
 *  Copyright (C) 2012-2013 Intel Corporation
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

typedef struct _GstVaapiMiniObjectBase GstVaapiMiniObjectBase;
struct _GstVaapiMiniObjectBase {
    gconstpointer       object_class;
    gint                ref_count;
    GDestroyNotify      user_data_destroy_notify;
};

static inline GstVaapiMiniObjectBase *
object2base(GstVaapiMiniObject *object)
{
    return GSIZE_TO_POINTER(GPOINTER_TO_SIZE(object) -
        sizeof(GstVaapiMiniObjectBase));
}

static inline GstVaapiMiniObject *
base2object(GstVaapiMiniObjectBase *base_object)
{
    return GSIZE_TO_POINTER(GPOINTER_TO_SIZE(base_object) +
        sizeof(GstVaapiMiniObjectBase));
}

static void
gst_vaapi_mini_object_free(GstVaapiMiniObject *object)
{
    GstVaapiMiniObjectBase * const base_object = object2base(object);
    const GstVaapiMiniObjectClass * const klass = base_object->object_class;

    g_atomic_int_inc(&base_object->ref_count);

    if (klass->finalize)
        klass->finalize(object);

    if (G_LIKELY(g_atomic_int_dec_and_test(&base_object->ref_count))) {
        if (object->user_data && base_object->user_data_destroy_notify)
            base_object->user_data_destroy_notify(object->user_data);
        g_slice_free1(sizeof(*base_object) + klass->size, base_object);
    }
}

const GstVaapiMiniObjectClass *
gst_vaapi_mini_object_get_class(GstVaapiMiniObject *object)
{
    g_return_val_if_fail(object != NULL, NULL);

    return object2base(object)->object_class;
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
gst_vaapi_mini_object_new(const GstVaapiMiniObjectClass *object_class)
{
    GstVaapiMiniObject *object;
    GstVaapiMiniObjectBase *base_object;

    static const GstVaapiMiniObjectClass default_object_class = {
        .size = sizeof(GstVaapiMiniObject),
    };

    if (!object_class)
        object_class = &default_object_class;

    g_return_val_if_fail(object_class->size >= sizeof(*object), NULL);

    base_object = g_slice_alloc(sizeof(*base_object) + object_class->size);
    if (!base_object)
        return NULL;

    object = base2object(base_object);
    object->flags = 0;
    object->user_data = 0;

    base_object->object_class = object_class;
    base_object->ref_count = 1;
    base_object->user_data_destroy_notify = NULL;
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
gst_vaapi_mini_object_new0(const GstVaapiMiniObjectClass *object_class)
{
    GstVaapiMiniObject *object;
    guint sub_size;

    object = gst_vaapi_mini_object_new(object_class);
    if (!object)
        return NULL;

    object_class = object2base(object)->object_class;

    sub_size = object_class->size - sizeof(*object);
    if (sub_size > 0)
        memset(((guchar *)object) + sizeof(*object), 0, sub_size);
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
gst_vaapi_mini_object_ref(GstVaapiMiniObject *object)
{
    g_return_val_if_fail(object != NULL, NULL);

    g_atomic_int_inc(&object2base(object)->ref_count);
    return object;
}

/**
 * gst_vaapi_mini_object_unref:
 * @object: a #GstVaapiMiniObject
 *
 * Atomically decreases the reference count of the @object by one. If
 * the reference count reaches zero, the object will be free'd.
 */
void
gst_vaapi_mini_object_unref(GstVaapiMiniObject *object)
{
    g_return_if_fail(object != NULL);
    g_return_if_fail(object2base(object)->ref_count > 0);

    if (g_atomic_int_dec_and_test(&object2base(object)->ref_count))
        gst_vaapi_mini_object_free(object);
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
gst_vaapi_mini_object_replace(GstVaapiMiniObject **old_object_ptr,
    GstVaapiMiniObject *new_object)
{
    GstVaapiMiniObject *old_object;

    g_return_if_fail(old_object_ptr != NULL);

    old_object = g_atomic_pointer_get((gpointer *)old_object_ptr);

    if (old_object == new_object)
        return;

    if (new_object)
        gst_vaapi_mini_object_ref(new_object);

    while (!g_atomic_pointer_compare_and_exchange((gpointer *)old_object_ptr,
               old_object, new_object))
        old_object = g_atomic_pointer_get((gpointer *)old_object_ptr);

    if (old_object)
        gst_vaapi_mini_object_unref(old_object);
}

/**
 * gst_vaapi_mini_object_get_user_data:
 * @object: a #GstVaapiMiniObject
 *
 * Gets user-provided data set on the object via a previous call to
 * gst_vaapi_mini_object_set_user_data().
 *
 * Returns: (transfer none): The previously set user_data
 */
gpointer
gst_vaapi_mini_object_get_user_data(GstVaapiMiniObject *object)
{
    g_return_val_if_fail(object != NULL, NULL);

    return object->user_data;
}

/**
 * gst_vaapi_mini_object_set_user_data:
 * @object: a #GstVaapiMiniObject
 * @user_data: user-provided data
 * @destroy_notify: (closure user_data): a #GDestroyNotify
 *
 * Sets @user_data on the object and the #GDestroyNotify that will be
 * called when the data is freed.
 *
 * If some @user_data was previously set, then the former @destroy_notify
 * function will be called before the @user_data is replaced.
 */
void
gst_vaapi_mini_object_set_user_data(GstVaapiMiniObject *object,
    gpointer user_data, GDestroyNotify destroy_notify)
{
    GstVaapiMiniObjectBase *base_object;

    g_return_if_fail(object != NULL);

    base_object = object2base(object);
    if (object->user_data && base_object->user_data_destroy_notify)
        base_object->user_data_destroy_notify(object->user_data);

    object->user_data = user_data;
    base_object->user_data_destroy_notify = destroy_notify;
}
