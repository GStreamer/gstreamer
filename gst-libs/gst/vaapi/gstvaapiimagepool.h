/*
 *  gstvaapiimagepool.h - Gst VA image pool
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2012 Intel Corporation
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

#ifndef GST_VAAPI_IMAGE_POOL_H
#define GST_VAAPI_IMAGE_POOL_H

#include <gst/vaapi/gstvaapiimage.h>
#include <gst/vaapi/gstvaapivideopool.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_IMAGE_POOL \
    (gst_vaapi_image_pool_get_type())

#define GST_VAAPI_IMAGE_POOL(obj)                               \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_IMAGE_POOL,      \
                                GstVaapiImagePool))

#define GST_VAAPI_IMAGE_POOL_CLASS(klass)                       \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_IMAGE_POOL,         \
                             GstVaapiImagePoolClass))

#define GST_VAAPI_IS_IMAGE_POOL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_IMAGE_POOL))

#define GST_VAAPI_IS_IMAGE_POOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_IMAGE_POOL))

#define GST_VAAPI_IMAGE_POOL_GET_CLASS(obj)                     \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_IMAGE_POOL,       \
                               GstVaapiImagePoolClass))

typedef struct _GstVaapiImagePool               GstVaapiImagePool;
typedef struct _GstVaapiImagePoolPrivate        GstVaapiImagePoolPrivate;
typedef struct _GstVaapiImagePoolClass          GstVaapiImagePoolClass;

/**
 * GstVaapiImagePool:
 *
 * A pool of lazily allocated #GstVaapiImage objects.
 */
struct _GstVaapiImagePool {
    /*< private >*/
    GstVaapiVideoPool parent_instance;

    GstVaapiImagePoolPrivate *priv;
};

/**
 * GstVaapiImagePoolClass:
 *
 * A pool of lazily allocated #GstVaapiImage objects.
 */
struct _GstVaapiImagePoolClass {
    /*< private >*/
    GstVaapiVideoPoolClass parent_class;
};

GType
gst_vaapi_image_pool_get_type(void) G_GNUC_CONST;

GstVaapiVideoPool *
gst_vaapi_image_pool_new(GstVaapiDisplay *display, GstCaps *caps);

G_END_DECLS

#endif /* GST_VAAPI_IMAGE_POOL_H */
