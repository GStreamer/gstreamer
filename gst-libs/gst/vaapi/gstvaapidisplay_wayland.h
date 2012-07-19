/*
 *  gstvaapidisplay_wayland.h - VA/Wayland display abstraction
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

#ifndef GST_VAAPI_DISPLAY_WAYLAND_H
#define GST_VAAPI_DISPLAY_WAYLAND_H

#include <va/va_wayland.h>
#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_DISPLAY_WAYLAND \
    (gst_vaapi_display_wayland_get_type())

#define GST_VAAPI_DISPLAY_WAYLAND(obj)                          \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_DISPLAY_WAYLAND, \
                                GstVaapiDisplayWayland))

#define GST_VAAPI_DISPLAY_WAYLAND_CLASS(klass)                  \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_DISPLAY_WAYLAND,    \
                             GstVaapiDisplayWaylandClass))

#define GST_VAAPI_IS_DISPLAY_WAYLAND(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DISPLAY_WAYLAND))

#define GST_VAAPI_IS_DISPLAY_WAYLAND_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DISPLAY_WAYLAND))

#define GST_VAAPI_DISPLAY_WAYLAND_GET_CLASS(obj)                \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_DISPLAY_WAYLAND,  \
                               GstVaapiDisplayWaylandClass))

typedef struct _GstVaapiDisplayWayland          GstVaapiDisplayWayland;
typedef struct _GstVaapiDisplayWaylandPrivate   GstVaapiDisplayWaylandPrivate;
typedef struct _GstVaapiDisplayWaylandClass     GstVaapiDisplayWaylandClass;

/**
 * GstVaapiDisplayWayland:
 *
 * VA/Wayland display wrapper.
 */
struct _GstVaapiDisplayWayland {
    /*< private >*/
    GstVaapiDisplay parent_instance;

    GstVaapiDisplayWaylandPrivate *priv;
};

/**
 * GstVaapiDisplayWaylandClass:
 *
 * VA/Wayland display wrapper clas.
 */
struct _GstVaapiDisplayWaylandClass {
    /*< private >*/
    GstVaapiDisplayClass parent_class;
};

GType
gst_vaapi_display_wayland_get_type(void) G_GNUC_CONST;

GstVaapiDisplay *
gst_vaapi_display_wayland_new(const gchar *display_name);

GstVaapiDisplay *
gst_vaapi_display_wayland_new_with_display(struct wl_display *wl_display);

struct wl_display *
gst_vaapi_display_wayland_get_display(GstVaapiDisplayWayland *display);

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_WAYLAND_H */
