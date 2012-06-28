/*
 *  gstvaapiupload.h - VA-API video uploader
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
*/

#ifndef GST_VAAPIUPLOAD_H
#define GST_VAAPIUPLOAD_H

#include <gst/base/gstbasetransform.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapiimagepool.h>
#include <gst/vaapi/gstvaapisurfacepool.h>
#include <gst/vaapi/gstvaapivideobuffer.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPIUPLOAD \
    (gst_vaapiupload_get_type())

#define GST_VAAPIUPLOAD(obj)                            \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_TYPE_VAAPIUPLOAD,   \
                                GstVaapiUpload))

#define GST_VAAPIUPLOAD_CLASS(klass)                    \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_TYPE_VAAPIUPLOAD,      \
                             GstVaapiUploadClass))

#define GST_IS_VAAPIUPLOAD(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VAAPIUPLOAD))

#define GST_IS_VAAPIUPLOAD_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VAAPIUPLOAD))

#define GST_VAAPIUPLOAD_GET_CLASS(obj)                  \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_TYPE_VAAPIUPLOAD,    \
                               GstVaapiUploadClass))

typedef struct _GstVaapiUpload                  GstVaapiUpload;
typedef struct _GstVaapiUploadClass             GstVaapiUploadClass;

/* Max output surfaces */
#define GST_VAAPIUPLOAD_MAX_SURFACES 2

struct _GstVaapiUpload {
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

struct _GstVaapiUploadClass {
    /*< private >*/
    GstBaseTransformClass parent_class;
};

GType
gst_vaapiupload_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* GST_VAAPIUPLOAD_H */
