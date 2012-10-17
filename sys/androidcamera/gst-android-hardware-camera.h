/*
 * Copyright (C) 2012, Collabora Ltd.
 * Copyright (C) 2012, Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_ANDROID_HARDWARE_CAMERA_H__
#define __GST_ANDROID_HARDWARE_CAMERA_H__

#include <gst/gst.h>
#include <jni.h>

#include "gst-android-graphics-surfacetexture.h"
#include "gst-android-graphics-imageformat.h"

typedef struct _GstAHCamera GstAHCamera;
typedef struct _GstAHCCameraInfo GstAHCCameraInfo;
typedef struct _GstAHCSize GstAHCSize;
typedef struct _GstAHCParameters GstAHCParameters;

/* android.hardware.Camera */
struct _GstAHCamera {
  /* < private > */
  jobject object; /* global reference */
};

/* android.hardware.Camera.CameraInfo */
struct _GstAHCCameraInfo {
  gint facing;
  gint orientation;
};
extern gint CameraInfo_CAMERA_FACING_BACK;
extern gint CameraInfo_CAMERA_FACING_FRONT;

/* android.hardware.Camera.Size */
struct _GstAHCSize {
  gint width;
  gint height;
};

/* android.hardware.Camera.Parameters */
struct _GstAHCParameters {
  /* < private > */
  jobject object; /* global reference */
};

/* android.hardware.Camera.ErrorCallback */
typedef void (*GstAHCErrorCallback) (gint error, gpointer user_data);

/* android.hardware.Camera.PreviewCallback */
typedef void (*GstAHCPreviewCallback) (jbyteArray data, gpointer user_data);

gboolean gst_android_hardware_camera_init (void);

/* android.hardware.Camera */
void gst_ah_camera_add_callback_buffer (GstAHCamera *self,
    jbyteArray buffer);
gboolean gst_ah_camera_get_camera_info (gint camera_id,
    GstAHCCameraInfo *camera_info);
gint gst_ah_camera_get_number_of_cameras (void);
GstAHCParameters *gst_ah_camera_get_parameters (GstAHCamera *self);
gboolean gst_ah_camera_lock (GstAHCamera *self);
GstAHCamera * gst_ah_camera_open (gint camera_id);
gboolean gst_ah_camera_reconnect (GstAHCamera *self);
void gst_ah_camera_release (GstAHCamera *self);
gboolean gst_ah_camera_set_parameters (GstAHCamera *self,
    GstAHCParameters *params);
gboolean gst_ah_camera_set_error_callback (GstAHCamera *self,
    GstAHCErrorCallback cb, gpointer user_data);
gboolean gst_ah_camera_set_preview_callback_with_buffer (GstAHCamera *self,
    GstAHCPreviewCallback cb, gpointer user_data);
void gst_ah_camera_set_preview_texture (GstAHCamera *self,
    GstAGSurfaceTexture *surfaceTexture);
gboolean gst_ah_camera_start_preview (GstAHCamera *self);
gboolean gst_ah_camera_start_smooth_zoom (GstAHCamera *self, gint value);
gboolean gst_ah_camera_stop_preview (GstAHCamera *self);
gboolean gst_ah_camera_stop_smooth_zoom (GstAHCamera *self);
gboolean gst_ah_camera_unlock (GstAHCamera *self);

/* android.hardware.Camera.Size */
GstAHCSize *gst_ahc_size_new (gint width, gint height);
void gst_ahc_size_free (GstAHCSize *self);

/* android.hardware.Camera.Parameters */
gchar * gst_ahc_parameters_flatten (GstAHCParameters *self);
gint gst_ahc_parameters_get_preview_format (GstAHCParameters *self);
gboolean gst_ahc_parameters_get_preview_fps_range (GstAHCParameters *self,
    gint *min, gint *max);
GstAHCSize *gst_ahc_parameters_get_preview_size (GstAHCParameters *self);
/* GList <int> */
GList *gst_ahc_parameters_get_supported_preview_formats (GstAHCParameters *self);
void gst_ahc_parameters_supported_preview_formats_free (GList *list);
/* GList <int [2]> */
GList *gst_ahc_parameters_get_supported_preview_fps_range (GstAHCParameters *self);
void gst_ahc_parameters_supported_preview_fps_range_free (GList *list);
/* GList <GstAHCSize *> */
GList *gst_ahc_parameters_get_supported_preview_sizes (GstAHCParameters *self);
void gst_ahc_parameters_supported_preview_sizes_free (GList *list);
gboolean gst_ahc_parameters_set_preview_format (GstAHCParameters *self, gint format);
gboolean gst_ahc_parameters_set_preview_fps_range (GstAHCParameters *self,
    gint min, gint max);
gboolean gst_ahc_parameters_set_preview_size (GstAHCParameters *self,
    gint width, gint height);
gboolean gst_ahc_parameters_unflatten (GstAHCParameters *self, gchar *flattened);
void gst_ahc_parameters_free (GstAHCParameters *self);


#endif /* __GST_ANDROID_HARDWARE_CAMERA_H__ */

