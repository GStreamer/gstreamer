/*
 *  gstvaapiwindow_x11.h - VA/X11 window abstraction
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

#ifndef GST_VAAPI_WINDOW_X11_H
#define GST_VAAPI_WINDOW_X11_H

#include <X11/Xlib.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiwindow.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_WINDOW_X11 \
    (gst_vaapi_window_x11_get_type())

#define GST_VAAPI_WINDOW_X11(obj)                               \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_WINDOW_X11,      \
                                GstVaapiWindowX11))

#define GST_VAAPI_WINDOW_X11_CLASS(klass)                       \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_WINDOW_X11,         \
                             GstVaapiWindowX11Class))

#define GST_VAAPI_IS_WINDOW_X11(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_WINDOW_X11))

#define GST_VAAPI_IS_WINDOW_X11_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_WINDOW_X11))

#define GST_VAAPI_WINDOW_X11_GET_CLASS(obj)                     \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_WINDOW_X11,       \
                               GstVaapiWindowX11Class))

/**
 * GST_VAAPI_WINDOW_XWINDOW:
 * @window: a #GstVaapiWindow
 *
 * Macro that evaluates to the underlying X11 #Window of @window
 */
#define GST_VAAPI_WINDOW_XWINDOW(window) \
    gst_vaapi_window_x11_get_xid(GST_VAAPI_WINDOW_X11(window))

typedef struct _GstVaapiWindowX11               GstVaapiWindowX11;
typedef struct _GstVaapiWindowX11Private        GstVaapiWindowX11Private;
typedef struct _GstVaapiWindowX11Class          GstVaapiWindowX11Class;

/**
 * GstVaapiWindowX11:
 *
 * An X11 #Window wrapper.
 */
struct _GstVaapiWindowX11 {
    /*< private >*/
    GstVaapiWindow parent_instance;

    GstVaapiWindowX11Private *priv;
};

/**
 * GstVaapiWindowX11Class:
 * @get_visual: virtual function to get the desired visual used to
 *   create the window
 * @get_colormap: virtual function to get the desired colormap used to
 *   create the window
 *
 * An X11 #Window wrapper class.
 */
struct _GstVaapiWindowX11Class {
    /*< private >*/
    GstVaapiWindowClass parent_class;

    Visual *  (*get_visual)     (GstVaapiWindow *window);
    Colormap  (*get_colormap)   (GstVaapiWindow *window);
};

GType
gst_vaapi_window_x11_get_type(void) G_GNUC_CONST;

GstVaapiWindow *
gst_vaapi_window_x11_new(GstVaapiDisplay *display, guint width, guint height);

GstVaapiWindow *
gst_vaapi_window_x11_new_with_xid(GstVaapiDisplay *display, Window xid);

Window
gst_vaapi_window_x11_get_xid(GstVaapiWindowX11 *window);

gboolean
gst_vaapi_window_x11_is_foreign_xid(GstVaapiWindowX11 *window);

G_END_DECLS

#endif /* GST_VAAPI_WINDOW_X11_H */
