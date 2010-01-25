/*
 *  gstvaapidisplay.h - VA display abstraction
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

#ifndef GST_VAAPI_DISPLAY_H
#define GST_VAAPI_DISPLAY_H

#include <va/va.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_DISPLAY \
    (gst_vaapi_display_get_type())

#define GST_VAAPI_DISPLAY(obj)                          \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_DISPLAY, \
                                GstVaapiDisplay))

#define GST_VAAPI_DISPLAY_CLASS(klass)                  \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_DISPLAY,    \
                             GstVaapiDisplayClass))

#define GST_VAAPI_IS_DISPLAY(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DISPLAY))

#define GST_VAAPI_IS_DISPLAY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DISPLAY))

#define GST_VAAPI_DISPLAY_GET_CLASS(obj)                \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_DISPLAY,  \
                               GstVaapiDisplay))

typedef struct _GstVaapiDisplay                 GstVaapiDisplay;
typedef struct _GstVaapiDisplayPrivate          GstVaapiDisplayPrivate;
typedef struct _GstVaapiDisplayClass            GstVaapiDisplayClass;

struct _GstVaapiDisplay {
    /*< private >*/
    GObject parent_instance;

    GstVaapiDisplayPrivate *priv;
};

struct _GstVaapiDisplayClass {
    /*< private >*/
    GObjectClass parent_class;
};

GType
gst_vaapi_display_get_type(void);

VADisplay
gst_vaapi_display_get_display(GstVaapiDisplay *display);

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_H */
