/*
 *  gstvaapidisplay_x11.h - VA/X11 display abstraction
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

#ifndef GST_VAAPI_DISPLAY_X11_H
#define GST_VAAPI_DISPLAY_X11_H

#include <va/va_x11.h>
#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_DISPLAY_X11 \
    (gst_vaapi_display_x11_get_type())

#define GST_VAAPI_DISPLAY_X11(obj)                              \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_DISPLAY_X11,     \
                                GstVaapiDisplayX11))

#define GST_VAAPI_DISPLAY_X11_CLASS(klass)                      \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_DISPLAY_X11,        \
                             GstVaapiDisplayX11Class))

#define GST_VAAPI_IS_DISPLAY_X11(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DISPLAY_X11))

#define GST_VAAPI_IS_DISPLAY_X11_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DISPLAY_X11))

#define GST_VAAPI_DISPLAY_X11_GET_CLASS(obj)                    \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_DISPLAY_X11,      \
                               GstVaapiDisplayX11Class))

typedef struct _GstVaapiDisplayX11              GstVaapiDisplayX11;
typedef struct _GstVaapiDisplayX11Private       GstVaapiDisplayX11Private;
typedef struct _GstVaapiDisplayX11Class         GstVaapiDisplayX11Class;

/**
 * GstVaapiDisplayX11:
 *
 * VA/X11 display wrapper.
 */
struct _GstVaapiDisplayX11 {
    /*< private >*/
    GstVaapiDisplay parent_instance;

    GstVaapiDisplayX11Private *priv;
};


/**
 * GstVaapiDisplayX11Class:
 *
 * VA/X11 display wrapper clas.
 */
struct _GstVaapiDisplayX11Class {
    /*< private >*/
    GstVaapiDisplayClass parent_class;
};

GType
gst_vaapi_display_x11_get_type(void) G_GNUC_CONST;

GstVaapiDisplay *
gst_vaapi_display_x11_new(const gchar *display_name);

GstVaapiDisplay *
gst_vaapi_display_x11_new_with_display(Display *x11_display);

Display *
gst_vaapi_display_x11_get_display(GstVaapiDisplayX11 *display);

int
gst_vaapi_display_x11_get_screen(GstVaapiDisplayX11 *display);

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_X11_H */
