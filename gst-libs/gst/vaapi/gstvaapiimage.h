/*
 *  gstvaapiimage.h - VA image abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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

#ifndef GST_VAAPI_IMAGE_H
#define GST_VAAPI_IMAGE_H

#include <gst/gstbuffer.h>
#include <gst/vaapi/gstvaapiobject.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiimageformat.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_IMAGE \
    (gst_vaapi_image_get_type())

#define GST_VAAPI_IMAGE(obj)                            \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_IMAGE,   \
                                GstVaapiImage))

#define GST_VAAPI_IMAGE_CLASS(klass)                    \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_IMAGE,      \
                             GstVaapiImageClass))

#define GST_VAAPI_IS_IMAGE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_IMAGE))

#define GST_VAAPI_IS_IMAGE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_IMAGE))

#define GST_VAAPI_IMAGE_GET_CLASS(obj)                  \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_IMAGE,    \
                               GstVaapiImageClass))

/**
 * GST_VAAPI_IMAGE_FORMAT:
 * @image: a #GstVaapiImage
 *
 * Macro that evaluates to the #GstVaapiImageFormat of @image.
 */
#define GST_VAAPI_IMAGE_FORMAT(image)   gst_vaapi_image_get_format(image)

/**
 * GST_VAAPI_IMAGE_WIDTH:
 * @image: a #GstVaapiImage
 *
 * Macro that evaluates to the width of @image.
 */
#define GST_VAAPI_IMAGE_WIDTH(image)    gst_vaapi_image_get_width(image)

/**
 * GST_VAAPI_IMAGE_HEIGHT:
 * @image: a #GstVaapiImage
 *
 * Macro that evaluates to the height of @image.
 */
#define GST_VAAPI_IMAGE_HEIGHT(image)   gst_vaapi_image_get_height(image)

typedef struct _GstVaapiImage                   GstVaapiImage;
typedef struct _GstVaapiImagePrivate            GstVaapiImagePrivate;
typedef struct _GstVaapiImageClass              GstVaapiImageClass;
typedef struct _GstVaapiImageRaw                GstVaapiImageRaw;

/**
 * GstVaapiImage:
 *
 * A VA image wrapper
 */
struct _GstVaapiImage {
    /*< private >*/
    GstVaapiObject parent_instance;

    GstVaapiImagePrivate *priv;
};

/**
 * GstVaapiImageClass:
 *
 * A VA image wrapper class
 */
struct _GstVaapiImageClass {
    /*< private >*/
    GstVaapiObjectClass parent_class;
};

/**
 * GstVaapiImageRaw:
 *
 * A raw image wrapper. The caller is responsible for initializing all
 * the fields with sensible values.
 */
struct _GstVaapiImageRaw {
    GstVaapiImageFormat format;
    guint               width;
    guint               height;
    guint               num_planes;
    guchar             *pixels[3];
    guint               stride[3];
};

GType
gst_vaapi_image_get_type(void) G_GNUC_CONST;

GstVaapiImage *
gst_vaapi_image_new(
    GstVaapiDisplay    *display,
    GstVaapiImageFormat format,
    guint               width,
    guint               height
);

GstVaapiImage *
gst_vaapi_image_new_with_image(GstVaapiDisplay *display, VAImage *va_image);

GstVaapiID
gst_vaapi_image_get_id(GstVaapiImage *image);

gboolean
gst_vaapi_image_get_image(GstVaapiImage *image, VAImage *va_image);

GstVaapiImageFormat
gst_vaapi_image_get_format(GstVaapiImage *image);

guint
gst_vaapi_image_get_width(GstVaapiImage *image);

guint
gst_vaapi_image_get_height(GstVaapiImage *image);

void
gst_vaapi_image_get_size(GstVaapiImage *image, guint *pwidth, guint *pheight);

gboolean
gst_vaapi_image_is_linear(GstVaapiImage *image);

gboolean
gst_vaapi_image_is_mapped(GstVaapiImage *image);

gboolean
gst_vaapi_image_map(GstVaapiImage *image);

gboolean
gst_vaapi_image_unmap(GstVaapiImage *image);

guint
gst_vaapi_image_get_plane_count(GstVaapiImage *image);

guchar *
gst_vaapi_image_get_plane(GstVaapiImage *image, guint plane);

guint
gst_vaapi_image_get_pitch(GstVaapiImage *image, guint plane);

guint
gst_vaapi_image_get_data_size(GstVaapiImage *image);

gboolean
gst_vaapi_image_get_buffer(
    GstVaapiImage     *image,
    GstBuffer         *buffer,
    GstVaapiRectangle *rect
);

gboolean
gst_vaapi_image_get_raw(
    GstVaapiImage     *image,
    GstVaapiImageRaw  *dst_image,
    GstVaapiRectangle *rect
);

gboolean
gst_vaapi_image_update_from_buffer(
    GstVaapiImage     *image,
    GstBuffer         *buffer,
    GstVaapiRectangle *rect
);

gboolean
gst_vaapi_image_update_from_raw(
    GstVaapiImage     *image,
    GstVaapiImageRaw  *src_image,
    GstVaapiRectangle *rect
);

G_END_DECLS

#endif /* GST_VAAPI_IMAGE_H */
