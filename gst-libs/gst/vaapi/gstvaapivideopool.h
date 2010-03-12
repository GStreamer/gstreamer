/*
 *  gstvaapivideopool.h - Video object pool abstraction
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

#ifndef GST_VAAPI_VIDEO_POOL_H
#define GST_VAAPI_VIDEO_POOL_H

#include <gst/gstcaps.h>
#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_VIDEO_POOL \
    (gst_vaapi_video_pool_get_type())

#define GST_VAAPI_VIDEO_POOL(obj)                               \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_VIDEO_POOL,      \
                                GstVaapiVideoPool))

#define GST_VAAPI_VIDEO_POOL_CLASS(klass)                       \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_VIDEO_POOL,         \
                             GstVaapiVideoPoolClass))

#define GST_VAAPI_IS_VIDEO_POOL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_VIDEO_POOL))

#define GST_VAAPI_IS_VIDEO_POOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_VIDEO_POOL))

#define GST_VAAPI_VIDEO_POOL_GET_CLASS(obj)                     \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_VIDEO_POOL,       \
                               GstVaapiVideoPoolClass))

typedef struct _GstVaapiVideoPool               GstVaapiVideoPool;
typedef struct _GstVaapiVideoPoolPrivate        GstVaapiVideoPoolPrivate;
typedef struct _GstVaapiVideoPoolClass          GstVaapiVideoPoolClass;

struct _GstVaapiVideoPool {
    /*< private >*/
    GObject parent_instance;

    GstVaapiVideoPoolPrivate *priv;
};

struct _GstVaapiVideoPoolClass {
    /*< private >*/
    GObjectClass parent_class;

    void     (*set_caps)    (GstVaapiVideoPool *pool, GstCaps *caps);
    gpointer (*alloc_object)(GstVaapiVideoPool *pool, GstVaapiDisplay *display);
};

GType
gst_vaapi_video_pool_get_type(void);

GstCaps *
gst_vaapi_video_pool_get_caps(GstVaapiVideoPool *pool);

gpointer
gst_vaapi_video_pool_get_object(GstVaapiVideoPool *pool);

void
gst_vaapi_video_pool_put_object(GstVaapiVideoPool *pool, gpointer object);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_POOL_H */
