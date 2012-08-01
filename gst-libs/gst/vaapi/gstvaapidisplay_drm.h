/*
 *  gstvaapidisplay_drm.h - VA/DRM display abstraction
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

#ifndef GST_VAAPI_DISPLAY_DRM_H
#define GST_VAAPI_DISPLAY_DRM_H

#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_DISPLAY_DRM \
    (gst_vaapi_display_drm_get_type())

#define GST_VAAPI_DISPLAY_DRM(obj)                              \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_DISPLAY_DRM,     \
                                GstVaapiDisplayDRM))

#define GST_VAAPI_DISPLAY_DRM_CLASS(klass)                      \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_DISPLAY_DRM,        \
                             GstVaapiDisplayDRMClass))

#define GST_VAAPI_IS_DISPLAY_DRM(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DISPLAY_DRM))

#define GST_VAAPI_IS_DISPLAY_DRM_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DISPLAY_DRM))

#define GST_VAAPI_DISPLAY_DRM_GET_CLASS(obj)                    \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_DISPLAY_DRM,      \
                               GstVaapiDisplayDRMClass))

typedef struct _GstVaapiDisplayDRM              GstVaapiDisplayDRM;
typedef struct _GstVaapiDisplayDRMPrivate       GstVaapiDisplayDRMPrivate;
typedef struct _GstVaapiDisplayDRMClass         GstVaapiDisplayDRMClass;

/**
 * GstVaapiDisplayDRM:
 *
 * VA/DRM display wrapper.
 */
struct _GstVaapiDisplayDRM {
    /*< private >*/
    GstVaapiDisplay parent_instance;

    GstVaapiDisplayDRMPrivate *priv;
};


/**
 * GstVaapiDisplayDRMClass:
 *
 * VA/DRM display wrapper clas.
 */
struct _GstVaapiDisplayDRMClass {
    /*< private >*/
    GstVaapiDisplayClass parent_class;
};

GType
gst_vaapi_display_drm_get_type(void) G_GNUC_CONST;

GstVaapiDisplay *
gst_vaapi_display_drm_new(const gchar *device_path);

GstVaapiDisplay *
gst_vaapi_display_drm_new_with_device(gint device);

gint
gst_vaapi_display_drm_get_device(GstVaapiDisplayDRM *display);

const gchar *
gst_vaapi_display_drm_get_device_path(GstVaapiDisplayDRM *display);

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_DRM_H */
