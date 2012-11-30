/*
 *  gstvaapiminiobject.h - A lightweight reference counted object
 *
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

#ifndef GST_VAAPI_MINI_OBJECT_H
#define GST_VAAPI_MINI_OBJECT_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _GstVaapiMiniObject      GstVaapiMiniObject;
typedef struct _GstVaapiMiniObjectClass GstVaapiMiniObjectClass;

/**
 * GST_VAAPI_MINI_OBJECT:
 * @object: a #GstVaapiMiniObject
 *
 * Casts the @object to a #GstVaapiMiniObject
 */
#define GST_VAAPI_MINI_OBJECT(object) \
    ((GstVaapiMiniObject *)(object))

/**
 * GST_VAAPI_MINI_OBJECT_GET_CLASS:
 * @object: a #GstVaapiMiniObject
 *
 * Retrieves the #GstVaapiMiniObjectClass associated with the @object
 */
#define GST_VAAPI_MINI_OBJECT_GET_CLASS(object) \
    gst_vaapi_mini_object_get_class(GST_VAAPI_MINI_OBJECT(object))

/**
 * GST_VAAPI_MINI_OBJECT_FLAGS:
 * @object: a #GstVaapiMiniObject
 *
 * The entire set of flags for the @object
 */
#define GST_VAAPI_MINI_OBJECT_FLAGS(object) \
    (GST_VAAPI_MINI_OBJECT(object)->flags)

/**
 * GST_VAAPI_MINI_OBJECT_FLAG_IS_SET:
 * @object: a #GstVaapiMiniObject
 * @flag: a flag to check for
 *
 * Checks whether the given @flag is set
 */
#define GST_VAAPI_MINI_OBJECT_FLAG_IS_SET(object, flag) \
    ((GST_VAAPI_MINI_OBJECT_FLAGS(object) & (flag)) != 0)

/**
 * GST_VAAPI_MINI_OBJECT_FLAG_SET:
 * @object: a #GstVaapiMiniObject
 * @flags: flags to set
 *
 * This macro sets the given bits
 */
#define GST_VAAPI_MINI_OBJECT_FLAG_SET(object, flags) \
    (GST_VAAPI_MINI_OBJECT_FLAGS(object) |= (flags))

/**
 * GST_VAAPI_MINI_OBJECT_FLAG_UNSET:
 * @object: a #GstVaapiMiniObject
 * @flags: flags to unset
 *
 * This macro unsets the given bits.
 */
#define GST_VAAPI_MINI_OBJECT_FLAG_UNSET(object, flags) \
    (GST_VAAPI_MINI_OBJECT_FLAGS(object) &= ~(flags))

/**
 * GstVaapiMiniObject:
 * @flags: set of flags that should be manipulated through
 *   GST_VAAPI_MINI_OBJECT_FLAG_*() functions
 * @user_data: user-provided data from gst_vaapi_mini_object_set_user_data()
 *
 * A #GstVaapiMiniObject represents a minimal reference counted data
 * structure that can hold a set of flags and user-provided data.
 */
struct _GstVaapiMiniObject {
    /*< private >*/
    guint               flags;
    gpointer            user_data;
};

/**
 * GstVaapiMiniObjectClass:
 * @size: size in bytes of the #GstVaapiMiniObject, plus any
 *   additional data for derived classes
 * @finalize: function called to destroy data in derived classes
 *
 * A #GstVaapiMiniObjectClass represents the base object class that
 * defines the size of the #GstVaapiMiniObject and utility function to
 * dispose child objects
 */
struct _GstVaapiMiniObjectClass {
    guint               size;
    GDestroyNotify      finalize;
};

G_GNUC_INTERNAL
const GstVaapiMiniObjectClass *
gst_vaapi_mini_object_get_class(GstVaapiMiniObject *object) G_GNUC_CONST;

G_GNUC_INTERNAL
GstVaapiMiniObject *
gst_vaapi_mini_object_new(const GstVaapiMiniObjectClass *object_class);

G_GNUC_INTERNAL
GstVaapiMiniObject *
gst_vaapi_mini_object_new0(const GstVaapiMiniObjectClass *object_class);

G_GNUC_INTERNAL
GstVaapiMiniObject *
gst_vaapi_mini_object_ref(GstVaapiMiniObject *object);

G_GNUC_INTERNAL
void
gst_vaapi_mini_object_unref(GstVaapiMiniObject *object);

G_GNUC_INTERNAL
void
gst_vaapi_mini_object_replace(GstVaapiMiniObject **old_object_ptr,
    GstVaapiMiniObject *new_object);

G_GNUC_INTERNAL
gpointer
gst_vaapi_mini_object_get_user_data(GstVaapiMiniObject *object);

G_GNUC_INTERNAL
void
gst_vaapi_mini_object_set_user_data(GstVaapiMiniObject *object,
    gpointer user_data, GDestroyNotify destroy_notify);

G_END_DECLS

#endif /* GST_VAAPI_MINI_OBJECT_H */
