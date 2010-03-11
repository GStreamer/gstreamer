/*
 *  gstvaapisinkbase.h - VA sink interface
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

#ifndef GST_VAAPISINK_BASE_H
#define GST_VAAPISINK_BASE_H

#include <gst/gstelement.h>
#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPISINK_BASE \
    (gst_vaapisink_base_get_type())

#define GST_VAAPISINK_BASE(obj)                                 \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_TYPE_VAAPISINK_BASE,        \
                                GstVaapiSinkBase))

#define GST_IS_VAAPISINK_BASE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VAAPISINK_BASE))

#define GST_VAAPISINK_BASE_GET_INTERFACE(obj)                   \
    (G_TYPE_INSTANCE_GET_INTERFACE((obj),                       \
                               GST_TYPE_VAAPISINK_BASE,         \
                               GstVaapiSinkBaseInterface))

typedef struct _GstVaapiSinkBase                GstVaapiSinkBase; /* dummy */
typedef struct _GstVaapiSinkBaseInterface       GstVaapiSinkBaseInterface;

struct _GstVaapiSinkBaseInterface {
    /*< private >*/
    GTypeInterface g_iface;

    GstVaapiDisplay *(*get_display)(GstVaapiSinkBase *sink);
};

GType
gst_vaapisink_base_get_type(void);

GstVaapiDisplay *
gst_vaapisink_base_get_display(GstVaapiSinkBase *sink);

GstVaapiSinkBase *
gst_vaapisink_base_lookup(GstElement *element);

G_END_DECLS

#endif /* GST_VAAPISINK_BASE_H */
