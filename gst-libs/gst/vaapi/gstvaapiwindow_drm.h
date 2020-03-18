/*
 *  gstvaapiwindow_drm.h - VA/DRM window abstraction
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

#ifndef GST_VAAPI_WINDOW_DRM_H
#define GST_VAAPI_WINDOW_DRM_H

#include <gst/gst.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiwindow.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_WINDOW_DRM (gst_vaapi_window_drm_get_type ())
#define GST_VAAPI_WINDOW_DRM(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_WINDOW_DRM, GstVaapiWindowDRM))
#define GST_VAAPI_IS_WINDOW_DRM(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_WINDOW_DRM))

typedef struct _GstVaapiWindowDRM GstVaapiWindowDRM;

GType
gst_vaapi_window_drm_get_type (void) G_GNUC_CONST;

GstVaapiWindow *
gst_vaapi_window_drm_new (GstVaapiDisplay * display, guint width, guint height);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiWindowDRM, gst_object_unref)

G_END_DECLS

#endif /* GST_VAAPI_WINDOW_DRM_H */
