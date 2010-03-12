/*
 *  gstvaapisurface.h - VA surface abstraction
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

#ifndef GST_VAAPI_SURFACE_H
#define GST_VAAPI_SURFACE_H

#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

typedef enum _GstVaapiChromaType                GstVaapiChromaType;

enum _GstVaapiChromaType {
    GST_VAAPI_CHROMA_TYPE_YUV420 = 1,
    GST_VAAPI_CHROMA_TYPE_YUV422,
    GST_VAAPI_CHROMA_TYPE_YUV444
};

#define GST_VAAPI_TYPE_SURFACE \
    (gst_vaapi_surface_get_type())

#define GST_VAAPI_SURFACE(obj)                          \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_SURFACE, \
                                GstVaapiSurface))

#define GST_VAAPI_SURFACE_CLASS(klass)                  \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_SURFACE,    \
                             GstVaapiSurfaceClass))

#define GST_VAAPI_IS_SURFACE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_SURFACE))

#define GST_VAAPI_IS_SURFACE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_SURFACE))

#define GST_VAAPI_SURFACE_GET_CLASS(obj)                \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_SURFACE,  \
                               GstVaapiSurfaceClass))

typedef struct _GstVaapiSurface                 GstVaapiSurface;
typedef struct _GstVaapiSurfacePrivate          GstVaapiSurfacePrivate;
typedef struct _GstVaapiSurfaceClass            GstVaapiSurfaceClass;

struct _GstVaapiSurface {
    /*< private >*/
    GObject parent_instance;

    GstVaapiSurfacePrivate *priv;
};

struct _GstVaapiSurfaceClass {
    /*< private >*/
    GObjectClass parent_class;
};

GType
gst_vaapi_surface_get_type(void);

GstVaapiSurface *
gst_vaapi_surface_new(
    GstVaapiDisplay    *display,
    GstVaapiChromaType  chroma_type,
    guint               width,
    guint               height
);

VASurfaceID
gst_vaapi_surface_get_id(GstVaapiSurface *surface);

GstVaapiDisplay *
gst_vaapi_surface_get_display(GstVaapiSurface *surface);

GstVaapiChromaType
gst_vaapi_surface_get_chroma_type(GstVaapiSurface *surface);

guint
gst_vaapi_surface_get_width(GstVaapiSurface *surface);

guint
gst_vaapi_surface_get_height(GstVaapiSurface *surface);

void
gst_vaapi_surface_get_size(
    GstVaapiSurface *surface,
    guint           *pwidth,
    guint           *pheight
);

G_END_DECLS

#endif /* GST_VAAPI_SURFACE_H */
