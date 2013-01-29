/*
 *  gstvaapiobject.h - Base VA object
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

#ifndef GST_VAAPI_OBJECT_H
#define GST_VAAPI_OBJECT_H

#include <gst/vaapi/gstvaapitypes.h>
#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_OBJECT \
    (gst_vaapi_object_get_type())

#define GST_VAAPI_OBJECT(obj)                           \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_OBJECT,  \
                                GstVaapiObject))

#define GST_VAAPI_OBJECT_CLASS(klass)                   \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_OBJECT,     \
                             GstVaapiObjectClass))

#define GST_VAAPI_IS_OBJECT(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_OBJECT))

#define GST_VAAPI_IS_OBJECT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_OBJECT))

#define GST_VAAPI_OBJECT_GET_CLASS(obj)                 \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_OBJECT,   \
                               GstVaapiObjectClass))

typedef struct _GstVaapiObject                  GstVaapiObject;
typedef struct _GstVaapiObjectPrivate           GstVaapiObjectPrivate;
typedef struct _GstVaapiObjectClass             GstVaapiObjectClass;

/**
 * GstVaapiObject:
 *
 * VA object base.
 */
struct _GstVaapiObject {
    /*< private >*/
    GObject parent_instance;

    GstVaapiObjectPrivate *priv;
};

/**
 * GstVaapiObjectClass:
 * @destroy: signal class handler for #GstVaapiObject::destroy
 *
 * VA object base class.
 */
struct _GstVaapiObjectClass {
    /*< private >*/
    GObjectClass parent_class;

    /*< public >*/
    void (*destroy)(GstVaapiObject *oject);
};

GType
gst_vaapi_object_get_type(void) G_GNUC_CONST;

GstVaapiDisplay *
gst_vaapi_object_get_display(GstVaapiObject *object);

void
gst_vaapi_object_lock_display(GstVaapiObject *object);

void
gst_vaapi_object_unlock_display(GstVaapiObject *object);

GstVaapiID
gst_vaapi_object_get_id(GstVaapiObject *object);

G_END_DECLS

#endif /* GST_VAAPI_OBJECT_H */
