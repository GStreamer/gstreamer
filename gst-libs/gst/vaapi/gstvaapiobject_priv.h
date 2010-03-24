/*
 *  gstvaapiobject_priv.h - Base VA object (private definitions)
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef GST_VAAPI_OBJECT_PRIV_H
#define GST_VAAPI_OBJECT_PRIV_H

#include <gst/vaapi/gstvaapiobject.h>

G_BEGIN_DECLS

#define GST_VAAPI_OBJECT_GET_PRIVATE(obj)                       \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_OBJECT,         \
                                 GstVaapiObjectPrivate))

#define GST_VAAPI_OBJECT_CAST(object) ((GstVaapiObject *)(object))

/**
 * GST_VAAPI_OBJECT_GET_DISPLAY:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #GstVaapiDisplay the @object is bound to.
 * This is an internal macro that does not do any run-time type check.
 */
#define GST_VAAPI_OBJECT_GET_DISPLAY(object) \
    GST_VAAPI_OBJECT_CAST(object)->priv->display

/**
 * GST_VAAPI_OBJECT_ID:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #GstVaapiID contained in @object.
 * This is an internal macro that does not do any run-time type checks.
 */
#define GST_VAAPI_OBJECT_ID(object) \
    GST_VAAPI_OBJECT_CAST(object)->priv->id

/**
 * GstVaapiObjectPrivate:
 *
 * VA object base.
 */
struct _GstVaapiObjectPrivate {
    GstVaapiDisplay    *display;
    GstVaapiID          id;
    guint               is_destroying   : 1;
};

G_END_DECLS

#endif /* GST_VAAPI_OBJECT_PRIV_H */
