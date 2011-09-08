/*
 *  gstvaapiconvert.h - VA-API video converter
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
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

#ifndef GST_VAAPICONVERT_H
#define GST_VAAPICONVERT_H

#include <gst/base/gstbasetransform.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapiimagepool.h>
#include <gst/vaapi/gstvaapisurfacepool.h>
#include <gst/vaapi/gstvaapivideobuffer.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPICONVERT \
    (gst_vaapiconvert_get_type())

#define GST_VAAPICONVERT(obj)                           \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_TYPE_VAAPICONVERT,  \
                                GstVaapiConvert))

#define GST_VAAPICONVERT_CLASS(klass)                   \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_TYPE_VAAPICONVERT,     \
                             GstVaapiConvertClass))

#define GST_IS_VAAPICONVERT(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VAAPICONVERT))

#define GST_IS_VAAPICONVERT_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VAAPICONVERT))

#define GST_VAAPICONVERT_GET_CLASS(obj)                 \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_TYPE_VAAPICONVERT,   \
                               GstVaapiConvertClass))

typedef struct _GstVaapiConvert                 GstVaapiConvert;
typedef struct _GstVaapiConvertClass            GstVaapiConvertClass;

/* Max output surfaces */
#define GST_VAAPICONVERT_MAX_SURFACES 2

struct _GstVaapiConvert {
    /*< private >*/
    GstBaseTransform    parent_instance;

    GstVaapiDisplay    *display;
    GstVaapiVideoPool  *images;
    guint               image_width;
    guint               image_height;
    GstVaapiVideoPool  *surfaces;
    guint               surface_width;
    guint               surface_height;
    guint               direct_rendering_caps;
    guint               direct_rendering;
    unsigned int        images_reset    : 1;
    unsigned int        surfaces_reset  : 1;
};

struct _GstVaapiConvertClass {
    /*< private >*/
    GstBaseTransformClass parent_class;
};

GType
gst_vaapiconvert_get_type(void);

G_END_DECLS

#endif /* GST_VAAPICONVERT_H */
