/*
 *  gstvaapisink.h - VA-API video sink
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

#ifndef GST_VAAPISINK_H
#define GST_VAAPISINK_H

#include <gst/video/gstvideosink.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPISINK \
    (gst_vaapisink_get_type())

#define GST_VAAPISINK(obj)                              \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_TYPE_VAAPISINK,     \
                                GstVaapiSink))

#define GST_VAAPISINK_CLASS(klass)                      \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_TYPE_VAAPISINK,        \
                             GstVaapiSinkClass))

#define GST_IS_VAAPISINK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VAAPISINK))

#define GST_IS_VAAPISINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VAAPISINK))

#define GST_VAAPISINK_GET_CLASS(obj)                    \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_TYPE_VAAPISINK,      \
                               GstVaapiSink))

typedef struct _GstVaapiSink                    GstVaapiSink;
typedef struct _GstVaapiSinkPrivate             GstVaapiSinkPrivate;
typedef struct _GstVaapiSinkClass               GstVaapiSinkClass;

struct _GstVaapiSink {
    /*< private >*/
    GstVideoSink parent_instance;

    GstVaapiSinkPrivate *priv;
};

struct _GstVaapiSinkClass {
    /*< private >*/
    GstVideoSinkClass parent_class;
};

GType
gst_vaapisink_get_type(void);

G_END_DECLS

#endif /* GST_VAAPISINK_H */
