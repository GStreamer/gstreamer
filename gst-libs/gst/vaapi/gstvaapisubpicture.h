/*
 *  gstvaapisubpicture.h - VA subpicture abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011 Intel Corporation
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

#ifndef GST_VAAPI_SUBPICTURE_H
#define GST_VAAPI_SUBPICTURE_H

#include <gst/vaapi/gstvaapiobject.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiimage.h>
#include <gst/video/video-overlay-composition.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_SUBPICTURE \
    (gst_vaapi_subpicture_get_type())

#define GST_VAAPI_SUBPICTURE(obj)                               \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_SUBPICTURE,      \
                                GstVaapiSubpicture))

#define GST_VAAPI_SUBPICTURE_CLASS(klass)                       \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_SUBPICTURE,         \
                             GstVaapiSubpictureClass))

#define GST_VAAPI_IS_SUBPICTURE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_SUBPICTURE))

#define GST_VAAPI_IS_SUBPICTURE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_SUBPICTURE))

#define GST_VAAPI_SUBPICTURE_GET_CLASS(obj)                     \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_SUBPICTURE,       \
                               GstVaapiSubpictureClass))

typedef struct _GstVaapiSubpicture              GstVaapiSubpicture;
typedef struct _GstVaapiSubpicturePrivate       GstVaapiSubpicturePrivate;
typedef struct _GstVaapiSubpictureClass         GstVaapiSubpictureClass;

/**
 * GstVaapiSubpicture:
 *
 * A VA subpicture wrapper
 */
struct _GstVaapiSubpicture {
    /*< private >*/
    GstVaapiObject parent_instance;

    GstVaapiSubpicturePrivate *priv;
};

/**
 * GstVaapiSubpictureClass:
 *
 * A VA subpicture wrapper class
 */
struct _GstVaapiSubpictureClass {
    /*< private >*/
    GstVaapiObjectClass parent_class;
};

GType
gst_vaapi_subpicture_get_type(void) G_GNUC_CONST;

GstVaapiSubpicture *
gst_vaapi_subpicture_new(GstVaapiImage *image);

GstVaapiSubpicture *
gst_vaapi_subpicture_new_from_overlay_rectangle(
    GstVaapiDisplay          *display,
    GstVideoOverlayRectangle *rect
);

GstVaapiID
gst_vaapi_subpicture_get_id(GstVaapiSubpicture *subpicture);

GstVaapiImage *
gst_vaapi_subpicture_get_image(GstVaapiSubpicture *subpicture);

void
gst_vaapi_subpicture_set_image(
    GstVaapiSubpicture *subpicture,
    GstVaapiImage      *image
);

G_END_DECLS

#endif /* GST_VAAPI_SUBPICTURE_H */
