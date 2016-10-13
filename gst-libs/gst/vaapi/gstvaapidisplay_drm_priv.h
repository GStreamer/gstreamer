/*
 *  gstvaapidisplay_drm_priv.h - Internal VA/DRM interface
 *
 *  Copyright (C) 2012-2013 Intel Corporation
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

#ifndef GST_VAAPI_DISPLAY_DRM_PRIV_H
#define GST_VAAPI_DISPLAY_DRM_PRIV_H

#include <gst/vaapi/gstvaapidisplay_drm.h>
#include "gstvaapidisplay_priv.h"

G_BEGIN_DECLS

#define GST_VAAPI_IS_DISPLAY_DRM(display) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((display), GST_TYPE_VAAPI_DISPLAY_DRM))

#define GST_VAAPI_DISPLAY_DRM_CAST(display) \
    ((GstVaapiDisplayDRM *)(display))

#define GST_VAAPI_DISPLAY_DRM_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_DISPLAY_DRM, GstVaapiDisplayDRMClass))

#define GST_VAAPI_DISPLAY_DRM_PRIVATE(display) \
    (GST_VAAPI_DISPLAY_DRM_CAST(display)->priv)

typedef struct _GstVaapiDisplayDRMPrivate       GstVaapiDisplayDRMPrivate;
typedef struct _GstVaapiDisplayDRMClass         GstVaapiDisplayDRMClass;

/**
 * GST_VAAPI_DISPLAY_DRM_DEVICE:
 * @display: a #GstVaapiDisplay
 *
 * Macro that evaluates to the underlying DRM file descriptor of @display
 */
#undef  GST_VAAPI_DISPLAY_DRM_DEVICE
#define GST_VAAPI_DISPLAY_DRM_DEVICE(display) \
    GST_VAAPI_DISPLAY_DRM_PRIVATE(display)->drm_device

struct _GstVaapiDisplayDRMPrivate
{
  gchar *device_path_default;
  gchar *device_path;
  gint drm_device;
  guint use_foreign_display:1;  // Foreign native_display?
};

/**
 * GstVaapiDisplayDRM:
 *
 * VA/DRM display wrapper.
 */
struct _GstVaapiDisplayDRM
{
  /*< private >*/
  GstVaapiDisplay parent_instance;

  GstVaapiDisplayDRMPrivate *priv;
};

/**
 * GstVaapiDisplayDRMClass:
 *
 * VA/DRM display wrapper clas.
 */
struct _GstVaapiDisplayDRMClass
{
  /*< private >*/
  GstVaapiDisplayClass parent_class;
};

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_DRM_PRIV_H */
