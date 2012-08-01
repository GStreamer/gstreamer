/*
 *  gstvaapiwindow_drm.h - VA/DRM window abstraction
 *
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

#ifndef GST_VAAPI_WINDOW_DRM_H
#define GST_VAAPI_WINDOW_DRM_H

#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiwindow.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_WINDOW_DRM \
    (gst_vaapi_window_drm_get_type())

#define GST_VAAPI_WINDOW_DRM(obj)                               \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_WINDOW_DRM,      \
                                GstVaapiWindowDRM))

#define GST_VAAPI_WINDOW_DRM_CLASS(klass)                       \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_WINDOW_DRM,         \
                             GstVaapiWindowDRMClass))

#define GST_VAAPI_IS_WINDOW_DRM(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_WINDOW_DRM))

#define GST_VAAPI_IS_WINDOW_DRM_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_WINDOW_DRM))

#define GST_VAAPI_WINDOW_DRM_GET_CLASS(obj)                     \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_WINDOW_DRM,       \
                               GstVaapiWindowDRMClass))

typedef struct _GstVaapiWindowDRM               GstVaapiWindowDRM;
typedef struct _GstVaapiWindowDRMClass          GstVaapiWindowDRMClass;

/**
 * GstVaapiWindowDRM:
 *
 * A dummy DRM window abstraction.
 */
struct _GstVaapiWindowDRM {
    /*< private >*/
    GstVaapiWindow parent_instance;
};

/**
 * GstVaapiWindowDRMClass:
 *
 * A DRM window class.
 */
struct _GstVaapiWindowDRMClass {
    /*< private >*/
    GstVaapiWindowClass parent_class;
};

GType
gst_vaapi_window_drm_get_type(void) G_GNUC_CONST;

GstVaapiWindow *
gst_vaapi_window_drm_new(GstVaapiDisplay *display, guint width, guint height);

G_END_DECLS

#endif /* GST_VAAPI_WINDOW_DRM_H */
