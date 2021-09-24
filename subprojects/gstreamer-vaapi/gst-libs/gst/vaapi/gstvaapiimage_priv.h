/*
 *  gstvaapiimage_priv.h - VA image abstraction (private definitions)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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

#ifndef GST_VAAPI_IMAGE_PRIV_H
#define GST_VAAPI_IMAGE_PRIV_H

#include <gst/vaapi/gstvaapiimage.h>

G_BEGIN_DECLS

typedef struct _GstVaapiImageRaw                GstVaapiImageRaw;

/**
 * GstVaapiImage:
 *
 * A VA image wrapper
 */
struct _GstVaapiImage {
    /*< private >*/
    GstMiniObject       mini_object;
    GstVaapiDisplay    *display;
    GstVaapiID          object_id;

    VAImage             internal_image;
    VAImage             image;
    guchar             *image_data;
    GstVideoFormat      internal_format;
    GstVideoFormat      format;
    guint               width;
    guint               height;
    guint               is_linear       : 1;
};

/**
 * GstVaapiImageRaw:
 *
 * A raw image wrapper. The caller is responsible for initializing all
 * the fields with sensible values.
 */
struct _GstVaapiImageRaw {
    GstVideoFormat      format;
    guint               width;
    guint               height;
    guint               num_planes;
    guchar             *pixels[3];
    guint               stride[3];
};

/**
 * GST_VAAPI_IMAGE_FORMAT:
 * @image: a #GstVaapiImage
 *
 * Macro that evaluates to the #GstVideoFormat of @image.
 */
#undef  GST_VAAPI_IMAGE_FORMAT
#define GST_VAAPI_IMAGE_FORMAT(image) \
    (GST_VAAPI_IMAGE(image)->format)

/**
 * GST_VAAPI_IMAGE_WIDTH:
 * @image: a #GstVaapiImage
 *
 * Macro that evaluates to the width of @image.
 */
#undef  GST_VAAPI_IMAGE_WIDTH
#define GST_VAAPI_IMAGE_WIDTH(image) \
    (GST_VAAPI_IMAGE(image)->width)

/**
 * GST_VAAPI_IMAGE_HEIGHT:
 * @image: a #GstVaapiImage
 *
 * Macro that evaluates to the height of @image.
 */
#undef  GST_VAAPI_IMAGE_HEIGHT
#define GST_VAAPI_IMAGE_HEIGHT(image) \
    (GST_VAAPI_IMAGE(image)->height)

/**
 * GST_VAAPI_IMAGE_DISPLAY:
 * @image: a #GstVaapiImage
 *
 * Macro that evaluates to the @image's display
 */
#undef  GST_VAAPI_IMAGE_DISPLAY
#define GST_VAAPI_IMAGE_DISPLAY(image) \
    (GST_VAAPI_IMAGE(image)->display)

/**
 * GST_VAAPI_IMAGE_ID:
 * @image: a #GstVaapiImage
 *
 * Macro that evaluates to the @image's object ID
 */
#undef  GST_VAAPI_IMAGE_ID
#define GST_VAAPI_IMAGE_ID(image) \
    (GST_VAAPI_IMAGE(image)->object_id)


G_GNUC_INTERNAL
gboolean
gst_vaapi_image_get_raw(
    GstVaapiImage     *image,
    GstVaapiImageRaw  *dst_image,
    GstVaapiRectangle *rect
);

G_GNUC_INTERNAL
gboolean
gst_vaapi_image_update_from_raw(
    GstVaapiImage     *image,
    GstVaapiImageRaw  *src_image,
    GstVaapiRectangle *rect
);

G_END_DECLS

#endif /* GST_VAAPI_IMAGE_PRIV_H */
