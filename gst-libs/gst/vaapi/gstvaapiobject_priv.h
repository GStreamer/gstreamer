/*
 *  gstvaapiobject_priv.h - Base VA object (private definitions)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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
 * GST_VAAPI_OBJECT_DISPLAY:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #GstVaapiDisplay the @object is bound to.
 * This is an internal macro that does not do any run-time type check.
 */
#define GST_VAAPI_OBJECT_DISPLAY(object) \
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
 * GST_VAAPI_OBJECT_DISPLAY_X11:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #GstVaapiDisplayX11 the @object is bound to.
 * This is an internal macro that does not do any run-time type check
 * and requires #include "gstvaapidisplay_x11_priv.h"
 */
#define GST_VAAPI_OBJECT_DISPLAY_X11(object) \
    GST_VAAPI_DISPLAY_X11_CAST(GST_VAAPI_OBJECT_DISPLAY(object))

/**
 * GST_VAAPI_OBJECT_DISPLAY_GLX:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #GstVaapiDisplayGLX the @object is bound to.
 * This is an internal macro that does not do any run-time type check
 * and requires #include "gstvaapidisplay_glx_priv.h".
 */
#define GST_VAAPI_OBJECT_DISPLAY_GLX(object) \
    GST_VAAPI_DISPLAY_GLX_CAST(GST_VAAPI_OBJECT_DISPLAY(object))

/**
 * GST_VAAPI_OBJECT_DISPLAY_WAYLAND:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #GstVaapiDisplayWayland the @object is
 * bound to.  This is an internal macro that does not do any run-time
 * type check and requires #include "gstvaapidisplay_wayland_priv.h"
 */
#define GST_VAAPI_OBJECT_DISPLAY_WAYLAND(object) \
    GST_VAAPI_DISPLAY_WAYLAND_CAST(GST_VAAPI_OBJECT_DISPLAY(object))

/**
 * GST_VAAPI_OBJECT_VADISPLAY:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the #VADisplay of @display.
 * This is an internal macro that does not do any run-time type check
 * and requires #include "gstvaapidisplay_priv.h".
 */
#define GST_VAAPI_OBJECT_VADISPLAY(object) \
    GST_VAAPI_DISPLAY_VADISPLAY(GST_VAAPI_OBJECT_DISPLAY(object))

/**
 * GST_VAAPI_OBJECT_XDISPLAY:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the underlying X11 #Display of @display.
 * This is an internal macro that does not do any run-time type check
 * and requires #include "gstvaapidisplay_x11_priv.h".
 */
#define GST_VAAPI_OBJECT_XDISPLAY(object) \
    GST_VAAPI_DISPLAY_XDISPLAY(GST_VAAPI_OBJECT_DISPLAY(object))

/**
 * GST_VAAPI_OBJECT_XSCREEN:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the underlying X11 screen of @display.
 * This is an internal macro that does not do any run-time type check
 * and requires #include "gstvaapidisplay_x11_priv.h".
 */
#define GST_VAAPI_OBJECT_XSCREEN(object) \
    GST_VAAPI_DISPLAY_XSCREEN(GST_VAAPI_OBJECT_DISPLAY(object))

/**
 * GST_VAAPI_OBJECT_WL_DISPLAY:
 * @object: a #GstVaapiObject
 *
 * Macro that evaluates to the underlying #wl_display of @display.
 * This is an internal macro that does not do any run-time type check
 * and requires #include "gstvaapidisplay_wayland_priv.h".
 */
#define GST_VAAPI_OBJECT_WL_DISPLAY(object) \
    GST_VAAPI_DISPLAY_WL_DISPLAY(GST_VAAPI_OBJECT_DISPLAY(object))

/**
 * GST_VAAPI_OBJECT_LOCK_DISPLAY:
 * @object: a #GstVaapiObject
 *
 * Macro that locks the #GstVaapiDisplay contained in the @object.
 * This is an internal macro that does not do any run-time type check.
 */
#define GST_VAAPI_OBJECT_LOCK_DISPLAY(object) \
    GST_VAAPI_DISPLAY_LOCK(GST_VAAPI_OBJECT_DISPLAY(object))

/**
 * GST_VAAPI_OBJECT_UNLOCK_DISPLAY:
 * @object: a #GstVaapiObject
 *
 * Macro that unlocks the #GstVaapiDisplay contained in the @object.
 * This is an internal macro that does not do any run-time type check.
 */
#define GST_VAAPI_OBJECT_UNLOCK_DISPLAY(object) \
    GST_VAAPI_DISPLAY_UNLOCK(GST_VAAPI_OBJECT_DISPLAY(object))

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
