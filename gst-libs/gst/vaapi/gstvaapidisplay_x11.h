/*
 *  gstvaapidisplay_x11.h - VA/X11 display abstraction
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

#ifndef GST_VAAPI_DISPLAY_X11_H
#define GST_VAAPI_DISPLAY_X11_H

#include <gst/vaapi/gstvaapidisplay.h>
#include <va/va_x11.h>

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

#define GST_VAAPI_DISPLAY_XDISPLAY(display) \
    gst_vaapi_display_x11_get_display(GST_VAAPI_DISPLAY_X11(display))

typedef struct _GstVaapiDisplayX11              GstVaapiDisplayX11;
typedef struct _GstVaapiDisplayX11Private       GstVaapiDisplayX11Private;
typedef struct _GstVaapiDisplayX11Class         GstVaapiDisplayX11Class;

struct _GstVaapiDisplayX11 {
    /*< private >*/
    GstVaapiDisplay parent_instance;

    GstVaapiDisplayX11Private *priv;
};

struct _GstVaapiDisplayX11Class {
    /*< private >*/
    GstVaapiDisplayClass parent_class;
};

GType
gst_vaapi_display_x11_get_type(void);

GstVaapiDisplay *
gst_vaapi_display_x11_new(const gchar *display_name);

GstVaapiDisplay *
gst_vaapi_display_x11_new_with_display(Display *x11_display);

Display *
gst_vaapi_display_x11_get_display(GstVaapiDisplayX11 *display);

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_X11_H */
